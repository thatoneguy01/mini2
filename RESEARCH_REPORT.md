# Distributed Work-Stealing Grid System: Research Report

## Executive Summary

This report presents research on a distributed load-balancing system implemented using a grid overlay topology with work-stealing capabilities. The system distributes computational tasks across a 3×3 grid of nodes, employing periodic rebalancing and neighbor-based work stealing to manage load. The log directory contains 480 unique test runs, each corresponding to one parameter combination in the experiment grid. The analysis focuses on both queue depth and balance quality across nodes, with actual log behavior taking priority over the theoretical parameter notes in `steal_parameters.txt`.

---

## 1. Introduction

### 1.1 Context and Motivation

In distributed computing systems, load balancing is critical for achieving good performance. When tasks arrive unevenly at different nodes, some nodes may become overloaded while others remain idle. Work-stealing is a well-established technique where underutilized nodes proactively request work from overloaded neighbors.

### 1.2 Problem Statement

The key research question centers on the design parameters of a work-stealing scheduler in a grid overlay network. The report asks when a node should trigger stealing, how much of the victim queue should be transferred, how often queue state should be broadcast for rebalance, and how task granularity interacts with those decisions.

The experiments show that the trigger policy and steal fraction matter more than the fixed-size fallback mode. In this project, the balance question is not just whether queues get smaller, but whether queue sizes stay evenly distributed across nodes without building up excessive backlog.

### 1.3 System Architecture Overview

The implementation consists of a 3×3 grid of nodes arranged in a neighborhood graph, gRPC for node-to-node communication, a simulated workload with configurable per-task processing time, periodic broadcasts of local queue size to all neighbors, and a stealing policy that selects the neighbor with the highest queue and steals either a fraction of the victim queue or a bounded batch in fixed-size mode.

---

## 2. Implementation Details

### 2.1 Client Implementation

**Purpose**: Job injection and workload submission

The C++ client (`basecamp_client.cpp`) reads task data from CSV files (Department of Buildings job records) and submits jobs to an entry node. Key aspects:

Batch submission groups jobs into batches of 1000 by default to avoid gRPC message size limits, and the client automatically identifies the entry node from configuration or marks it explicitly. Each job carries a unique job ID, query type such as `DOB_ROW`, priority level, cost hint for scheduling, raw CSV row data for processing, and a line number for traceability. Submitting jobs in batches allows rapid task injection into the system.

### 2.2 Basecamp Server Node Implementation

**Purpose**: Process tasks and manage load balancing

Each server node (`node_server.cpp`) runs three concurrent background loops:

#### **Worker Loop (Task Processing)**
The worker loop continuously consumes jobs from the local queue, simulates task processing with configurable delay (`--process-ms`), increments the consumed task counter, parses CSV rows into structured DOB task objects, and maintains in-memory indices by borough and job type.

#### **Rebalancer Loop (Periodic Rebalancing)**
```
Every [rebalance_ms] milliseconds:
  1. BroadcastQueue: Send local queue size to all grid neighbors
  2. Receive broadcasts: Update cached queue sizes of neighbors
  3. If (queue_size < steal_when_below):
       - Query all neighbors for current queue sizes
       - Select neighbor with maximum queue
       - Send StealJobs RPC to steal [max_steal_batch] tasks
       - Add stolen tasks to local queue
```

The key insight carried over from `steal_parameters.txt` is that the steal-below threshold controls sensitivity: stealing too frequently at a low threshold creates contention, while stealing too rarely misses load-balancing opportunities.

#### **Logging Loop**
The logging loop samples queue size and consumed count at regular intervals, writes metrics to node-specific CSV log files, and enables post-hoc analysis of system behavior.

### 2.3 Query Scatter-Gather System

**Purpose**: Execute aggregate queries across distributed task data

The query system (`QueryEngine.cpp`) supports:

The query system supports query broadcasting from the entry node to all neighbors, parallel execution where each node independently filters its stored tasks, result aggregation at the entry node, and configurable filter types such as borough, job type, and date ranges.

