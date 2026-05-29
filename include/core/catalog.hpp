#pragma once
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/database.hpp"

namespace dbms {

class Catalog {
 public:
  Catalog() = default;

  bool create_database(const std::string& name, std::string& err);
  bool drop_database(const std::string& name, std::string& err);
  Database* get_database(const std::string& name);
  const Database* get_database(const std::string& name) const;

  void set_current(const std::string& name, std::string& err);
  Database* current();
  const Database* current() const;

  const std::unordered_map<std::string, std::unique_ptr<Database>>& databases() const {
    return dbs_;
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<Database>> dbs_;
  std::string current_;
};

}  // namespace dbms
