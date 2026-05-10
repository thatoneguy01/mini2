#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "Config.hpp"
#include "DobTask.hpp"
#include "QueryEngine.hpp"
#include "basecamp.grpc.pb.h"

namespace {

class NodeServiceImpl final : public basecamp::BasecampNode::Service {
 public:
  NodeServiceImpl(basecamp::NodeInfo self, basecamp::GridConfig config, int steal_when_below,
                  int steal_ratio, int max_steal_batch, int process_ms, int rebalance_ms,
                  const std::string& log_file, int log_interval_ms)
      : self_(std::move(self)),
        config_(std::move(config)),
        steal_when_below_(steal_when_below),
        steal_ratio_(steal_ratio),
        max_steal_batch_(max_steal_batch),
        process_ms_(process_ms),
        rebalance_ms_(rebalance_ms),
        log_file_(log_file),
        log_interval_ms_(log_interval_ms) {
    for (const auto& neighbor : config_.Neighbors(self_.node_id)) {
      const std::string endpoint = basecamp::ToEndpoint(neighbor);
      neighbor_stubs_[neighbor.node_id] =
          basecamp::BasecampNode::NewStub(grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
      last_seen_queue_size_[neighbor.node_id] = 0;
    }
  }

  void StartBackgroundLoops() {
    running_ = true;
    worker_thread_ = std::thread([this]() { WorkerLoop(); });
    rebalancer_thread_ = std::thread([this]() { RebalanceLoop(); });
    if (!log_file_.empty()) {
      logging_thread_ = std::thread([this]() { LoggingLoop(); });
    }
  }

  void StopBackgroundLoops() {
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    if (rebalancer_thread_.joinable()) {
      rebalancer_thread_.join();
    }
    if (logging_thread_.joinable()) {
      logging_thread_.join();
    }
  }

  grpc::Status SubmitJobs(grpc::ServerContext*, const basecamp::SubmitJobsRequest* request,
                          basecamp::SubmitJobsReply* reply) override {
    // const auto t_start = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto& job : request->jobs()) {
        queue_.push_back(job);
      }
    }
    // const auto t_end = std::chrono::steady_clock::now();
    // const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
    // std::cerr << "[" << self_.node_id << "] SubmitJobs lock held_us=" << us << " num=" << request->jobs_size() << std::endl;
    reply->set_accepted(static_cast<uint32_t>(request->jobs_size()));
    return grpc::Status::OK;
  }