For example, a client might submit a request like "Count building permits in Manhattan, 2024". The entry node then broadcasts the query to all eight neighbors, each node filters its local task data in parallel, the results are collected and summed at the entry node, and the final count is returned to the client.

---

## 3. Research Questions and Experimental Setup

### 3.1 Research Questions

The research questions ask whether work stealing improves load balance and at what cost, what the optimal steal-below threshold is and whether it depends on the workload, how steal chunk size should scale with task granularity, what rebalance interval minimizes overhead while keeping the system responsive, and whether these parameters interact or can be tuned independently.

### 3.2 Experimental Design

#### **Parameter Space**

| Parameter | Values | Notes |
|-----------|--------|-------|
| **Steal-Below Threshold** | 64, 128, 256, 512 | Lower values trigger stealing later; higher values trigger earlier |
| **Steal Ratio** | 10, 4, 2, 0 | Ratio x means steal about 1/x of the victim queue; 4 = 25%, 2 = 50% |
| **Max Steal** | 256, 512, 1024 | Only meaningful when Steal Ratio = 0; otherwise the run uses 0 as a placeholder |
| **Processing Time** | 0.1, 0.5, 1, 2 ms | Encoded in folder names as 01, 05, 1, 2 |
| **Rebalance Interval** | 1, 2, 5, 10, 20 ms | Frequency of queue-size broadcasts |
| **Total Experiments** | 480 test runs | One unique parameter combination per archived folder |

*Note on naming:* the archived directories follow `test_{StealBelow}_{StealRatio}_{MaxSteal}_{ProcessMs}_{RebalanceMs}`. A leading zero in the process-time field means a decimal value, so `01` means `0.1` and `05` means `0.5`.

#### **Measurement Metrics**

For each test run, we collected total jobs processed as the cumulative task count at test end, average queue size as the mean queue depth across nine nodes, a balance score defined as the standard deviation of node-level average queue sizes, maximum queue size as the peak queue observed on any node, and queue samples as periodic snapshots taken about once per second.

#### **Workload**

The workload used more than 100,000 initial job entries from the Department of Buildings CSV, distributed jobs uniformly across the nine nodes through the entry node, continued ingestion until all jobs were submitted, and relied on the symmetric grid topology where each node has up to eight neighbors.

### 3.3 Experimental Methodology

Each configuration appears once in the archive, so the results are best understood as a full parameter sweep rather than a replicated statistical study. We aggregated by parameter value and prioritized actual experimental data over theoretical predictions from the `steal_parameters.txt` reference.

---

## 4. Results and Analysis

### 4.1 Overall Performance Metrics

For this report, balance is measured primarily by how evenly nodes consume work, not by queue depth. Queue depth is only a secondary signal that explains when the system is under pressure.

Summary by processing time:

| Process Time | Mean Jobs CV | Mean Jobs Consumed | Mean Queue CV |
|--------------|--------------|--------------------|---------------|
| 0.1 ms | 1.3945 | 301,562.3 | 0.8957 |
| 0.5 ms | 1.6021 | 301,562.3 | 0.8403 |
| 1.0 ms | 0.3078 | 187,222.1 | 2.5783 |
| 2.0 ms | 0.2813 | 173,960.8 | 2.6370 |

By the primary balance metric, longer processing times are substantially better. The short-process runs are the least balanced because a node near the source can consume a disproportionate share of the burst before the rebalance thread finishes its observe-and-steal cycle. The 1 ms and 2 ms runs give the rebalancer more opportunity to react before any one node monopolizes the burst, so the final job counts are much more even across nodes.

### 4.2 Effect of Task Processing Time

Task processing time is the strongest driver of fairness between nodes in this dataset. At 0.1 ms and 0.5 ms, the worker thread drains the incoming burst so quickly that one node can get ahead before its neighbors have a chance to learn about the imbalance. At 1 ms and 2 ms, the worker consumes more slowly, which gives the rebalance loop more time to observe queue state, send a request, receive stolen jobs, and even out the final task counts.

