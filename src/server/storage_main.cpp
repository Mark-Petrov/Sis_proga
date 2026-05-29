#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "core/journal.hpp"
#include "core/string_pool.hpp"
#include "exec/executor.hpp"
#include "net/protocol.hpp"
#include "server/access_log.hpp"
#include "server/job_queue.hpp"
#include "server/telemetry.hpp"

static void handle_client(int cfd, dbms::Executor& engine, dbms::JobQueue& jobs, dbms::Telemetry& tele,
                          dbms::AccessLog& log, const std::string& handler_id) {
  std::string line;
  if (!dbms::net::read_line(cfd, line)) return;
  if (line.size() >= 7 && line.substr(0, 7) == "METRICS") {
    dbms::net::write_all(cfd, std::string("OK\t") + tele.snapshot_json() + "\n");
    return;
  }
  if (line.size() >= 5 && line.substr(0, 5) == "POLL\t") {
    std::string id = line.substr(5);
    auto jr = jobs.poll(id);
    if (!jr) {
      dbms::net::write_all(cfd, "ERR\tunknown job\n");
      return;
    }
    std::ostringstream o;
    o << "OK\t{\"status\":\"" << static_cast<int>(jr->status) << "\",\"ok\":" << (jr->result.ok ? "true" : "false")
      << ",\"message\":\"" << jr->result.message << "\",\"result\":" << jr->result.json << "}\n";
    dbms::net::write_all(cfd, o.str());
    return;
  }
  std::size_t t1 = line.find('\t');
  std::size_t t2 = line.find('\t', t1 == std::string::npos ? 0 : t1 + 1);
  if (t1 == std::string::npos || t2 == std::string::npos) {
    dbms::net::write_all(cfd, "ERR\tbad request\n");
    return;
  }
  bool async = (line.substr(0, t1) == "1");
  std::string jwt = line.substr(t1 + 1, t2 - t1 - 1);
  std::string sql = line.substr(t2 + 1);
  std::uint64_t t0 = dbms::now_us();
  std::string client = "tcp";

  if (async) {
    std::string jid = jobs.submit([&](dbms::Executor& ex) { return ex.execute(sql, client, jwt, true, false); },
                                  engine);
    std::ostringstream o;
    o << "JOB\t" << jid << "\n";
    dbms::net::write_all(cfd, o.str());
    log.log(client, handler_id, sql, t0, dbms::now_us(), 202);
    tele.record_request(false, 0);
    return;
  }

  auto tstart = std::chrono::steady_clock::now();
  dbms::ExecResult r = engine.execute(sql, client, jwt, true, false);
  auto tend = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(tend - tstart).count();
  tele.record_request(!r.ok, ms);
  log.log(client, handler_id, sql, t0, dbms::now_us(), r.ok ? 200 : 400);
  if (r.ok) {
    std::ostringstream o;
    o << "OK\t" << r.json << "\n";
    dbms::net::write_all(cfd, o.str());
  } else {
    std::ostringstream o;
    o << "ERR\t" << r.message << "\n";
    dbms::net::write_all(cfd, o.str());
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: db_storage <port> <data_dir>\n";
    return 1;
  }
  int port = std::atoi(argv[1]);
  std::string data_dir = argv[2];
  auto pool = std::make_shared<dbms::StringPool>();
  dbms::Executor engine(data_dir, pool);
  dbms::JobQueue jobs;
  dbms::Telemetry tele;
  dbms::AccessLog log(data_dir + "/access.log");

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  if (listen(fd, 64) < 0) {
    perror("listen");
    return 1;
  }
  std::cerr << "storage listening on " << port << "\n";
  int handler = 0;
  while (true) {
    int c = accept(fd, nullptr, nullptr);
    if (c < 0) continue;
    std::string hid = "h" + std::to_string(++handler);
    std::thread([c, &engine, &jobs, &tele, &log, hid]() {
      handle_client(c, engine, jobs, tele, log, hid);
      close(c);
    }).detach();
  }
}
