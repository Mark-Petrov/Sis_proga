#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "core/table.hpp"

namespace dbms::sql {

struct Name {
  std::string db;
  std::string table;
  std::string full() const { return db.empty() ? table : (db + "." + table); }
};

enum class CmpOp { Eq, Ne, Lt, Gt, Le, Ge };

struct Literal {
  enum Kind { Null, Int, String } kind{Null};
  std::int64_t int_v{0};
  std::string str;  // parsed string before intern
};

struct ColRef {
  std::string name;
};

struct BetweenExpr {
  ColRef col;
  Literal a;
  Literal b;
};

struct LikeExpr {
  ColRef col;
  std::string pattern;
};

struct BinBool {
  enum Kind { And, Or } kind{And};
  int left_idx{-1};   // index into flat nodes
  int right_idx{-1};
};

struct CmpExpr {
  ColRef left_col;
  CmpOp op{CmpOp::Eq};
  bool rhs_is_col{false};
  ColRef right_col;
  Literal rhs_lit;
};

enum class WhereNodeKind { Cmp, Between, Like, AndOr, Paren };

struct WhereNode {
  WhereNodeKind kind{WhereNodeKind::Cmp};
  CmpExpr cmp;
  BetweenExpr between;
  LikeExpr like;
  BinBool bin;
  int paren_child{-1};
};

struct WhereClause {
  std::vector<WhereNode> nodes;
  int root{-1};
};

enum class AggFn { None, Sum, Count, Avg };

struct SelectItem {
  AggFn agg{AggFn::None};
  ColRef col;
  std::string alias;
};

struct StmtCreateDatabase {
  std::string name;
};
struct StmtDropDatabase {
  std::string name;
};
struct StmtUse {
  std::string name;
};
struct StmtCreateTable {
  Name table;
  std::vector<ColumnDef> columns;
};
struct StmtDropTable {
  Name table;
};
struct StmtInsert {
  Name table;
  std::vector<std::string> cols;
  std::vector<std::vector<Literal>> rows;
};
struct StmtUpdate {
  Name table;
  std::vector<std::pair<std::string, Literal>> sets;
  WhereClause where;
};
struct StmtDelete {
  Name table;
  WhereClause where;
};
struct StmtSelect {
  std::vector<SelectItem> items;
  Name table;
  WhereClause where;
};
struct StmtRevert {
  Name table_hint;
  std::string ts;  // yyyy.mm.dd-hh:mm:ss.msmsms
};

struct StmtAuthLogin {
  std::string user;
  std::string password;
};
struct StmtAuthCreateUser {
  std::string user;
  std::string password;
};
struct StmtAuthCreateGroup {
  std::string group;
};
struct StmtAuthGrant {
  std::string db;
  std::string principal;  // user:x or group:y
  std::string permission; // read, write, create_table, drop_table, drop_database
};

using Statement = std::variant<StmtCreateDatabase, StmtDropDatabase, StmtUse, StmtCreateTable,
                                 StmtDropTable, StmtInsert, StmtUpdate, StmtDelete, StmtSelect,
                                 StmtRevert, StmtAuthLogin, StmtAuthCreateUser, StmtAuthCreateGroup,
                                 StmtAuthGrant>;

}  // namespace dbms::sql
