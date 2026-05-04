#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace basecamp {

struct DobTask {
  uint64_t job_id = 0;
  uint32_t csv_line_number = 0;

  int job_number = 0;
  int doc_number = 0;
  std::string borough;
  std::string house_number;
  std::string street_name;
  std::string job_type;
  std::string job_status;
  std::string owner_business_name;
  std::string zip;
  std::string job_description;
  std::string raw_csv_row;
};

std::optional<DobTask> ParseDobTaskFromCsvRow(uint64_t job_id, uint32_t csv_line_number, const std::string& row);

}  // namespace basecamp
