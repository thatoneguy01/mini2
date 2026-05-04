#include "Config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace basecamp {
namespace {

std::string Trim(std::string value) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](unsigned char c) { return !is_space(c); }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](unsigned char c) { return !is_space(c); }).base(), value.end());
  return value;
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream ss(line);
  std::string item;
  while (std::getline(ss, item, ',')) {
    fields.push_back(Trim(item));
  }
  return fields;
}

bool ParseBool(const std::string& value) {
  std::string lower;
  lower.reserve(value.size());
  for (char c : value) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return lower == "1" || lower == "true" || lower == "yes";
}

}  // namespace

bool GridConfig::LoadFromCsv(const std::string& csv_path, std::string* error) {
  nodes_.clear();

  std::ifstream input(csv_path);
  if (!input.is_open()) {
    if (error != nullptr) {
      *error = "Could not open config file: " + csv_path;
    }
    return false;
  }

  std::string line;
  if (!std::getline(input, line)) {
    if (error != nullptr) {
      *error = "CSV file is empty: " + csv_path;
    }
    return false;
  }

  while (std::getline(input, line)) {
    if (Trim(line).empty()) {
      continue;
    }

    const auto fields = SplitCsvLine(line);
    if (fields.size() < 7) {
      if (error != nullptr) {
        *error = "Expected 7 columns in CSV row: " + line;
      }
      return false;
    }

    NodeInfo info;
    info.node_id = fields[0];
    info.host = fields[1];
    info.port = std::stoi(fields[2]);
    info.language = fields[3];
    info.row = std::stoi(fields[4]);
    info.col = std::stoi(fields[5]);
    info.is_entry = ParseBool(fields[6]);

    if (info.node_id.empty()) {
      if (error != nullptr) {
        *error = "node_id cannot be empty.";
      }
      return false;
    }

    nodes_[info.node_id] = info;
  }

  if (nodes_.empty()) {
    if (error != nullptr) {
      *error = "No nodes were loaded from CSV.";
    }
    return false;
  }

  return true;
}

std::optional<NodeInfo> GridConfig::GetNode(const std::string& node_id) const {
  const auto iter = nodes_.find(node_id);
  if (iter == nodes_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

std::vector<NodeInfo> GridConfig::AllNodes() const {
  std::vector<NodeInfo> nodes;
  nodes.reserve(nodes_.size());
  for (const auto& [_, info] : nodes_) {
    nodes.push_back(info);
  }
  return nodes;
}

std::vector<NodeInfo> GridConfig::Neighbors(const std::string& node_id) const {
  std::vector<NodeInfo> neighbors;
  const auto me = GetNode(node_id);
  if (!me.has_value()) {
    return neighbors;
  }

  for (const auto& [_, candidate] : nodes_) {
    if (candidate.node_id == me->node_id) {
      continue;
    }
    const int distance = std::abs(candidate.row - me->row) + std::abs(candidate.col - me->col);
    if (distance == 1) {
      neighbors.push_back(candidate);
    }
  }

  return neighbors;
}

std::string ToEndpoint(const NodeInfo& info) {
  return info.host + ":" + std::to_string(info.port);
}

}  // namespace basecamp
