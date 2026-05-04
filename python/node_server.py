print("===========================================================================================================Starting node server...", flush=True)
import argparse
import collections
import csv
import os
import threading
import time
from concurrent import futures
from dataclasses import dataclass
import logging
import signal
import sys
import traceback

import grpc

from generated import basecamp_pb2
from generated import basecamp_pb2_grpc

print("===========================================================================================================Imports complete, starting main...", flush=True)  

# Global server handle used by signal handlers for graceful shutdown
GLOBAL_SERVER = None


class NodeConfig:
    def __init__(self, node_id, host, port, language, row, col, is_entry):
        self.node_id = node_id
        self.host = host
        self.port = int(port)
        self.language = language
        self.row = int(row)
        self.col = int(col)
        self.is_entry = str(is_entry).strip().lower() in {"1", "true", "yes"}


def load_nodes(csv_path):
    nodes = {}
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            node = NodeConfig(
                row["node_id"],
                row["host"],
                row["port"],
                row["language"],
                row["row"],
                row["col"],
                row["is_entry"],
            )
            nodes[node.node_id] = node
    return nodes


def neighbors_for(nodes, node_id):
    me = nodes[node_id]
    output = []
    for candidate in nodes.values():
        if candidate.node_id == node_id:
            continue
        distance = abs(candidate.row - me.row) + abs(candidate.col - me.col)
        if distance == 1:
            output.append(candidate)
    return output


