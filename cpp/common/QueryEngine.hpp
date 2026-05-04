#pragma once

#include <optional>
#include <string>
#include <vector>

#include "DobTask.hpp"

namespace basecamp {

enum class ComparisonOp {
  Equals,
  GreaterThan,
  LessThan,
  GreaterThanOrEqual,
  LessThanOrEqual,
};

struct QueryFilter {
  std::string field_name;
  ComparisonOp op;
  std::string value;
};

std::optional<QueryFilter> ParseQueryFilter(const std::string& filter_str);

bool EvaluateFilter(const DobTask& task, const QueryFilter& filter);

std::vector<DobTask> ExecuteQueryOnTasks(const std::vector<DobTask>& tasks, const QueryFilter& filter);

std::string FilterToString(const QueryFilter& filter);

}  // namespace basecamp
