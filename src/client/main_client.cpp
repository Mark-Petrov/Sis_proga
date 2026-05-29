#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "net/protocol.hpp"

static std::string env_or(const char* k, const char* d) {
  const char* v = std::getenv(k);
  return v && v[0] ? std::string(v) : std::string(d);
}

static int connect_host(const std::string& host, int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
    close(fd);
    return -1;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0) {
    perror("connect");
    close(fd);
    return -1;
  }
  return fd;
}

static std::string trim_sql(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
  return s.substr(i);
}

static bool is_comment_only(const std::string& s) {
  std::istringstream is(s);
  std::string line;
  bool any = false;
  while (std::getline(is, line)) {
    std::string t = trim_sql(line);
    if (t.empty()) continue;
    any = true;
    if (t.size() >= 2 && t[0] == '-' && t[1] == '-') continue;
    return false;
  }
  return any == false;
}

static std::vector<std::string> split_sql_statements(const std::string& src) {
  std::vector<std::string> out;
  std::string cur;
  bool in_str = false;
  for (char c : src) {
    if (c == '"') {
      in_str = !in_str;
      cur.push_back(c);
      continue;
    }
    if (c == ';' && !in_str) {
      std::string one = trim_sql(cur);
      if (!one.empty() && !is_comment_only(one)) out.push_back(one);
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  std::string tail = trim_sql(cur);
  if (!tail.empty() && !is_comment_only(tail)) out.push_back(tail);
  return out;
}

static std::string flatten_sql(std::string s) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }
  return trim_sql(s);
}

static std::string send_sql(int fd, bool async, const std::string& jwt, const std::string& sql) {
  std::string line = (async ? "1" : "0");
  line.push_back('\t');
  line += jwt;
  line.push_back('\t');
  line += flatten_sql(sql);
  line.push_back('\n');
  if (!dbms::net::write_all(fd, line)) return "ERR\tio\n";
  std::string resp;
  if (!dbms::net::read_line(fd, resp)) return "ERR\tio\n";
  return resp;
}

static void run_interactive() {
  std::string host = env_or("STORAGE_HOST", "127.0.0.1");
  int port = std::atoi(env_or("STORAGE_PORT", "4100").c_str());
  std::cout << "target " << host << ":" << port << " (set STORAGE_HOST/STORAGE_PORT)\n";
  std::string buf;
  std::string line;
  std::string jwt = env_or("JWT", "");
  while (std::cout << "sql> " << std::flush && std::getline(std::cin, line)) {
    buf += line;
    buf.push_back('\n');
    if (buf.find(';') == std::string::npos) continue;
    std::string sql = buf;
    buf.clear();
    while (!sql.empty() && (sql.back() == '\n' || sql.back() == '\r' || sql.back() == ' '))
      sql.pop_back();
    int fd = connect_host(host, port);
    if (fd < 0) {
      std::cout << "ERR\tconnect (is db_storage running?)\n";
      continue;
    }
    std::string r = send_sql(fd, false, jwt, sql);
    close(fd);
    if (r.rfind("OK\t", 0) == 0)
      std::cout << r.substr(3) << "\n";
    else
      std::cout << r << "\n";
  }
}

static void run_batch(const char* path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open " << path << "\n";
    return;
  }
  std::string host = env_or("STORAGE_HOST", "127.0.0.1");
  int port = std::atoi(env_or("STORAGE_PORT", "4100").c_str());
  std::string file;
  std::string line;
  while (std::getline(in, line)) {
    file += line;
    file.push_back('\n');
  }
  std::string jwt = env_or("JWT", "");
  for (const std::string& stmt : split_sql_statements(file)) {
    int sfd = connect_host(host, port);
    if (sfd < 0) {
      std::cout << "ERR\tconnect\n";
      continue;
    }
    std::string r = send_sql(sfd, false, jwt, stmt + ";");
    std::cout << r << "\n";
    close(sfd);
  }
}

int main(int argc, char** argv) {
  if (argc >= 2) {
    run_batch(argv[1]);
    return 0;
  }
  run_interactive();
  return 0;
}
