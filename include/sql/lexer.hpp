#pragma once
#include <string>
#include <vector>

namespace dbms::sql {

enum class TokKind {
  Ident,
  Number,
  String,
  Star,
  Comma,
  LParen,
  RParen,
  Semicolon,
  Eq,
  Ne,
  Lt,
  Gt,
  Le,
  Ge,
  Eof,
  Kw
};

struct Token {
  TokKind kind{TokKind::Eof};
  std::string text;
  int line{1};
  int col{1};
};

class Lexer {
 public:
  explicit Lexer(std::string src);

  Token next();
  void putback(Token t);

 private:
  std::string src_;
  std::size_t pos_{0};
  int line_{1};
  int col_{1};
  bool has_putback_{false};
  Token putback_;

  char peek() const;
  char get();
  void skip_ws();
  bool match_kw(const std::string& upper, std::string_view ident) const;
};

}  // namespace dbms::sql
