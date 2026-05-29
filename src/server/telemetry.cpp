#include "server/telemetry.hpp"

#include <chrono>
#include <sstream>

namespace dbms {

static void prune_old(std::deque<std::chrono::steady_clock::time_point>& d,
                       std::chrono::steady_clock::time_point now,
                       std::chrono::seconds window) {
  while (!d.empty() && now - d.front() > window) d.pop_front();
}

void Telemetry::record_request(bool error, double latency_ms) {
  using clock = std::chrono::steady_clock;
  auto now = clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  hits_.push_back(now);
  latencies_.push_back({now, latency_ms});
  if (error) errors_.push_back(now);
  prune_old(hits_, now, std::chrono::seconds(600));
  while (!latencies_.empty() && now - latencies_.front().first > std::chrono::seconds(10))
    latencies_.pop_front();
  prune_old(errors_, now, std::chrono::seconds(60));
}

std::string Telemetry::snapshot_json() {
  using clock = std::chrono::steady_clock;
  auto now = clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  auto hits_copy = hits_;
  auto lat_copy = latencies_;
  auto err_copy = errors_;
  (void)now;
  double rps_now = 0;
  if (!hits_copy.empty()) {
    auto t0 = hits_copy.back();
    std::size_t c = 0;
    for (auto it = hits_copy.rbegin(); it != hits_copy.rend(); ++it) {
      if (t0 - *it > std::chrono::seconds(1)) break;
      ++c;
    }
    rps_now = static_cast<double>(c);
  }
  double sum = 0;
  for (const auto& p : lat_copy) sum += p.second;
  double avg_lat = lat_copy.empty() ? 0 : sum / lat_copy.size();
  std::ostringstream o;
  o << "{\"rps_now\":" << rps_now << ",\"avg_latency_ms_10s\":" << avg_lat
    << ",\"errors_last_min\":" << err_copy.size() << "}";
  return o.str();
}

}  // namespace dbms
