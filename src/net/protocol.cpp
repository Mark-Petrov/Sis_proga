#include "net/protocol.hpp"

#include <cerrno>
#include <unistd.h>

namespace dbms::net {

bool read_line(int fd, std::string& out) {
  out.clear();
  char c;
  while (true) {
    ssize_t n = ::read(fd, &c, 1);
    if (n <= 0) return false;
    if (c == '\n') return true;
    out.push_back(c);
  }
}

bool write_all(int fd, const std::string& data) {
  const char* p = data.data();
  std::size_t left = data.size();
  while (left > 0) {
    ssize_t n = ::write(fd, p, left);
    if (n <= 0) return false;
    p += n;
    left -= static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace dbms::net