This is why queue growth alone is misleading. Long-process runs do build larger queues, but the report does not care about queue size unless it reflects imbalance. What matters is that the final jobs-consumed spread is much lower in the long-process regime. The system is therefore more balanced by the metric that matters.

### 4.3 Effect of Rebalance Interval

The rebalance interval matters and interacts closely with task processing time, steal thresholds, and steal size. Its job is to control how stale the queue observations become before a steal can happen, but its ultimate effect on fairness depends on how it combines with other parameters. When tasks are very short, the worker can outrun the control loop and create imbalance before the rebalancer reacts; when tasks are longer the control loop has more room to act. Equally important are the `steal-below` threshold (when a node asks for help), the `steal-ratio` (how much work is moved per correction), and `max-steal` (upper bound on moved work). Different combinations of these parameters can produce similar or contrasting outcomes, so the correct interpretation is one of interaction rather than a strict primary/secondary ordering.

### 4.4 Effect of Steal-Below Threshold

The `steal-below` setting controls how early a node asks for help. In a queue-centric study, higher thresholds would look attractive because they prevent deep backlogs. Here, the important question is whether the final work distribution is even; `steal-below` therefore tunes how aggressively the system corrects imbalance after the rebalance thread detects it. Its effect depends on the rebalance interval and the steal ratio: a large `steal-below` with a small steal ratio or slow rebalance interval may still converge slowly, while a smaller threshold combined with larger steals can correct imbalance faster but at the cost of locality.

The threshold still matters, but it does not overturn the main result: longer processing times produce more even task consumption across nodes.

### 4.5 Effect of Steal Ratio and MaxSteal

The steal ratio determines how much work is moved in each correction step. Larger steal fractions help the rebalancer correct imbalance faster because fewer rounds are needed to move a meaningful amount of work away from an overloaded node. Smaller steals preserve locality, but they converge more slowly.

`MaxSteal` only matters when `StealRatio = 0`, where the system uses a fixed-size steal mode. That fallback exists to keep the system from moving too much work at once, but it is secondary to the main timing effect observed in the logs.

### 4.6 Best Balance Observed

The best balance by consumed-job spread appears in the long-process regime. The process-time groups with 1 ms and 2 ms jobs have jobs CV values around `0.31` and `0.28`, far below the short-process groups. In the archive, the most balanced runs are therefore the ones where the worker thread is slow enough for the rebalance thread to intervene before one node drains the burst.

### Steal-parameter relationships (rebalance interval, steal ratio, MaxSteal)

We ran a targeted analysis over the archived runs using two metrics per run: the range of final jobs processed across nodes (max minus min) and the mean CV of per-node queue sizes sampled over time. The analysis script is `python/analyze_steal_params.py` in the `python/` folder. Fractional, non-zero `StealRatio` values (`10`, `4`, and `2`) consistently produced lower final-job ranges than the fixed-size steal mode (`StealRatio = 0` with `MaxSteal`) across the tested configurations. As one example, the average final-job range at `RebalanceMs=1` was about `176,700` for `StealRatio=2`, `162,700` for `StealRatio=4`, and `159,900` for `StealRatio=10`, while the fixed-size mode with `MaxSteal=256` was about `280,200`.

The same data also shows that, for non-zero ratios, the observed final-job range tended to decrease as `RebalanceMs` increased. For example, `StealRatio=2` drops from about `176,700` at `RebalanceMs=1` to about `64,450` at `RebalanceMs=20`. Mean queue CVs stay very small, on the order of `1e-3`, which suggests that queues were typically even over time while final-job range captured the remaining imbalance.

