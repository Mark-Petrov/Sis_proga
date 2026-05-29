#pragma once
#include <memory>
#include <string>

#include "auth/rbac.hpp"
#include "core/catalog.hpp"
#include "core/journal.hpp"
#include "core/string_pool.hpp"
#include "sql/ast.hpp"

namespace dbms {

struct ExecResult {
  bool ok{false};
  std::string message;
  std::string json;  // payload or error detail
  bool async{false};
  std::string job_id;
};

class Executor {
 public:
  Executor(std::string data_dir, SharedStringPool pool);

  ExecResult execute(const std::string& sql, const std::string& client_id,
                     const std::string& jwt_or_empty, bool allow_mutations = true,
                     bool skip_journal = false);

  Catalog& catalog() { return catalog_; }
  const Catalog& catalog() const { return catalog_; }
  StringPool& strings() { return *pool_; }
  Rbac& rbac() { return rbac_; }

  void set_current_user_for_rbac(const std::string& user) { rbac_user_ = user; }

 private:
  std::string data_dir_;
  SharedStringPool pool_;
  Catalog catalog_;
  Rbac rbac_;
  std::string rbac_user_;

  std::string journal_path_for(const std::string& db) const;
  void maybe_journal(const std::string& sql, bool skip, bool is_mutating, const std::string& db);
  void persist_current_db();

  ExecResult exec_statement(const sql::Statement& st, bool allow_mutations, bool skip_journal);
};

}  // namespace dbms
