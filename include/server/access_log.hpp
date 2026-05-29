#pragma once
#include <fstream>
#include <mutex>
#include <string>

namespace dbms {

class AccessLog {
 public:
  explicit AccessLog(std::string path) : path_(std::move(path)) {}

  void log(const std::string& client_id, const std::string& handler_id, const std::string& body,
           std::uint64_t t0_us, std::uint64_t t1_us, int code);

 private:
  std::mutex mutex_;
  std::string path_;
};

}  // namespace dbms
