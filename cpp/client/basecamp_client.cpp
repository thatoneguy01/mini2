#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "Config.hpp"
#include "CsvLibrary.hpp"
#include "basecamp.grpc.pb.h"

namespace {

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

}  // namespace

int main(int argc, char** argv) {
  const std::string config_path = ArgValue(argc, argv, "--config", "config/grid_nodes.csv");
  const std::string source_id = ArgValue(argc, argv, "--source", "client-1");
  const std::string csv_path = ArgValue(argc, argv, "--csv", "reference/DOB_Job_Application_Filings_20260413.csv");
  std::string entry_node = ArgValue(argc, argv, "--entry-node", "");
  const int max_rows = ArgValueInt(argc, argv, "--max-rows", 0);
  const int priority = ArgValueInt(argc, argv, "--priority", 5);

  basecamp::GridConfig config;
  std::string error;
  if (!config.LoadFromCsv(config_path, &error)) {
    std::cerr << error << std::endl;
    return 1;
  }

  if (entry_node.empty()) {
    for (const auto& node : config.AllNodes()) {
      if (node.is_entry) {
        entry_node = node.node_id;
        break;
      }
    }
  }

  if (entry_node.empty()) {
    std::cerr << "No entry node provided and no node marked is_entry=true." << std::endl;
    return 1;
  }

  const auto target = config.GetNode(entry_node);
  if (!target.has_value()) {
    std::cerr << "Entry node not found in config: " << entry_node << std::endl;
    return 1;
  }

  auto channel = grpc::CreateChannel(basecamp::ToEndpoint(*target), grpc::InsecureChannelCredentials());
  std::unique_ptr<basecamp::BasecampNode::Stub> stub = basecamp::BasecampNode::NewStub(channel);

  std::ifstream csv_stream(csv_path);
  if (!csv_stream.is_open()) {
    std::cerr << "Could not open CSV: " << csv_path << std::endl;
    return 1;
  }

  basecamp::CsvReader csv_reader(csv_stream);
  std::vector<std::string> fields;
  std::string raw_row;

  // Skip header row.
  if (!csv_reader.ReadRow(&fields, &raw_row)) {
    std::cerr << "CSV is empty: " << csv_path << std::endl;
    return 1;
  }

  uint64_t job_id_seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  uint32_t line_number = 1;
  uint64_t total_jobs_accepted = 0;
  const int batch_size = 1000;  // Submit in batches to avoid exceeding gRPC message size limit

  basecamp::SubmitJobsRequest request;
  request.set_source_id(source_id);

  while (csv_reader.ReadRow(&fields, &raw_row)) {
    ++line_number;
    auto* job = request.add_jobs();
    job->set_job_id(job_id_seed++);
    job->set_query("DOB_ROW");
    job->set_priority(priority);
    job->set_requires_gpu(false);
    job->set_cost_hint(1.0);
    job->set_csv_row(raw_row);
    job->set_csv_line_number(line_number);

    if (max_rows > 0 && total_jobs_accepted + request.jobs_size() >= max_rows) {
      break;
    }

    // Send batch when it reaches batch_size
    if (request.jobs_size() >= batch_size) {
      grpc::ClientContext submit_ctx;
      basecamp::SubmitJobsReply submit_reply;
      const grpc::Status submit_status = stub->SubmitJobs(&submit_ctx, request, &submit_reply);
      if (!submit_status.ok()) {
        std::cerr << "SubmitJobs batch failed: " << submit_status.error_message() << std::endl;
        return 1;
      }
      total_jobs_accepted += submit_reply.accepted();
      std::cout << "Submitted batch of " << submit_reply.accepted() << " jobs (total: " << total_jobs_accepted << ")" << std::endl;
      request.clear_jobs();
    }
  }

  // Send remaining jobs
  if (request.jobs_size() > 0) {
    grpc::ClientContext submit_ctx;
    basecamp::SubmitJobsReply submit_reply;
    const grpc::Status submit_status = stub->SubmitJobs(&submit_ctx, request, &submit_reply);
    if (!submit_status.ok()) {
      std::cerr << "SubmitJobs final batch failed: " << submit_status.error_message() << std::endl;
      return 1;
    }
    total_jobs_accepted += submit_reply.accepted();
    std::cout << "Submitted final batch of " << submit_reply.accepted() << " jobs (total: " << total_jobs_accepted << ")" << std::endl;
  }

  if (total_jobs_accepted == 0) {
    std::cerr << "No data rows found in CSV." << std::endl;
    return 1;
  }

  std::cout << "Submitted total " << total_jobs_accepted << " jobs to node " << entry_node << std::endl;

  grpc::ClientContext snapshot_ctx;
  basecamp::SnapshotRequest snapshot_request;
  basecamp::SnapshotReply snapshot_reply;
  const grpc::Status snapshot_status = stub->GetSnapshot(&snapshot_ctx, snapshot_request, &snapshot_reply);
  if (snapshot_status.ok()) {
    std::cout << "Entry node queue size: " << snapshot_reply.queue_size() << std::endl;
    std::cout << "Entry node consumed count: " << snapshot_reply.consumed_count() << std::endl;
  }

  return 0;
}
