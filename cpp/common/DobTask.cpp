#include "DobTask.hpp"

#include <stdexcept>
#include <vector>

#include "CsvLibrary.hpp"

namespace basecamp {
namespace {

int ToInt(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  return std::stoi(value);
}

std::string FieldAt(const std::vector<std::string>& fields, size_t index) {
  if (index >= fields.size()) {
    return "";
  }
  return fields[index];
}

}  // namespace

std::optional<DobTask> ParseDobTaskFromCsvRow(uint64_t job_id, uint32_t csv_line_number, const std::string& row) {
  const std::vector<std::string> fields = ParseCsvRow(row);

  // The DOB file has 100+ columns; we keep a focused typed subset for later query work.
  if (fields.size() < 82) {
    return std::nullopt;
  }

  DobTask task;
  task.job_id = job_id;
  task.csv_line_number = csv_line_number;
  task.raw_csv_row = row;

  try {
    task.job_number = ToInt(FieldAt(fields, 0));
    task.doc_number = ToInt(FieldAt(fields, 1));
  } catch (const std::exception&) {
    return std::nullopt;
  }

  task.borough = FieldAt(fields, 2);
  task.house_number = FieldAt(fields, 3);
  task.street_name = FieldAt(fields, 4);
  task.job_type = FieldAt(fields, 8);
  task.job_status = FieldAt(fields, 9);
  task.owner_business_name = FieldAt(fields, 74);
  task.zip = FieldAt(fields, 78);
  task.job_description = FieldAt(fields, 80);

  return task;
}

}  // namespace basecamp
