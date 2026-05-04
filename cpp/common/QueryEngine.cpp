#include "QueryEngine.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace basecamp {
namespace {

std::string Trim(const std::string& str) {
  auto start = str.begin();
  while (start != str.end() && std::isspace(*start)) {
    ++start;
  }
  auto end = str.end();
  do {
    --end;
  } while (std::distance(start, end) > 0 && std::isspace(*end));
  return std::string(start, end + 1);
}

bool TryParseInt(const std::string& str, int& result) {
  try {
    result = std::stoi(str);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

std::optional<QueryFilter> ParseQueryFilter(const std::string& filter_str) {
  const std::string trimmed = Trim(filter_str);

  // Try to find the operator.
  ComparisonOp op = ComparisonOp::Equals;
  size_t op_pos = std::string::npos;
  size_t op_len = 0;

  if (trimmed.find(">=") != std::string::npos) {
    op = ComparisonOp::GreaterThanOrEqual;
    op_pos = trimmed.find(">=");
    op_len = 2;
  } else if (trimmed.find("<=") != std::string::npos) {
    op = ComparisonOp::LessThanOrEqual;
    op_pos = trimmed.find("<=");
    op_len = 2;
  } else if (trimmed.find(">") != std::string::npos) {
    op = ComparisonOp::GreaterThan;
    op_pos = trimmed.find(">");
    op_len = 1;
  } else if (trimmed.find("<") != std::string::npos) {
    op = ComparisonOp::LessThan;
    op_pos = trimmed.find("<");
    op_len = 1;
  } else if (trimmed.find("=") != std::string::npos) {
    op = ComparisonOp::Equals;
    op_pos = trimmed.find("=");
    op_len = 1;
  } else {
    return std::nullopt;
  }

  if (op_pos == std::string::npos) {
    return std::nullopt;
  }

  std::string field = Trim(trimmed.substr(0, op_pos));
  std::string value = Trim(trimmed.substr(op_pos + op_len));

  if (field.empty() || value.empty()) {
    return std::nullopt;
  }

  return QueryFilter{field, op, value};
}

bool EvaluateFilter(const DobTask& task, const QueryFilter& filter) {
  const std::string& field = filter.field_name;
  const std::string& value = filter.value;
  const ComparisonOp op = filter.op;

  if (field == "borough") {
    if (op == ComparisonOp::Equals) {
      return task.borough == value;
    }
    return false;
  }

  if (field == "job_type") {
    if (op == ComparisonOp::Equals) {
      return task.job_type == value;
    }
    return false;
  }

  if (field == "job_status") {
    if (op == ComparisonOp::Equals) {
      return task.job_status == value;
    }
    return false;
  }

  if (field == "zip") {
    if (op == ComparisonOp::Equals) {
      return task.zip == value;
    }
    int int_value = 0;
    if (!TryParseInt(value, int_value)) {
      return false;
    }
    int task_zip = 0;
    if (!TryParseInt(task.zip, task_zip)) {
      return false;
    }
    if (op == ComparisonOp::GreaterThan) {
      return task_zip > int_value;
    }
    if (op == ComparisonOp::LessThan) {
      return task_zip < int_value;
    }
    if (op == ComparisonOp::GreaterThanOrEqual) {
      return task_zip >= int_value;
    }
    if (op == ComparisonOp::LessThanOrEqual) {
      return task_zip <= int_value;
    }
    return false;
  }

  if (field == "job_number") {
    int int_value = 0;
    if (!TryParseInt(value, int_value)) {
      return false;
    }
    if (op == ComparisonOp::Equals) {
      return task.job_number == int_value;
    }
    if (op == ComparisonOp::GreaterThan) {
      return task.job_number > int_value;
    }
    if (op == ComparisonOp::LessThan) {
      return task.job_number < int_value;
    }
    if (op == ComparisonOp::GreaterThanOrEqual) {
      return task.job_number >= int_value;
    }
    if (op == ComparisonOp::LessThanOrEqual) {
      return task.job_number <= int_value;
    }
    return false;
  }

  if (field == "doc_number") {
    int int_value = 0;
    if (!TryParseInt(value, int_value)) {
      return false;
    }
    if (op == ComparisonOp::Equals) {
      return task.doc_number == int_value;
    }
    if (op == ComparisonOp::GreaterThan) {
      return task.doc_number > int_value;
    }
    if (op == ComparisonOp::LessThan) {
      return task.doc_number < int_value;
    }
    if (op == ComparisonOp::GreaterThanOrEqual) {
      return task.doc_number >= int_value;
    }
    if (op == ComparisonOp::LessThanOrEqual) {
      return task.doc_number <= int_value;
    }
    return false;
  }

  return false;
}

std::vector<DobTask> ExecuteQueryOnTasks(const std::vector<DobTask>& tasks, const QueryFilter& filter) {
  std::vector<DobTask> results;
  for (const auto& task : tasks) {
    if (EvaluateFilter(task, filter)) {
      results.push_back(task);
    }
  }
  return results;
}

std::string FilterToString(const QueryFilter& filter) {
  std::string op_str;
  switch (filter.op) {
    case ComparisonOp::Equals:
      op_str = "=";
      break;
    case ComparisonOp::GreaterThan:
      op_str = ">";
      break;
    case ComparisonOp::LessThan:
      op_str = "<";
      break;
    case ComparisonOp::GreaterThanOrEqual:
      op_str = ">=";
      break;
    case ComparisonOp::LessThanOrEqual:
      op_str = "<=";
      break;
  }
  return filter.field_name + op_str + filter.value;
}

}  // namespace basecamp
