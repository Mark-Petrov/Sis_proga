#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "net/protocol.hpp"

static std::vector<std::pair<std::string, int>> load_storages(const char* path) {
  std::vector<std::pair<std::string, int>> out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto p = line.find(':');
    if (p == std::string::npos) continue;
    out.push_back({line.substr(0, p), std::atoi(line.substr(p + 1).c_str())});
  }
  return out;
}

static int connect_to(const std::string& host, int port) {
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
    close(fd);
    return -1;
  }
  return fd;
}

static std::atomic<std::size_t> rr{0};

static int pick_storage(const std::vector<std::pair<std::string, int>>& nodes) {
  if (nodes.empty()) return -1;
  std::size_t i = rr++ % nodes.size();
  return static_cast<int>(i);
}

static void forward(int cfd, std::vector<std::pair<std::string, int>> nodes) {
  std::string line;
  if (!dbms::net::read_line(cfd, line)) return;
  int idx = pick_storage(nodes);
  if (idx < 0) {
    dbms::net::write_all(cfd, "ERR\tno storage\n");
    return;
  }
  int sfd = connect_to(nodes[static_cast<std::size_t>(idx)].first, nodes[static_cast<std::size_t>(idx)].second);
  if (sfd < 0) {
    dbms::net::write_all(cfd, "ERR\tstorage down\n");
    return;
  }
  dbms::net::write_all(sfd, line + "\n");
  std::string resp;
  dbms::net::read_line(sfd, resp);
  dbms::net::write_all(cfd, resp + "\n");
  close(sfd);
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: db_entry <listen_port> <storage_list_file> <heartbeat_sec>\n";
    return 1;
  }
  int port = std::atoi(argv[1]);
  auto nodes = load_storages(argv[2]);
  int hb = std::atoi(argv[3]);

  std::thread hb_thread([&]() {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(hb > 0 ? hb : 5));
      for (auto& n : nodes) {
        int t = connect_to(n.first, n.second);
        if (t >= 0) {
          dbms::net::write_all(t, std::string("METRICS\n"));
          close(t);
        } else {
          std::cerr << "heartbeat failed for " << n.first << ":" << n.second << " (restart manually)\n";
        }
      }
    }
  });
  hb_thread.detach();

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  listen(fd, 64);
  std::cerr << "entry listening " << port << "\n";
  while (true) {
    int c = accept(fd, nullptr, nullptr);
    if (c < 0) continue;
    std::thread([c, nodes]() {
      forward(c, nodes);
      close(c);
    }).detach();
  }
}
