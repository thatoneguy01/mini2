# Mini 2 Basecamp (Grid Overlay + Work Stealing)

This implementation is a from-scratch Basecamp in the root project folder and uses `reference/` only as design inspiration.

It satisfies the requested requirements:
- Grid overlay (3x3 by configuration, not hardcoded in code)
- gRPC communication without gRPC async/streaming APIs
- C++ client
- C++ server
- Python server
- No hardcoded server identity/role/hostname in source code
- Work stealing via neighbor queue broadcasts and neighbor-max steal selection

## Structure

- `proto/basecamp.proto`: shared service and typed data structures
- `config/grid_nodes.csv`: overlay map and host assignment
- `cpp/common/Config.hpp`, `cpp/common/Config.cpp`: CSV config + grid neighbor derivation
- `cpp/server/node_server.cpp`: C++ gRPC server node
- `cpp/client/basecamp_client.cpp`: C++ load-injection client (targets entry node)
- `python/node_server.py`: Python gRPC server node
- `python/build_proto.ps1`: generate Python protobuf/gRPC files

## Work-Stealing Behavior

Each node runs two loops:
- Processing loop: consumes local queued jobs (simulated processing delay)
- Rebalance loop:
1. Broadcast own queue size to all grid neighbors (`BroadcastQueue`)
2. If local queue is below threshold (`--steal-below`), query neighbor queue sizes
3. Select neighbor with highest queue
4. Steal a bounded job batch (`StealJobs`) from that neighbor

This follows your requested policy: broadcast queue levels and steal from the neighbor with the most queued jobs.

## Build C++

Prerequisites:
- CMake
- Protobuf
- gRPC (C++ toolchain and CMake package config)

```powershell
cd mini2
cmake -S . -B build
cmake --build build --config Release
```

## Prepare Python

```powershell
cd mini2\python
python -m pip install -r requirements.txt
.\build_proto.ps1
```

## Run Nodes (Example: mixed C++ and Python)

Open separate shells for each node. Example mix (from project root):

C++ nodes:
```powershell
.\build\basecamp_cpp_server.exe --node-id A --config config/grid_nodes.csv
.\build\basecamp_cpp_server.exe --node-id B --config config/grid_nodes.csv
.\build\basecamp_cpp_server.exe --node-id D --config config/grid_nodes.csv
.\build\basecamp_cpp_server.exe --node-id F --config config/grid_nodes.csv
.\build\basecamp_cpp_server.exe --node-id G --config config/grid_nodes.csv
.\build\basecamp_cpp_server.exe --node-id I --config config/grid_nodes.csv
```

Python nodes:
```powershell
cd mini2
python -m python.node_server --node-id C --config config/grid_nodes.csv
python -m python.node_server --node-id E --config config/grid_nodes.csv
python -m python.node_server --node-id H --config config/grid_nodes.csv
```

If running Python from `mini2/python` directly:
```powershell
python node_server.py --node-id C --config ..\config\grid_nodes.csv
```

## Inject Jobs Through Entry Node

`A` is marked as entry in `config/grid_nodes.csv`, but the code does not hardcode this.

```powershell
cd mini2
.\build\basecamp_cpp_client.exe --config config/grid_nodes.csv --csv reference/DOB_Job_Application_Filings_20260413.csv --max-rows 200 --entry-node A
```

The client now reads each DOB CSV data row and sends it as a task (`job.csv_row`) to the entry node.

Each server consumes queued jobs by parsing `csv_row` into typed DOB task objects and stores them in memory along with index counters by borough and job type for later spread-reduce queries.

## Scatter-Gather Query System

After tasks are distributed and consumed across the grid, clients can submit queries to Node A which broadcasts them to all nodes, collects results, and aggregates them back.

### Query Format

Queries use field-operator-value syntax:
- **Match queries**: `borough=BROOKLYN`, `job_status=SIGNED_OFF`, `job_type=A1`
- **Range queries**: `job_number>100000`, `job_number<200000`, `zip>=10001`
- **Comparison operators**: `=`, `>`, `<`, `>=`, `<=`

Supported fields:
- `borough` (string, = only)
- `job_type` (string, = only)
- `job_status` (string, = only)
- `job_number` (integer, all operators)
- `doc_number` (integer, all operators)
- `zip` (integer or string, all operators)
- `owner_business_name` (string, = only)

### Execution Flow

1. Client submits query to Node A via `SubmitQuery` RPC with a filter string.
2. Node A broadcasts `BroadcastQuery` to all direct neighbors with originating_node=A.
3. Each node:
   - Executes the filter on its local consumed tasks.
   - Forwards the query to neighbors (except originating_node) to avoid loops.
   - Collects results from downstream neighbors.
   - Returns aggregated results.
4. Results bubble back up to Node A which aggregates and returns to client.

### Query Client

Submit queries using the query client:

```powershell
cd mini2
.\build\query_client.exe --config config/grid_nodes.csv --query "borough=BROOKLYN"
.\build\query_client.exe --config config/grid_nodes.csv --query "job_number>100000" --entry-node A
.\build\query_client.exe --config config/grid_nodes.csv --query "zip>=10001"
```

Results are printed with field summaries from each matching task.

## Queue Monitoring and Visualization

Each node can log its queue size at regular intervals to a CSV file for later visualization.

### Enable Queue Logging

Add `--log-file` and `--log-interval` parameters when starting nodes:

**C++ nodes**:
```powershell
.\build\basecamp_cpp_server.exe --node-id A --config config/grid_nodes.csv --log-file logs/node_A.log --log-interval 1000
.\build\basecamp_cpp_server.exe --node-id B --config config/grid_nodes.csv --log-file logs/node_B.log --log-interval 1000
```

**Python nodes**:
```powershell
python node_server.py --node-id C --config ..\config\grid_nodes.csv --log-file ..\logs\node_C.log --log-interval 1000
python node_server.py --node-id E --config ..\config\grid_nodes.csv --log-file ..\logs\node_E.log --log-interval 1000
```

Log format is CSV: `timestamp_us,node_id,queue_size` (one entry per interval).

### Visualize Queue Sizes

After running the cluster and collecting logs, visualize all 9 nodes on a single line graph:

```powershell
cd python
python visualize_queues.py --log-dir ..\logs --output queue_graph.png
```

Or view interactively:
```powershell
python visualize_queues.py --log-dir ..\logs
```

The script:
- Reads all `node_*.log` files from the specified directory
- Normalizes timestamps to relative seconds from start
- Plots queue size trends for all nodes on one graph
- Shows peak queue sizes per node
- Optionally saves to PNG file

## Multi-Computer Mapping

To run on 2 or 3 machines, only update `config/grid_nodes.csv`:
- Set each node's host IP
- Keep row/col values to preserve grid neighbors
- Start each node process on the assigned machine

No source code changes are needed.

## Notes

- Service APIs are unary synchronous gRPC RPCs only.
- Overlay edges are derived from row/col adjacency (`|dr| + |dc| == 1`).
- Data structures are typed in protobuf (`uint64`, `int32`, `bool`, `double`, `string`).
