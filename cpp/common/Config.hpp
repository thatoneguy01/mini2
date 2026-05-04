#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace basecamp {

struct NodeInfo {
  std::string node_id;
  std::string host;
  int port = 0;
  std::string language;
  int row = 0;
  int col = 0;
  bool is_entry = false;
};

class GridConfig {
 public:
  bool LoadFromCsv(const std::string& csv_path, std::string* error);

  std::optional<NodeInfo> GetNode(const std::string& node_id) const;
  std::vector<NodeInfo> AllNodes() const;
  std::vector<NodeInfo> Neighbors(const std::string& node_id) const;

 private:
  std::unordered_map<std::string, NodeInfo> nodes_;
};

std::string ToEndpoint(const NodeInfo& info);

}  // namespace basecamp
