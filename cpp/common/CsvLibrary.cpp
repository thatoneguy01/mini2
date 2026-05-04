#include "CsvLibrary.hpp"

namespace basecamp {
namespace {

std::string StripCarriageReturn(std::string row) {
  if (!row.empty() && row.back() == '\r') {
    row.pop_back();
  }
  return row;
}

}  // namespace

CsvReader::CsvReader(std::istream& input) : input_(input) {}

bool CsvReader::ReadRow(std::vector<std::string>* out_fields, std::string* raw_row) {
  if (out_fields == nullptr || raw_row == nullptr) {
    return false;
  }

  std::string line;
  if (!std::getline(input_, line)) {
    return false;
  }

  *raw_row = StripCarriageReturn(std::move(line));
  *out_fields = ParseCsvRow(*raw_row);
  return true;
}

std::vector<std::string> ParseCsvRow(const std::string& row) {
  std::vector<std::string> out;
  std::string current;
  bool in_quotes = false;

  for (size_t i = 0; i < row.size(); ++i) {
    const char c = row[i];
    if (c == '"') {
      if (in_quotes && i + 1 < row.size() && row[i + 1] == '"') {
        current.push_back('"');
        ++i;
      } else {
        in_quotes = !in_quotes;
      }
      continue;
    }

    if (c == ',' && !in_quotes) {
      out.push_back(current);
      current.clear();
      continue;
    }

    current.push_back(c);
  }

  out.push_back(current);
  return out;
}

}  // namespace basecamp
