#include <iostream>
#include <memory>
#include <string>
#include <chrono>

#include <grpcpp/grpcpp.h>

#include "Config.hpp"
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

}  // namespace

int main(int argc, char** argv) {
  const std::string config_path = ArgValue(argc, argv, "--config", "config/grid_nodes.csv");
  const std::string query_filter = ArgValue(argc, argv, "--query", "");
  std::string entry_node = ArgValue(argc, argv, "--entry-node", "");

  if (query_filter.empty()) {
    std::cerr << "Missing required --query" << std::endl;
    return 1;
  }

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

  const std::string query_id = "query_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

  basecamp::QueryRequest request;
  request.set_query_id(query_id);
  request.set_query_filter(query_filter);

  grpc::ClientContext ctx;
  basecamp::QueryResponse response;
  const grpc::Status status = stub->SubmitQuery(&ctx, request, &response);

  if (!status.ok()) {
    std::cerr << "SubmitQuery failed: " << status.error_message() << std::endl;
    return 1;
  }

  std::cout << "Query ID: " << response.query_id() << std::endl;
  std::cout << "Total Results: " << response.total_results() << std::endl;
  std::cout << "---" << std::endl;

  for (int i = 0; i < response.results_size(); ++i) {
    const auto& result = response.results(i);
    std::cout << "Result " << (i + 1) << ":" << std::endl;
    std::cout << "  Job#: " << result.job_number() << std::endl;
    std::cout << "  Doc#: " << result.doc_number() << std::endl;
    std::cout << "  Borough: " << result.borough() << std::endl;
    std::cout << "  JobType: " << result.job_type() << std::endl;
    std::cout << "  Owner: " << result.owner_business_name() << std::endl;
    std::cout << "  Zip: " << result.zip() << std::endl;
    std::cout << std::endl;
  }

  return 0;
}
