#pragma once
#include <cstdint>
#include <string>
#include <variant>

#include "core/string_pool.hpp"

namespace dbms {

struct NullType {};

struct Value {
  enum class Kind { Null, Int, String };
  Kind kind{Kind::Null};
  std::int64_t int_v{0};
  StringPool::Id str_id{0};

  static Value make_null();
  static Value make_int(std::int64_t v);
  static Value make_string(StringPool::Id id);

  bool is_null() const { return kind == Kind::Null; }
};

bool value_eq(const Value& a, const Value& b, const StringPool& pool);
bool value_lt(const Value& a, const Value& b, const StringPool& pool);
int value_cmp(const Value& a, const Value& b, const StringPool& pool);

}  // namespace dbms
