#pragma once
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "exec/executor.hpp"

namespace dbms {

struct JobRecord {
  enum class Status { Pending, Running, Done, Error } status{Status::Pending};
  ExecResult result;
};

class JobQueue {
 public:
  std::string submit(std::function<ExecResult(Executor&)> fn, Executor& ex);
  std::optional<JobRecord> poll(const std::string& id);

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, JobRecord> jobs_;
};

}  // namespace dbms
