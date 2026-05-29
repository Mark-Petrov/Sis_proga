#pragma once
#include <string>

namespace dbms::net {

bool read_line(int fd, std::string& out);
bool write_all(int fd, const std::string& data);

}  // namespace dbms::net
