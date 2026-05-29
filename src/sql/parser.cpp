#include "sql/parser.hpp"

#include <stdexcept>

namespace dbms::sql {

Parser::Parser(Lexer& lex) : lex_(lex) { advance(); }

void Parser::advance() { cur_ = lex_.next(); }

bool Parser::check(TokKind k) const { return cur_.kind == k; }

bool Parser::check_text(const std::string& kw) const {
  return cur_.kind == TokKind::Kw && cur_.text == kw;
}

void Parser::expect(TokKind k, const char* ctx) {
  if (cur_.kind != k) throw ParseError{"parse: unexpected token in " + std::string(ctx)};
  advance();
}

void Parser::expect_kw(const std::string& kw) {
  if (cur_.kind != TokKind::Kw || cur_.text != kw) throw ParseError{"expected keyword " + kw};
  advance();
}

std::string Parser::parse_ident() {
  if (cur_.kind != TokKind::Ident) throw ParseError{"expected identifier"};
  std::string s = cur_.text;
  advance();
  return s;
}

static Name split_qualified(const std::string& id) {
  auto p = id.find('.');
  if (p == std::string::npos) return Name{"", id};
  return Name{id.substr(0, p), id.substr(p + 1)};
}

Name Parser::parse_qualified_table() {
  if (cur_.kind != TokKind::Ident) throw ParseError{"expected table name"};
  Name n = split_qualified(cur_.text);
  advance();
  return n;
}

Literal Parser::parse_literal() {
  Literal L;
  if (check(TokKind::Kw) && cur_.text == "NULL") {
    advance();
    L.kind = Literal::Null;
    return L;
  }
  if (check(TokKind::Number)) {
    L.kind = Literal::Int;
    L.int_v = std::stoll(cur_.text);
    advance();
    return L;
  }
  if (check(TokKind::String)) {
    L.kind = Literal::String;
    L.str = cur_.text;
    advance();
    return L;
  }
  throw ParseError{"expected literal"};
}

ColumnDef Parser::parse_column_def() {
  ColumnDef c;
  c.name = parse_ident();
  if (check_text("INT"))
    c.type = ColType::Int;
  else if (check_text("STRING"))
    c.type = ColType::String;
  else
    throw ParseError{"expected INT or STRING"};
  advance();
  while (check(TokKind::Kw)) {
    if (cur_.text == "NOT") {
      advance();
      expect_kw("NULL");
      c.not_null = true;
    } else if (cur_.text == "INDEXED") {
      advance();
      c.indexed = true;
      c.not_null = true;
    } else if (cur_.text == "DEFAULT") {
      advance();
      Literal lit = parse_literal();
      if (lit.kind == Literal::Null) {
        c.default_value = Value::make_null();
      } else if (lit.kind == Literal::Int) {
        c.default_value = Value::make_int(lit.int_v);
      } else {
        c.default_str = lit.str;
      }
    } else
      break;
  }
  return c;
}

CmpOp Parser::parse_cmp_op() {
  if (check(TokKind::Eq)) {
    advance();
    return CmpOp::Eq;
  }
  if (check(TokKind::Ne)) {
    advance();
    return CmpOp::Ne;
  }
  if (check(TokKind::Lt)) {
    advance();
    return CmpOp::Lt;
  }
  if (check(TokKind::Gt)) {
    advance();
    return CmpOp::Gt;
  }
  if (check(TokKind::Le)) {
    advance();
    return CmpOp::Le;
  }
  if (check(TokKind::Ge)) {
    advance();
    return CmpOp::Ge;
  }
  throw ParseError{"expected comparison operator"};
}

int Parser::parse_predicate(WhereClause& w) {
  ColRef col;
  col.name = parse_ident();
  if (check_text("BETWEEN")) {
    advance();
    BetweenExpr b;
    b.col = col;
    b.a = parse_literal();
    expect_kw("AND");
    b.b = parse_literal();
    WhereNode n;
    n.kind = WhereNodeKind::Between;
    n.between = std::move(b);
    w.nodes.push_back(std::move(n));
    return static_cast<int>(w.nodes.size()) - 1;
  }
  if (check_text("LIKE")) {
    advance();
    if (!check(TokKind::String)) throw ParseError{"LIKE expects string pattern"};
    LikeExpr L;
    L.col = col;
    L.pattern = cur_.text;
    advance();
    WhereNode n;
    n.kind = WhereNodeKind::Like;
    n.like = std::move(L);
    w.nodes.push_back(std::move(n));
    return static_cast<int>(w.nodes.size()) - 1;
  }
  CmpExpr cx;
  cx.left_col = col;
  cx.op = parse_cmp_op();
  if (cur_.kind == TokKind::Ident) {
    cx.rhs_is_col = true;
    cx.right_col.name = parse_ident();
  } else {
    cx.rhs_is_col = false;
    cx.rhs_lit = parse_literal();
  }
  WhereNode n;
  n.kind = WhereNodeKind::Cmp;
  n.cmp = std::move(cx);
  w.nodes.push_back(std::move(n));
  return static_cast<int>(w.nodes.size()) - 1;
}

int Parser::parse_bool_primary(WhereClause& w) {
  if (check(TokKind::LParen)) {
    advance();
    int inner = parse_bool_or(w);
    expect(TokKind::RParen, "where paren");
    WhereNode n;
    n.kind = WhereNodeKind::Paren;
    n.paren_child = inner;
    w.nodes.push_back(std::move(n));
    return static_cast<int>(w.nodes.size()) - 1;
  }
  return parse_predicate(w);
}

int Parser::parse_bool_and(WhereClause& w) {
  int left = parse_bool_primary(w);
  while (check_text("AND")) {
    advance();
    int right = parse_bool_primary(w);
    WhereNode n;
    n.kind = WhereNodeKind::AndOr;
    n.bin.kind = BinBool::And;
    n.bin.left_idx = left;
    n.bin.right_idx = right;
    w.nodes.push_back(std::move(n));
    left = static_cast<int>(w.nodes.size()) - 1;
  }
  return left;
}

int Parser::parse_bool_or(WhereClause& w) {
  int left = parse_bool_and(w);
  while (check_text("OR")) {
    advance();
    int right = parse_bool_and(w);
    WhereNode n;
    n.kind = WhereNodeKind::AndOr;
    n.bin.kind = BinBool::Or;
    n.bin.left_idx = left;
    n.bin.right_idx = right;
    w.nodes.push_back(std::move(n));
    left = static_cast<int>(w.nodes.size()) - 1;
  }
  return left;
}

WhereClause Parser::parse_where() {
  WhereClause w;
  if (!check_text("WHERE")) return w;
  advance();
  w.root = parse_bool_or(w);
  return w;
}

StmtSelect Parser::parse_select() {
  StmtSelect s;
  if (check(TokKind::Star)) {
    advance();
    SelectItem it;
    it.agg = AggFn::None;
    it.col.name = "*";
    s.items.push_back(it);
  } else {
    while (true) {
      SelectItem it;
      it.agg = AggFn::None;
      if (check_text("SUM")) {
        advance();
        expect(TokKind::LParen, "sum");
        it.agg = AggFn::Sum;
        it.col.name = parse_ident();
        expect(TokKind::RParen, "sum");
      } else if (check_text("COUNT")) {
        advance();
        expect(TokKind::LParen, "count");
        if (check(TokKind::Star)) {
          advance();
          it.col.name = "*";
        } else
          it.col.name = parse_ident();
        it.agg = AggFn::Count;
        expect(TokKind::RParen, "count");
      } else if (check_text("AVG")) {
        advance();
        expect(TokKind::LParen, "avg");
        it.agg = AggFn::Avg;
        it.col.name = parse_ident();
        expect(TokKind::RParen, "avg");
      } else {
        it.col.name = parse_ident();
      }
      if (check_text("AS")) {
        advance();
        it.alias = parse_ident();
      }
      s.items.push_back(std::move(it));
      if (!check(TokKind::Comma)) break;
      advance();
    }
  }
  expect_kw("FROM");
  s.table = parse_qualified_table();
  s.where = parse_where();
  return s;
}

StmtInsert Parser::parse_insert() {
  StmtInsert s;
  expect_kw("INTO");
  s.table = parse_qualified_table();
  expect(TokKind::LParen, "insert");
  while (true) {
    s.cols.push_back(parse_ident());
    if (!check(TokKind::Comma)) break;
    advance();
  }
  expect(TokKind::RParen, "insert");
  if (check_text("VALUE"))
    advance();
  else if (check_text("VALUES"))
    advance();
  else
    throw ParseError{"VALUE or VALUES expected"};
  while (true) {
    expect(TokKind::LParen, "values");
    std::vector<Literal> one;
    while (true) {
      one.push_back(parse_literal());
      if (!check(TokKind::Comma)) break;
      advance();
    }
    expect(TokKind::RParen, "values");
    s.rows.push_back(std::move(one));
    if (!check(TokKind::Comma)) break;
    advance();
  }
  return s;
}

StmtUpdate Parser::parse_update() {
  StmtUpdate s;
  s.table = parse_qualified_table();
  expect_kw("SET");
  while (true) {
    std::string c = parse_ident();
    expect(TokKind::Eq, "set");
    Literal lit = parse_literal();
    s.sets.push_back({c, lit});
    if (!check(TokKind::Comma)) break;
    advance();
  }
  s.where = parse_where();
  return s;
}

StmtDelete Parser::parse_delete() {
  StmtDelete s;
  expect_kw("FROM");
  s.table = parse_qualified_table();
  s.where = parse_where();
  return s;
}

Statement Parser::parse_one() {
  if (cur_.kind != TokKind::Kw) throw ParseError{"statement must start with keyword"};
  std::string k = cur_.text;
  advance();
  if (k == "CREATE") {
    if (check_text("DATABASE")) {
      advance();
      StmtCreateDatabase d;
      d.name = parse_ident();
      return d;
    }
    if (check_text("TABLE")) {
      advance();
      StmtCreateTable t;
      t.table = parse_qualified_table();
      expect(TokKind::LParen, "create table");
      while (true) {
        t.columns.push_back(parse_column_def());
        if (!check(TokKind::Comma)) break;
        advance();
      }
      expect(TokKind::RParen, "create table");
      return t;
    }
    if (check_text("USER")) {
      advance();
      StmtAuthCreateUser u;
      u.user = parse_ident();
      expect_kw("PASSWORD");
      Literal lit = parse_literal();
      if (lit.kind != Literal::String) throw ParseError{"password must be string"};
      u.password = lit.str;
      return u;
    }
    if (check_text("GROUP")) {
      advance();
      StmtAuthCreateGroup g;
      g.group = parse_ident();
      return g;
    }
    throw ParseError{"CREATE what?"};
  }
  if (k == "DROP") {
    if (check_text("DATABASE")) {
      advance();
      StmtDropDatabase d;
      d.name = parse_ident();
      return d;
    }
    if (check_text("TABLE")) {
      advance();
      StmtDropTable t;
      t.table = parse_qualified_table();
      return t;
    }
    throw ParseError{"DROP what?"};
  }
  if (k == "USE") {
    StmtUse u;
    u.name = parse_ident();
    return u;
  }
  if (k == "INSERT") {
    return parse_insert();
  }
  if (k == "UPDATE") {
    return parse_update();
  }
  if (k == "DELETE") {
    return parse_delete();
  }
  if (k == "SELECT") {
    return parse_select();
  }
  if (k == "REVERT") {
    StmtRevert r;
    r.table_hint = parse_qualified_table();
    if (cur_.kind != TokKind::Ident) throw ParseError{"expected timestamp"};
    r.ts = cur_.text;
    advance();
    return r;
  }
  if (k == "LOGIN") {
    StmtAuthLogin L;
    expect_kw("USER");
    L.user = parse_ident();
    expect_kw("PASSWORD");
    Literal lit = parse_literal();
    if (lit.kind != Literal::String) throw ParseError{"password must be string"};
    L.password = lit.str;
    return L;
  }
  if (k == "GRANT") {
    StmtAuthGrant g;
    g.permission = parse_ident();
    expect_kw("ON");
    g.db = parse_ident();
    expect_kw("TO");
    expect_kw("USER");
    g.principal = "user:" + parse_ident();
    return g;
  }
  throw ParseError{"unknown statement: " + k};
}

}  // namespace dbms::sql