class BasecampServicer(basecamp_pb2_grpc.BasecampNodeServicer):
    def __init__(self, node, nodes, steal_below, max_steal, process_ms, rebalance_ms, log_file="", log_interval_ms=1000):
        self.node = node
        self.nodes = nodes
        self.steal_below = steal_below
        self.max_steal = max_steal
        self.process_ms = process_ms
        self.rebalance_ms = rebalance_ms
        self.log_file = log_file
        self.log_interval_ms = log_interval_ms

        self.queue = collections.deque()
        self.last_seen = {}
        self.consumed_tasks = []
        self.by_borough = collections.Counter()
        self.by_job_type = collections.Counter()
        self.lock = threading.Lock()
        self.running = True

        self.neighbors = neighbors_for(nodes, node.node_id)
        self.stubs = {}
        for neighbor in self.neighbors:
            channel = grpc.insecure_channel(f"{neighbor.host}:{neighbor.port}")
            self.stubs[neighbor.node_id] = basecamp_pb2_grpc.BasecampNodeStub(channel)
            self.last_seen[neighbor.node_id] = 0

        logging.debug(f"[{self.node.node_id}] initialized with {len(self.neighbors)} neighbors")

        self.worker = threading.Thread(target=self._worker_loop, daemon=True)
        self.worker.start()

        if self.log_file:
            self.logging_thread = threading.Thread(target=self._logging_loop, daemon=True)
            self.logging_thread.start()

    def SubmitJobs(self, request, context):
        try:
            with self.lock:
                count = 0
                for job in request.jobs:
                    self.queue.append(job)
                    count += 1
            logging.debug(f"[{self.node.node_id}] SubmitJobs received {count} jobs")
            return basecamp_pb2.SubmitJobsReply(accepted=count)
        except Exception:
            logging.exception(f"[{self.node.node_id}] Exception in SubmitJobs")
            return basecamp_pb2.SubmitJobsReply(accepted=0)

    def QueueStatus(self, request, context):
        del request
        return basecamp_pb2.QueueStatusReply(node_id=self.node.node_id, queue_size=self._queue_size())

    def BroadcastQueue(self, request, context):
        with self.lock:
            self.last_seen[request.node_id] = request.queue_size
        return basecamp_pb2.QueueStatusReply(node_id=self.node.node_id, queue_size=self._queue_size())

    def StealJobs(self, request, context):
        jobs = []
        with self.lock:
            steal_n = min(int(request.max_jobs), len(self.queue))
            for _ in range(steal_n):
                jobs.append(self.queue.popleft())
        return basecamp_pb2.StealJobsReply(jobs=jobs)

    def Tick(self, request, context):
        del request
        stolen = self._rebalance_once()
        return basecamp_pb2.TickReply(queue_size=self._queue_size(), stolen=stolen)

    def GetSnapshot(self, request, context):
        del request
        with self.lock:
            neighbors = [
                basecamp_pb2.NeighborSnapshot(node_id=node_id, queue_size=qsize)
                for node_id, qsize in self.last_seen.items()
            ]
            consumed_count = len(self.consumed_tasks)
        return basecamp_pb2.SnapshotReply(
            node_id=self.node.node_id,
            queue_size=self._queue_size(),
            neighbors=neighbors,
            consumed_count=consumed_count,
        )

    def SubmitQuery(self, request, context):
        filter_str = request.query_filter
        filter_parts = self._parse_query_filter(filter_str)
        if not filter_parts:
            return basecamp_pb2.QueryResponse(query_id=request.query_id)

        aggregated_results = []
        with self.lock:
            local_results = self._execute_query(self.consumed_tasks, filter_parts)
            for task in local_results:
                result = basecamp_pb2.QueryResult(
                    job_number=task.job_number,
                    doc_number=task.doc_number,
                    borough=task.borough,
                    job_type=task.job_type,
                    owner_business_name=task.owner_business_name,
                    zip=task.zip_code,
                )
                aggregated_results.append(result)

        downstream_request = basecamp_pb2.BroadcastQueryRequest(
            query_id=request.query_id,
            query_filter=filter_str,
            originating_node=self.node.node_id,
        )

        for neighbor_id, stub in self.stubs.items():
            try:
                reply = stub.BroadcastQuery(downstream_request, timeout=2.0)
                for result in reply.aggregated_results:
                    aggregated_results.append(result)
            except grpc.RpcError:
                continue

        response = basecamp_pb2.QueryResponse(query_id=request.query_id, total_results=len(aggregated_results))
        for result in aggregated_results:
            response.results.append(result)
        return response

    def BroadcastQuery(self, request, context):
        filter_str = request.query_filter
        filter_parts = self._parse_query_filter(filter_str)
        if not filter_parts:
            return basecamp_pb2.BroadcastQueryReply(node_id=self.node.node_id)

        reply = basecamp_pb2.BroadcastQueryReply(node_id=self.node.node_id)

        with self.lock:
            local_results = self._execute_query(self.consumed_tasks, filter_parts)
            for task in local_results:
                result = basecamp_pb2.QueryResult(
                    job_number=task.job_number,
                    doc_number=task.doc_number,
                    borough=task.borough,
                    job_type=task.job_type,
                    owner_business_name=task.owner_business_name,
                    zip=task.zip_code,
                )
                reply.local_results.append(result)

        downstream_request = basecamp_pb2.BroadcastQueryRequest(
            query_id=request.query_id,
            query_filter=filter_str,
            originating_node=request.originating_node,
        )

        for neighbor_id, stub in self.stubs.items():
            if neighbor_id == request.originating_node:
                continue
            try:
                nested_reply = stub.BroadcastQuery(downstream_request, timeout=2.0)
                for result in nested_reply.local_results:
                    reply.aggregated_results.append(result)
                for result in nested_reply.aggregated_results:
                    reply.aggregated_results.append(result)
            except grpc.RpcError:
                continue

        for result in reply.local_results:
            reply.aggregated_results.append(result)

        return reply

    def _parse_query_filter(self, filter_str):
        for op in [">=", "<=", ">", "<", "="]:
            if op in filter_str:
                parts = filter_str.split(op, 1)
                if len(parts) == 2:
                    return (parts[0].strip(), op, parts[1].strip())
        return None

    def _execute_query(self, tasks, filter_parts):
        if not filter_parts:
            return []
        field, op, value = filter_parts
        results = []
        for task in tasks:
            if self._matches_filter(task, field, op, value):
                results.append(task)
        return results

    def _matches_filter(self, task, field, op, value):
        if field == "borough":
            if op == "=":
                return task.borough == value
            return False
        if field == "job_type":
            if op == "=":
                return task.job_type == value
            return False
        if field == "job_status":
            if op == "=":
                return task.job_status == value
            return False
        if field == "zip":
            if op == "=":
                return task.zip_code == value
            try:
                task_zip = int(task.zip_code)
                value_int = int(value)
                if op == ">":
                    return task_zip > value_int
                if op == "<":
                    return task_zip < value_int
                if op == ">=":
                    return task_zip >= value_int
                if op == "<=":
                    return task_zip <= value_int
            except ValueError:
                return False
            return False
        if field == "job_number":
            try:
                value_int = int(value)
                if op == "=":
                    return task.job_number == value_int
                if op == ">":
                    return task.job_number > value_int
                if op == "<":
                    return task.job_number < value_int
                if op == ">=":
                    return task.job_number >= value_int
                if op == "<=":
                    return task.job_number <= value_int
            except ValueError:
                return False
            return False
        if field == "doc_number":
            try:
                value_int = int(value)
                if op == "=":
                    return task.doc_number == value_int
                if op == ">":
                    return task.doc_number > value_int
                if op == "<":
                    return task.doc_number < value_int
                if op == ">=":
                    return task.doc_number >= value_int
                if op == "<=":
                    return task.doc_number <= value_int
            except ValueError:
                return False
            return False
        return False

    @dataclass
    class DobTask:
        job_id: int
        csv_line_number: int
        job_number: int
        doc_number: int
        borough: str
        house_number: str
        street_name: str
        job_type: str
        job_status: str
        owner_business_name: str
        zip_code: str
        job_description: str

    def _parse_dob_task(self, job):
        if not job.csv_row:
            return None

        fields = next(csv.reader([job.csv_row]))
        if len(fields) < 82:
            return None

        def to_int(value):
            return int(value) if value else 0

        try:
            return self.DobTask(
                job_id=job.job_id,
                csv_line_number=job.csv_line_number,
                job_number=to_int(fields[0]),
                doc_number=to_int(fields[1]),
                borough=fields[2],
                house_number=fields[3],
                street_name=fields[4],
                job_type=fields[8],
                job_status=fields[9],
                owner_business_name=fields[74],
                zip_code=fields[78],
                job_description=fields[80],
            )
        except (ValueError, IndexError) as e:
            logging.debug(f"[{getattr(job, 'job_id', 'unknown')}] parse failed: {e}")
            return None

    def _queue_size(self):
        with self.lock:
            return len(self.queue)

    def _broadcast_queue_size(self, local_size):
        for node_id, stub in self.stubs.items():
            try:
                reply = stub.BroadcastQueue(
                    basecamp_pb2.QueueBroadcast(node_id=self.node.node_id, queue_size=local_size), timeout=0.5
                )
                with self.lock:
                    self.last_seen[node_id] = reply.queue_size
            except grpc.RpcError:
                continue

    def _refresh_neighbor_sizes(self):
        for node_id, stub in self.stubs.items():
            try:
                reply = stub.QueueStatus(basecamp_pb2.QueueStatusRequest(requester_id=self.node.node_id), timeout=0.5)
                with self.lock:
                    self.last_seen[node_id] = reply.queue_size
            except grpc.RpcError:
                continue

    def _rebalance_once(self):
        local = self._queue_size()
        self._broadcast_queue_size(local)
        if local >= self.steal_below:
            return 0

        self._refresh_neighbor_sizes()
        with self.lock:
            if not self.last_seen:
                return 0
            donor_id = max(self.last_seen, key=self.last_seen.get)
            donor_size = int(self.last_seen[donor_id])

        if donor_size <= local + 1:
            return 0

        want = max(1, (donor_size - local) // 2)
        request_n = min(self.max_steal, want)

        try:
            response = self.stubs[donor_id].StealJobs(
                basecamp_pb2.StealJobsRequest(requester_id=self.node.node_id, max_jobs=request_n), timeout=0.8
            )
        except grpc.RpcError:
            return 0

        with self.lock:
            for job in response.jobs:
                self.queue.append(job)
        return len(response.jobs)

    def _worker_loop(self):
        next_rebalance = time.time() + (self.rebalance_ms / 1000.0)
        while self.running:
            did_work = False
            consumed_job = None
            with self.lock:
                if self.queue:
                    consumed_job = self.queue.popleft()
                    did_work = True

            if did_work:
                try:
                    task = self._parse_dob_task(consumed_job)
                    if task is not None:
                        with self.lock:
                            self.consumed_tasks.append(task)
                            self.by_borough[task.borough] += 1
                            self.by_job_type[task.job_type] += 1
                    else:
                        logging.debug(f"[{self.node.node_id}] dropped job during parse: {getattr(consumed_job, 'job_id', 'unknown')}")
                except Exception:
                    logging.exception(f"[{self.node.node_id}] Exception in worker processing job {getattr(consumed_job, 'job_id', 'unknown')}")
                time.sleep(self.process_ms / 1000.0)
            else:
                time.sleep(0.05)

            now = time.time()
            if now >= next_rebalance:
                stolen = self._rebalance_once()
                if stolen > 0:
                    print(f"[{self.node.node_id}] stole {stolen} jobs", flush=True)
                next_rebalance = now + (self.rebalance_ms / 1000.0)

    def _logging_loop(self):
        next_log = time.time() + (self.log_interval_ms / 1000.0)
        file_exists = os.path.exists(self.log_file)

        while self.running:
            now = time.time()
            if now >= next_log:
                self._log_queue_size(file_exists)
                file_exists = True
                next_log = now + (self.log_interval_ms / 1000.0)
            time.sleep(0.05)

    def _log_queue_size(self, file_exists):
        try:
            with open(self.log_file, "a") as f:
                if not file_exists:
                    f.write("timestamp_us,node_id,queue_size\n")

                timestamp_us = int(time.time() * 1_000_000)
                qsize = self._queue_size()
                f.write(f"{timestamp_us},{self.node.node_id},{qsize}\n")
                f.flush()
        except IOError:
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--node-id", required=True)
    parser.add_argument("--config", default="config/grid_nodes.csv")
    parser.add_argument("--steal-below", type=int, default=2)
    parser.add_argument("--max-steal", type=int, default=4)
    parser.add_argument("--process-ms", type=int, default=150)
    parser.add_argument("--rebalance-ms", type=int, default=1000)
    parser.add_argument("--log-file", default="")
    parser.add_argument("--log-interval", type=int, default=1000)
    args = parser.parse_args()

    # Configure logging
    if args.log_file:
        logging.basicConfig(filename=args.log_file, level=logging.DEBUG, format='%(asctime)s %(levelname)s %(message)s')
    else:
        logging.basicConfig(stream=sys.stderr, level=logging.DEBUG, format='%(asctime)s %(levelname)s %(message)s')

    # Global exception hook to capture uncaught exceptions
    def _excepthook(exc_type, exc_value, exc_traceback):
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc_value, exc_traceback)
            return
        logging.critical("Uncaught exception", exc_info=(exc_type, exc_value, exc_traceback))

    sys.excepthook = _excepthook

    nodes = load_nodes(args.config)
    if args.node_id not in nodes:
        raise ValueError(f"Unknown node id {args.node_id}")

    node = nodes[args.node_id]
    servicer = BasecampServicer(
        node=node,
        nodes=nodes,
        steal_below=args.steal_below,
        max_steal=args.max_steal,
        process_ms=args.process_ms,
        rebalance_ms=args.rebalance_ms,
        log_file=args.log_file,
        log_interval_ms=args.log_interval,
    )
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=16))
    basecamp_pb2_grpc.add_BasecampNodeServicer_to_server(servicer, server)
    server.add_insecure_port(f"0.0.0.0:{node.port}")

    # Signal handler to gracefully shutdown
    def _handle_signal(sig, frame):
        logging.info(f"Signal {sig} received, shutting down node {node.node_id}")
        try:
            servicer.running = False
        except Exception:
            logging.exception("Error setting servicer.running=False")
        try:
            if GLOBAL_SERVER is not None:
                GLOBAL_SERVER.stop(0)
        except Exception:
            logging.exception("Error stopping gRPC server")

    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    try:
        global GLOBAL_SERVER
        server.start()
        GLOBAL_SERVER = server
        logging.info(f"Node {node.node_id} listening on {node.port}")
        print(f"Node {node.node_id} listening on {node.port}", flush=True)
        server.wait_for_termination()
    except Exception:
        logging.exception(f"Node {node.node_id} terminated with exception")
    finally:
        try:
            servicer.running = False
        except Exception:
            pass
        logging.info(f"Node {node.node_id} shutting down")


if __name__ == "__main__":
    main()
