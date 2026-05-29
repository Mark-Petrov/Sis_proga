#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "core/table.hpp"

namespace dbms {

class Database {
 public:
  explicit Database(std::string name) : name_(std::move(name)) {}

  const std::string& name() const { return name_; }

  bool create_table(const std::string& tname, std::vector<ColumnDef> cols, SharedStringPool pool,
                    std::string& err);
  bool drop_table(const std::string& tname, std::string& err);
  Table* get_table(const std::string& tname);
  const Table* get_table(const std::string& tname) const;

  const std::unordered_map<std::string, std::shared_ptr<Table>>& tables() const { return tables_; }

 private:
  std::string name_;
  std::unordered_map<std::string, std::shared_ptr<Table>> tables_;
};

}  // namespace dbms
