#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dbms {

struct JournalRecord {
  std::uint64_t ts_us{0};
  std::string db;
  std::string sql;
};

class Journal {
 public:
  static void append(const std::string& path, std::uint64_t ts_us, const std::string& db,
                     const std::string& sql);
  static std::vector<JournalRecord> read(const std::string& path);
};

std::uint64_t now_us();
std::uint64_t parse_ts_string(const std::string& s, std::string& err);

}  // namespace dbms
