#pragma once

#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace basecamp {

class CsvReader {
 public:
  explicit CsvReader(std::istream& input);

  bool ReadRow(std::vector<std::string>* out_fields, std::string* raw_row);

 private:
  std::istream& input_;
};

std::vector<std::string> ParseCsvRow(const std::string& row);

}  // namespace basecamp
