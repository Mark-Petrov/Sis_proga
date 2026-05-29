#include "sql/lexer.hpp"

#include <cctype>
#include <unordered_set>

namespace dbms::sql {

static std::string to_upper(std::string s) {
    for (char &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

Lexer::Lexer(std::string src) : src_(std::move(src)) {}

char Lexer::peek() const {
    if (pos_ >= src_.size())
        return '\0';
    return src_[pos_];
}

char Lexer::get() {
    if (pos_ >= src_.size())
        return '\0';
    char c = src_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

void Lexer::skip_ws() {
    while (true) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            get();
            continue;
        }
        if (c == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '-') {
            while (peek() && peek() != '\n')
                get();
            continue;
        }
        break;
    }
}

void Lexer::putback(Token t) {
    has_putback_ = true;
    putback_ = std::move(t);
}

Token Lexer::next() {
    if (has_putback_) {
        has_putback_ = false;
        return putback_;
    }
    skip_ws();
    int l = line_;
    int c = col_;
    char ch = peek();
    if (ch == '\0')
        return Token{TokKind::Eof, "", l, c};

    if (ch == '*') {
        get();
        return Token{TokKind::Star, "*", l, c};
    }
    if (ch == ',') {
        get();
        return Token{TokKind::Comma, ",", l, c};
    }
    if (ch == '(') {
        get();
        return Token{TokKind::LParen, "(", l, c};
    }
    if (ch == ')') {
        get();
        return Token{TokKind::RParen, ")", l, c};
    }
    if (ch == ';') {
        get();
        return Token{TokKind::Semicolon, ";", l, c};
    }
    if (ch == '=' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        return Token{TokKind::Eq, "==", l, c};
    }
    if (ch == '!' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        return Token{TokKind::Ne, "!=", l, c};
    }
    if (ch == '<' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        return Token{TokKind::Le, "<=", l, c};
    }
    if (ch == '>' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        return Token{TokKind::Ge, ">=", l, c};
    }
    if (ch == '<') {
        get();
        return Token{TokKind::Lt, "<", l, c};
    }
    if (ch == '>') {
        get();
        return Token{TokKind::Gt, ">", l, c};
    }
    if (ch == '=') {
        get();
        return Token{TokKind::Eq, "=", l, c};
    }
    if (ch == '"') {
        get();
        std::string s;
        while (true) {
            char x = get();
            if (x == '\0')
                return Token{TokKind::String, s, l, c};
            if (x == '"')
                break;
            s.push_back(x);
        }
        return Token{TokKind::String, s, l, c};
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
        std::size_t save = pos_;
        int sl = line_;
        int sc = col_;
        std::string buf;
        while (std::isdigit(static_cast<unsigned char>(peek())) ||
               peek() == '.' || peek() == '-' || peek() == ':') {
            buf.push_back(get());
        }
        if (buf.find('-') != std::string::npos &&
            buf.find('.') != std::string::npos) {
            return Token{TokKind::Ident, buf, sl, sc};
        }
        pos_ = save;
        line_ = sl;
        col_ = sc;
        std::string num;
        while (std::isdigit(static_cast<unsigned char>(peek())))
            num.push_back(get());
        return Token{TokKind::Number, num, sl, sc};
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
        std::string id;
        while (std::isalnum(static_cast<unsigned char>(peek())) ||
               peek() == '_' || peek() == '.')
            id.push_back(get());
        static const std::unordered_set<std::string> kws = {
            "CREATE", "DATABASE", "DROP",    "USE",     "TABLE",  "INT",
            "STRING", "NOT",      "NULL",    "INDEXED", "INSERT", "INTO",
            "VALUE",  "VALUES",   "UPDATE",  "SET",     "WHERE",  "DELETE",
            "FROM",   "SELECT",   "AS",      "BETWEEN", "AND",    "OR",
            "LIKE",   "REVERT",   "DEFAULT", "SUM",     "COUNT",  "AVG",
            "LOGIN",  "PASSWORD", "USER",    "GROUP",   "GRANT",  "TO",
            "ON"};
        std::string up = to_upper(id);
        if (kws.count(up))
            return Token{TokKind::Kw, up, l, c};
        return Token{TokKind::Ident, id, l, c};
    }

    std::string bad(1, get());
    return Token{TokKind::Eof, bad, l, c};
}

} // namespace dbms::sql