  grpc::Status BroadcastQueue(grpc::ServerContext*, const basecamp::QueueBroadcast* request,
                              basecamp::QueueStatusReply* reply) override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      last_seen_queue_size_[request->node_id()] = request->queue_size();
    }
    reply->set_node_id(self_.node_id);
    reply->set_queue_size(static_cast<uint32_t>(QueueSize()));
    return grpc::Status::OK;
  }

  grpc::Status StealJobs(grpc::ServerContext*, const basecamp::StealJobsRequest* request,
                         basecamp::StealJobsReply* reply) override {
    // AcceptStealJobs: Give jobs to requester without pre-checks.
    // The requester's decision to steal is based on cached queue size info.
    // const auto t_start = std::chrono::steady_clock::now();
    std::vector<basecamp::Job> stolen_jobs;
    {
      std::lock_guard<std::mutex> lock(mu_);
      const size_t steal_count = std::min(static_cast<size_t>(request->max_jobs()), queue_.size());
      for (size_t i = 0; i < steal_count; ++i) {
        stolen_jobs.push_back(queue_.back());
        queue_.pop_back();
      }
    }
    // const auto t_lock_end = std::chrono::steady_clock::now();
    
    // Add jobs to reply outside the lock (protobuf serialization is expensive)
    for (const auto& job : stolen_jobs) {
      auto* reply_job = reply->add_jobs();
      *reply_job = job;
    }
    
    // const auto t_end = std::chrono::steady_clock::now();
    // const auto lock_us = std::chrono::duration_cast<std::chrono::microseconds>(t_lock_end - t_start).count();
    // const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
    // std::cerr << "[" << self_.node_id << "] StealJobs lock_us=" << lock_us << " total_us=" << total_us << " num=" << stolen_jobs.size() << std::endl;
    return grpc::Status::OK;
  }

  grpc::Status Tick(grpc::ServerContext*, const basecamp::TickRequest*, basecamp::TickReply* reply) override {
    const int stolen = RebalanceOnce();
    reply->set_stolen(static_cast<uint32_t>(stolen));
    reply->set_queue_size(static_cast<uint32_t>(QueueSize()));
    return grpc::Status::OK;
  }

  grpc::Status GetSnapshot(grpc::ServerContext*, const basecamp::SnapshotRequest*,
                           basecamp::SnapshotReply* reply) override {
    reply->set_node_id(self_.node_id);
    reply->set_queue_size(static_cast<uint32_t>(QueueSize()));
    std::lock_guard<std::mutex> lock(mu_);
    reply->set_consumed_count(static_cast<uint32_t>(consumed_tasks_.size()));
    for (const auto& [node_id, queue_size] : last_seen_queue_size_) {
      auto* neighbor = reply->add_neighbors();
      neighbor->set_node_id(node_id);
      neighbor->set_queue_size(queue_size);
    }
    return grpc::Status::OK;
  }

  grpc::Status SubmitQuery(grpc::ServerContext*, const basecamp::QueryRequest* request,
                           basecamp::QueryResponse* reply) override {
    reply->set_query_id(request->query_id());
    const auto filter = basecamp::ParseQueryFilter(request->query_filter());
    if (!filter.has_value()) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid query filter");
    }

    basecamp::BroadcastQueryRequest bcast_request;
    bcast_request.set_query_id(request->query_id());
    bcast_request.set_query_filter(request->query_filter());
    bcast_request.set_originating_node(self_.node_id);

    std::vector<basecamp::QueryResult> aggregated_results;

    {
      std::lock_guard<std::mutex> lock(mu_);
      const auto local_results = basecamp::ExecuteQueryOnTasks(consumed_tasks_, *filter);
      for (const auto& task : local_results) {
        auto* result = reply->add_results();
        result->set_job_number(task.job_number);
        result->set_doc_number(task.doc_number);
        result->set_borough(task.borough);
        result->set_job_type(task.job_type);
        result->set_owner_business_name(task.owner_business_name);
        result->set_zip(task.zip);
        result->set_raw_csv_row(task.raw_csv_row);
        aggregated_results.push_back(*result);
      }
    }

    std::vector<std::string> neighbor_ids;
    for (const auto& [neighbor_id, _] : neighbor_stubs_) {
      neighbor_ids.push_back(neighbor_id);
    }

    for (const auto& neighbor_id : neighbor_ids) {
      auto stub_it = neighbor_stubs_.find(neighbor_id);
      if (stub_it == neighbor_stubs_.end()) {
        continue;
      }

      grpc::ClientContext ctx;
      basecamp::BroadcastQueryReply bcast_reply;
      const grpc::Status status = stub_it->second->BroadcastQuery(&ctx, bcast_request, &bcast_reply);
      if (status.ok()) {
        for (const auto& result : bcast_reply.aggregated_results()) {
          auto* reply_result = reply->add_results();
          *reply_result = result;
          aggregated_results.push_back(result);
        }
      }
    }

    reply->set_total_results(static_cast<uint32_t>(aggregated_results.size()));
    return grpc::Status::OK;
  }

  grpc::Status BroadcastQuery(grpc::ServerContext*, const basecamp::BroadcastQueryRequest* request,
                              basecamp::BroadcastQueryReply* reply) override {
    reply->set_node_id(self_.node_id);

    const auto filter = basecamp::ParseQueryFilter(request->query_filter());
    if (!filter.has_value()) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid query filter");
    }

    int local_result_count = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      const auto local_results = basecamp::ExecuteQueryOnTasks(consumed_tasks_, *filter);
      for (const auto& task : local_results) {
        auto* result = reply->add_local_results();
        result->set_job_number(task.job_number);
        result->set_doc_number(task.doc_number);
        result->set_borough(task.borough);
        result->set_job_type(task.job_type);
        result->set_owner_business_name(task.owner_business_name);
        result->set_zip(task.zip);
        result->set_raw_csv_row(task.raw_csv_row);
      }
      local_result_count = local_results.size();
    }

    basecamp::BroadcastQueryRequest downstream_request = *request;

    for (const auto& [neighbor_id, stub] : neighbor_stubs_) {
      if (neighbor_id == request->originating_node()) {
        continue;
      }

      grpc::ClientContext ctx;
      basecamp::BroadcastQueryReply nested_reply;
      const grpc::Status status = stub->BroadcastQuery(&ctx, downstream_request, &nested_reply);
      if (status.ok()) {
        for (const auto& result : nested_reply.local_results()) {
          auto* agg = reply->add_aggregated_results();
          *agg = result;
        }
        for (const auto& result : nested_reply.aggregated_results()) {
          auto* agg = reply->add_aggregated_results();
          *agg = result;
        }
      }
    }

    for (int i = 0; i < local_result_count; ++i) {
      auto* agg = reply->add_aggregated_results();
      *agg = reply->local_results(i);
    }

    return grpc::Status::OK;
  }

 private:
  size_t QueueSize() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
  }

  int RebalanceOnce() {
    const int local_queue_size = static_cast<int>(QueueSize());

    // Skip if we have enough work
    if (local_queue_size >= steal_when_below_) {
      return 0;
    }

    std::optional<std::string> best_neighbor;
    int best_size = -1;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto& [node_id, qsize] : last_seen_queue_size_) {
        if (static_cast<int>(qsize) > best_size) {
          best_size = static_cast<int>(qsize);
          best_neighbor = node_id;
        }
      }
    }

    // Skip if no neighbor info or donor is empty
    if (!best_neighbor.has_value() || best_size == 0) {
      return 0;
    }

    int request_count;
    if (steal_ratio_ == 0) {
      // Use max_steal_batch cap (traditional behavior)
      const int desired = std::max(1, (best_size - local_queue_size) / 2);
      request_count = std::min(max_steal_batch_, desired);
    } else {
      // Use steal_ratio calculation
      request_count = std::max(1, (best_size - local_queue_size) / steal_ratio_);
    }

    auto stub_it = neighbor_stubs_.find(*best_neighbor);
    if (stub_it == neighbor_stubs_.end()) {
      return 0;
    }

    grpc::ClientContext ctx;
    basecamp::StealJobsRequest request;
    request.set_requester_id(self_.node_id);
    request.set_max_jobs(static_cast<uint32_t>(request_count));

    basecamp::StealJobsReply response;
    const grpc::Status status = stub_it->second->StealJobs(&ctx, request, &response);
    if (!status.ok()) {
      return 0;
    }

    // Instrument lock hold while inserting stolen jobs into our queue
    // const auto t_start = std::chrono::steady_clock::now();
    // size_t inserted = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto& job : response.jobs()) {
        queue_.push_back(job);
        // ++inserted;
      }
    }
    // const auto t_end = std::chrono::steady_clock::now();
    // const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
    // if (inserted > 0) {
      // std::cerr << "[" << self_.node_id << "] Rebalance insert lock_us=" << us << " inserted=" << inserted << std::endl;
    // }

    return static_cast<int>(response.jobs_size());
  }

  void BroadcastLocalQueueSize(int local_queue_size) {
    for (auto& [neighbor_id, stub] : neighbor_stubs_) {
      grpc::ClientContext ctx;
      basecamp::QueueBroadcast request;
      request.set_node_id(self_.node_id);
      request.set_queue_size(static_cast<uint32_t>(local_queue_size));
      basecamp::QueueStatusReply response;
      const grpc::Status status = stub->BroadcastQueue(&ctx, request, &response);
      if (status.ok()) {
        std::lock_guard<std::mutex> lock(mu_);
        last_seen_queue_size_[neighbor_id] = response.queue_size();
      }
    }
  }

  void RefreshNeighborQueueSizes() {
    // DEPRECATED: Nodes no longer poll. They broadcast and cache instead.
  }

  void WorkerLoop() {
    while (running_) {
      bool did_work = false;
      basecamp::Job consumed_job;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!queue_.empty()) {
          consumed_job = queue_.front();
          queue_.pop_front();
          did_work = true;
        }
      }

      if (did_work) {
        if (!consumed_job.csv_row().empty()) {
          const auto parsed = basecamp::ParseDobTaskFromCsvRow(consumed_job.job_id(), consumed_job.csv_line_number(),
                                                                consumed_job.csv_row());
          if (parsed.has_value()) {
            std::lock_guard<std::mutex> lock(mu_);
            consumed_tasks_.push_back(*parsed);
            ++borough_counts_[parsed->borough];
            ++job_type_counts_[parsed->job_type];
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(process_ms_));
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  }

  void RebalanceLoop() {
    // Separate thread: periodically broadcast queue size and attempt to steal.

    while (running_) {
      const int local_size = static_cast<int>(QueueSize());
      BroadcastLocalQueueSize(local_size);
      // const int stolen = RebalanceOnce();
      // if (stolen > 0) {
      //   std::cout << "[" << self_.node_id << "] stole " << stolen << " jobs" << std::endl;
      // }
      std::this_thread::sleep_for(std::chrono::milliseconds(rebalance_ms_));
    }
  }

  void LoggingLoop() {
    auto next_log = std::chrono::steady_clock::now() + std::chrono::milliseconds(log_interval_ms_);
    
    // Write CSV header if file is new
    bool file_exists = false;
    {
      std::ifstream f(log_file_);
      file_exists = f.good();
    }
    
    while (running_) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_log) {
        LogQueueSize(file_exists);
        file_exists = true;
        next_log = now + std::chrono::milliseconds(log_interval_ms_);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  void LogQueueSize(bool file_exists) {
    std::ofstream log(log_file_, std::ios::app);
    if (!log.is_open()) {
      return;
    }

    if (!file_exists) {
      log << "timestamp_us,node_id,queue_size,jobs_processed\n";
    }

    const auto now = std::chrono::system_clock::now();
    const auto duration = now.time_since_epoch();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    
    const int qsize = QueueSize();
    const int jobs_processed = static_cast<int>(consumed_tasks_.size());
    log << us << "," << self_.node_id << "," << qsize << "," << jobs_processed << "\n";
    log.flush();
  }

  const basecamp::NodeInfo self_;
  const basecamp::GridConfig config_;
  const int steal_when_below_;
  const int steal_ratio_;
  const int max_steal_batch_;
  const int process_ms_;
  const int rebalance_ms_;
  const std::string log_file_;
  const int log_interval_ms_;

  mutable std::mutex mu_;
  std::deque<basecamp::Job> queue_;
  std::vector<basecamp::DobTask> consumed_tasks_;
  std::unordered_map<std::string, uint64_t> borough_counts_;
  std::unordered_map<std::string, uint64_t> job_type_counts_;
  std::unordered_map<std::string, uint32_t> last_seen_queue_size_;
  std::unordered_map<std::string, std::unique_ptr<basecamp::BasecampNode::Stub>> neighbor_stubs_;

  bool running_ = false;
  std::thread worker_thread_;
  std::thread rebalancer_thread_;
  std::thread logging_thread_;
};

std::string ArgValue(int argc, char** argv, const std::string& name, const std::string& fallback) {
  for (int i = 1; i < argc - 1; ++i) {
    if (name == argv[i]) {
      return argv[i + 1];
    }
  }
  return fallback;
}

int ArgValueInt(int argc, char** argv, const std::string& name, int fallback) {
  for (int i = 1; i < argc - 1; ++i) {
    if (name == argv[i]) {
      return std::stoi(argv[i + 1]);
    }
  }
  return fallback;
}

float ArgValueFloat(int argc, char** argv, const std::string& name, float fallback) {
  for (int i = 1; i < argc - 1; ++i) {
    if (name == argv[i]) {
      return std::stof(argv[i + 1]);
    }
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string node_id = ArgValue(argc, argv, "--node-id", "");
  const std::string config_path = ArgValue(argc, argv, "--config", "config/grid_nodes.csv");
  const int steal_when_below = ArgValueInt(argc, argv, "--steal-below", 50);
  const int steal_ratio = ArgValueInt(argc, argv, "--steal-ratio", 0);
  const int max_steal_batch = ArgValueInt(argc, argv, "--max-steal", 500);
  const float process_ms = ArgValueFloat(argc, argv, "--process-ms", 0.1);
  const int rebalance_ms = ArgValueInt(argc, argv, "--rebalance-ms", 1000);
  const std::string log_file = ArgValue(argc, argv, "--log-file", "");
  const int log_interval_ms = ArgValueInt(argc, argv, "--log-interval", 1000);

  if (node_id.empty()) {
    std::cerr << "Missing required --node-id" << std::endl;
    return 1;
  }

  basecamp::GridConfig config;
  std::string error;
  if (!config.LoadFromCsv(config_path, &error)) {
    std::cerr << error << std::endl;
    return 1;
  }

  const auto self = config.GetNode(node_id);
  if (!self.has_value()) {
    std::cerr << "node_id not found in config: " << node_id << std::endl;
    return 1;
  }

  NodeServiceImpl service(*self, config, steal_when_below, steal_ratio, max_steal_batch, process_ms, rebalance_ms,
                         log_file, log_interval_ms);
  service.StartBackgroundLoops();

  grpc::ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:" + std::to_string(self->port), grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::cerr << "Failed to start gRPC server for node " << self->node_id << std::endl;
    service.StopBackgroundLoops();
    return 1;
  }

  std::cout << "Node " << self->node_id << " listening on " << self->port << std::endl;
  server->Wait();

  service.StopBackgroundLoops();
  return 0;
}
