#include "core/value.hpp"

namespace dbms {

Value Value::make_null() {
    Value v;
    v.kind = Kind::Null;
    return v;
}

Value Value::make_int(std::int64_t v) {
    Value x;
    x.kind = Kind::Int;
    x.int_v = v;
    return x;
}

Value Value::make_string(StringPool::Id id) {
    Value x;
    x.kind = Kind::String;
    x.str_id = id;
    return x;
}

bool value_eq(const Value &a, const Value &b, const StringPool &pool) {
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case Value::Kind::Null:
        return true;
    case Value::Kind::Int:
        return a.int_v == b.int_v;
    case Value::Kind::String:
        return a.str_id == b.str_id;
    }
    return false;
}

bool value_lt(const Value &a, const Value &b, const StringPool &pool) {
    return value_cmp(a, b, pool) < 0;
}

int value_cmp(const Value &a, const Value &b, const StringPool &pool) {
    if (a.kind != b.kind) {
        return static_cast<int>(a.kind) < static_cast<int>(b.kind) ? -1 : 1;
    }
    switch (a.kind) {
    case Value::Kind::Null:
        return 0;
    case Value::Kind::Int:
        if (a.int_v < b.int_v)
            return -1;
        if (a.int_v > b.int_v)
            return 1;
        return 0;
    case Value::Kind::String: {
        const std::string &sa = pool.resolve(a.str_id);
        const std::string &sb = pool.resolve(b.str_id);
        if (sa < sb)
            return -1;
        if (sa > sb)
            return 1;
        return 0;
    }
    }
    return 0;
}

} // namespace dbms
