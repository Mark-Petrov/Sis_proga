#pragma once
#include <string>

#include "sql/ast.hpp"
#include "sql/lexer.hpp"

namespace dbms::sql {

struct ParseError {
  std::string message;
};

class Parser {
 public:
  explicit Parser(Lexer& lex);

  Statement parse_one();

 private:
  Lexer& lex_;
  Token cur_;

  void advance();
  bool check(TokKind k) const;
  bool check_text(const std::string& kw) const;
  void expect(TokKind k, const char* ctx);
  void expect_kw(const std::string& kw);

  Name parse_qualified_table();
  std::string parse_ident();
  Literal parse_literal();
  ColumnDef parse_column_def();

  int parse_bool_or(WhereClause& w);
  int parse_bool_and(WhereClause& w);
  int parse_bool_primary(WhereClause& w);
  int parse_predicate(WhereClause& w);
  WhereClause parse_where();

  CmpOp parse_cmp_op();

  StmtSelect parse_select();
  StmtInsert parse_insert();
  StmtUpdate parse_update();
  StmtDelete parse_delete();
};

}  // namespace dbms::sql