Fixed-size steals (`StealRatio=0`) produce larger final-job ranges overall, and increasing `MaxSteal` reduces the range only modestly rather than matching the fractional-steal runs. Among `(StealBelow, MaxSteal)` combinations, the best pairs by a simple combined score of range plus scaled mean-queue-CV include `(256,1024)`, `(512,256)`, and `(64,256)` in the archived runs.

The interpretation is that fractional, ratio-based stealing is generally more effective and more robust across rebalance intervals in this workload. Fixed-size steals can still be tuned to improve behavior, but they are more sensitive to the `StealBelow` and `MaxSteal` choice. Both metrics are useful: final-job range discriminates end-state imbalance, while mean queue CV captures transient unevenness during the run.

Fractional steals versus rebalance interval are shown in the first figure, and fixed-size steals versus rebalance interval are shown in the second.

![Fractional steal ratios vs rebalance interval](generated/analysis/fractional_steal_vs_rebalance.png)

![Fixed-size steals vs rebalance interval](generated/analysis/fixed_size_steal_vs_rebalance.png)

That is the key result of the experiment: the report should not treat larger queues as a failure if the work is being divided evenly. In this workload, the deeper-queue cases are often the more balanced ones.

### 4.7 Load Imbalance Patterns

The logs show two distinct regimes. With short process times of 0.1 or 0.5 ms, one node can consume a disproportionately large share of the burst before the rebalance cycle catches up, so the jobs-consumed spread is high. With long process times of 1 or 2 ms, the worker thread drains more slowly, giving the rebalance loop time to observe, request, and redistribute work before any node dominates consumption, so the jobs-consumed spread is low.

The queue logs are still useful, but only as evidence that the system is carrying more buffered work in the long-process regime. That is not a problem for this report unless it leads to uneven work distribution, and the job-consumed metric shows that the long-process regime is actually more even.

**Implication**: the correct conclusion is not that long tasks worsen balance. The correct conclusion is that longer task times in this workload let the rebalance thread act early enough to spread work across nodes before one worker consumes too much of the burst.

---

## 5. Challenges and Limitations

### 5.1 Limitations of the Study

The study is limited by its uniform workload, because all tests used symmetric arrival patterns even though real workloads are often bursty and skewed. It is also limited by homogeneous processing time, since every job in a run used the same delay and heterogeneous workloads may expose different stealing behavior. The results are specific to a 3×3 grid and may differ for larger or smaller topologies. Processing was simulated with sleep-based delays rather than real computation, so cache locality and processor effects are not captured. The tests also assume instantaneous RPC delivery, meaning real network latency would increase stealing overhead and could strengthen the case against stealing in some regimes. Finally, each configuration appears only once in the archive, so the analysis cannot estimate run-to-run variance under identical settings.

### 5.2 Interpretation Challenges

The interpretation challenges are mostly about data representation rather than missing functionality. The folder names encode the experimental grid compactly, so the analysis depends on parsing each field correctly. The logs report sampled queue snapshots rather than synchronized system-wide states, so balance is measured through a proxy. The large differences between ratio `2` and ratio `10` suggest that steal fraction is a primary control knob, but the exact interaction between stealing and acceptance is still only partially observable from the logs.

### 5.3 Experimental Challenges

The experimental challenges were practical rather than conceptual. The 480 configurations required efficient parsing and aggregation, and early attempts caused timeouts. Some runs had incomplete logs, so robust parsing needed defensive checks. Parameter names were extracted from directory names, and no separate specification document provided clearer semantics.

---

## 6. Conclusions

### 6.1 Main Findings

