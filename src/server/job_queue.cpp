#include "server/job_queue.hpp"

#include <cstdio>
#include <openssl/rand.h>

#include <mutex>
#include <thread>

namespace dbms {

static std::string make_guid_v4() {
  unsigned char b[16];
  RAND_bytes(b, 16);
  b[6] = static_cast<unsigned char>((b[6] & 0x0f) | 0x40);
  b[8] = static_cast<unsigned char>((b[8] & 0x3f) | 0x80);
  char buf[40];
  std::snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2],
                b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return std::string(buf);
}

std::string JobQueue::submit(std::function<ExecResult(Executor&)> fn, Executor& ex) {
  std::string id = make_guid_v4();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    JobRecord jr;
    jr.status = JobRecord::Status::Pending;
    jobs_[id] = std::move(jr);
  }
  std::thread([this, id, fn, &ex]() {
    ExecResult r;
    try {
      r = fn(ex);
    } catch (...) {
      r.ok = false;
      r.message = "internal error";
      r.json = "{}";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return;
    it->second.status = r.ok ? JobRecord::Status::Done : JobRecord::Status::Error;
    it->second.result = std::move(r);
  }).detach();
  return id;
}

std::optional<JobRecord> JobQueue::poll(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = jobs_.find(id);
  if (it == jobs_.end()) return std::nullopt;
  return it->second;
}

}  // namespace dbms
