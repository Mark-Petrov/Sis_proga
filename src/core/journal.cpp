#include "core/journal.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

namespace dbms {

std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
}

static std::string sanitize_line(std::string s) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '|') c = ' ';
  }
  return s;
}

void Journal::append(const std::string& path, std::uint64_t ts_us, const std::string& db,
                     const std::string& sql) {
  std::ofstream out(path, std::ios::app);
  if (!out) return;
  out << ts_us << '|' << sanitize_line(db) << '|' << sanitize_line(sql) << '\n';
}

static std::string read_file_all(const std::string& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<JournalRecord> Journal::read(const std::string& path) {
  std::vector<JournalRecord> recs;
  std::string data = read_file_all(path);
  std::size_t pos = 0;
  while (pos < data.size()) {
    std::size_t nl = data.find('\n', pos);
    std::string line = nl == std::string::npos ? data.substr(pos) : data.substr(pos, nl - pos);
    pos = nl == std::string::npos ? data.size() : nl + 1;
    if (line.empty()) continue;
    std::size_t p1 = line.find('|');
    std::size_t p2 = line.find('|', p1 == std::string::npos ? 0 : p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) continue;
    JournalRecord r;
    try {
      r.ts_us = std::stoull(line.substr(0, p1));
    } catch (...) {
      continue;
    }
    r.db = line.substr(p1 + 1, p2 - p1 - 1);
    r.sql = line.substr(p2 + 1);
    recs.push_back(std::move(r));
  }
  return recs;
}

std::uint64_t parse_ts_string(const std::string& s, std::string& err) {
  int y, mo, d, h, mi, se, ms = 0;
  int n = std::sscanf(s.c_str(), "%d.%d.%d-%d:%d:%d.%d", &y, &mo, &d, &h, &mi, &se, &ms);
  if (n < 6) {
    err = "bad timestamp format";
    return 0;
  }
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = se;
  std::time_t tt = 0;
#if defined(_WIN32)
  tt = _mkgmtime(&tm);
#else
  tt = timegm(&tm);
#endif
  if (tt == static_cast<std::time_t>(-1)) {
    err = "timestamp out of range";
    return 0;
  }
  std::uint64_t base = static_cast<std::uint64_t>(tt) * 1000000ULL;
  if (n >= 7) base += static_cast<std::uint64_t>(ms % 1000000);
  return base;
}

}  // namespace dbms