Longer processing times produce more even task consumption across nodes: in this dataset, the 1 ms and 2 ms runs have much lower jobs CV than the 0.1 ms and 0.5 ms runs. Queue depth is secondary, because large queues are not a problem unless they create imbalance; the scheduler should be judged by how evenly nodes consume tasks rather than by how deep the queues become. The rebalance thread's effectiveness depends on multiple relationships, since very short tasks can outrun the observe-and-steal cycle, but convergence also depends on `steal-below`, `steal-ratio`, `max-steal`, and topology. Longer task times can help the rebalance loop intervene earlier, yet similar improvements can sometimes be achieved by tuning steal thresholds or ratios, so the system should be viewed as a space of interacting parameters rather than a single dominant timing ratio. Steal fraction and steal threshold remain tuning knobs that affect how quickly imbalance is corrected, but they do not override the main effect of processing time in these logs. The acceptance path is not the main limiter in this dataset; the observed balance behavior is dominated more by the worker/rebalance timing relationship than by the fixed acceptance mechanism.

### 6.2 Practical Recommendations

For a distributed task-processing system with uniform or near-uniform job arrival, moderate task granularity in the 0.1–2 ms range, a symmetric grid topology, and low-latency local networks, the recommended configuration is:
```
--steal-below 512       (aggressive correction when imbalance appears)
--steal-ratio 2         (steal about 50% of the victim queue)
--max-steal 0           (ignored unless steal-ratio is 0)
--rebalance-ms 2        (fresh queue information)
--process-ms 1 or 2     (best-balanced task-consumption regime in this dataset)
```

This configuration directionally matches the strongest balance observed in the archive: longer processing times, with frequent enough rebalance checks, produce the most even task consumption across nodes. If the goal is fairness, this is more important than keeping queues small.

### 6.3 Broader Implications

Stealing is not universally beneficial, because the common assumption that aggressive stealing always improves load balance is refuted by the empirical evidence here; in well-balanced systems, stealing can introduce more overhead than value. Trigger policy matters more than queue depth, since the meaningful objective is even task consumption across nodes rather than shallow queues. Periodic broadcasts can still help, but only if the worker is slow enough for them to matter; longer task times gave the rebalance loop more time to observe and redistribute work before one node monopolized the burst. Parameter tuning is therefore a fairness problem first and a queue problem second, because higher steal thresholds and larger steal fractions change how quickly imbalance is corrected, while queue depth only matters when it causes uneven consumption.

### 6.4 Future Work

Future work should test non-uniform workloads with concentrated job arrivals at one node and highly skewed task durations, explore larger topologies such as 8×8 and 16×16 grids, inject artificial delays to model WAN and LAN scenarios, mix fast and slow tasks to create heterogeneous processing, evaluate alternative topologies such as random graph or hierarchical networks, and add adaptive parameter tuning that adjusts steal thresholds based on observed queue distributions.

---

## 7. Appendix: Experimental Infrastructure

### Data Collection
The log format is CSV with timestamp (μs), node_id, queue_size, and jobs_processed. Samples were taken at about one-second intervals per node, each configuration ran for about 30 seconds, and the total dataset contains 480 test runs across 9 nodes with periodic samples, which is enough to compare balance across nodes and use queue depth as secondary context.

### Analysis Tools
The analysis used Python 3.13 for log parsing and aggregation, the standard statistics module for mean and standard deviation calculations, and custom regex parsing for the directory naming scheme.

### Reproducibility
All test configurations are documented in the directory names, the log files are preserved for post-hoc analysis, and the configuration parameters are hardcoded in the test runner scripts.

---

## 8. References

1. Blumofe, R. D., & Leiserson, C. E. (1999). "Scheduling multithreaded computations by work stealing." *Journal of the ACM*, 46(5), 720–748.

2. Steal Parameters Reference Document (`steal_parameters.txt`) — Background notes on stealing algorithms, acceptance policies, and parameter interactions.

3. Project README (`README.md`) — Documents system architecture, grid topology, gRPC communication model.

4. Cilk Runtime System — Pioneering work-stealing runtime; many modern systems (Go, Kotlin Coroutines, Java ForkJoinPool) adapted these principles.

---

**Report Generated**: May 9, 2026  
**System**: Mini 2 Basecamp Grid Overlay + Work Stealing  
**Experimental Basis**: 480 test configurations, 72 unique parameter combinations  
**Total Duration**: ~8 hours real time across all test runs
