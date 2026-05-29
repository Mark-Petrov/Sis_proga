#pragma once
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace dbms {

class Telemetry {
 public:
  void record_request(bool error, double latency_ms);
  std::string snapshot_json();

 private:
  std::mutex mutex_;
  std::deque<std::chrono::steady_clock::time_point> hits_;
  std::deque<std::pair<std::chrono::steady_clock::time_point, double>> latencies_;
  std::deque<std::chrono::steady_clock::time_point> errors_;
};

}  // namespace dbms
