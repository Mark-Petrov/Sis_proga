#include "server/access_log.hpp"

#include <fstream>

namespace dbms {

void AccessLog::log(const std::string& client_id, const std::string& handler_id,
                     const std::string& body, std::uint64_t t0_us, std::uint64_t t1_us, int code) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ofstream out(path_, std::ios::app);
  if (!out) return;
  out << t0_us << '\t' << t1_us << '\t' << client_id << '\t' << handler_id << '\t' << code << '\t'
      << body << '\n';
}

}  // namespace dbms
