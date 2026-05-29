#include "sql/where_eval.hpp"

#include <regex>

#include "core/value.hpp"

namespace dbms {

static Value literal_to_value(const sql::Literal &lit, StringPool &pool) {
    if (lit.kind == sql::Literal::Null)
        return Value::make_null();
    if (lit.kind == sql::Literal::Int)
        return Value::make_int(lit.int_v);
    return Value::make_string(pool.intern(lit.str));
}

static const Value *cell_by_name(const Table &table, const Row &row,
                                 const std::string &name) {
    int idx = table.column_index(name);
    if (idx < 0)
        return nullptr;
    return &row.cells[static_cast<std::size_t>(idx)];
}

static bool cmp_values(sql::CmpOp op, const Value &a, const Value &b,
                       const StringPool &pool) {
    int c = value_cmp(a, b, pool);
    switch (op) {
    case sql::CmpOp::Eq:
        return c == 0;
    case sql::CmpOp::Ne:
        return c != 0;
    case sql::CmpOp::Lt:
        return c < 0;
    case sql::CmpOp::Gt:
        return c > 0;
    case sql::CmpOp::Le:
        return c <= 0;
    case sql::CmpOp::Ge:
        return c >= 0;
    }
    return false;
}

static bool eval_node(const sql::WhereClause &w, int idx, const Table &table,
                      const Row &row, StringPool &pool, std::string &err);

bool eval_where(const sql::WhereClause &w, int root, const Table &table,
                const Row &row, StringPool &pool, std::string &err) {
    if (root < 0)
        return true;
    return eval_node(w, root, table, row, pool, err);
}

static bool eval_node(const sql::WhereClause &w, int idx, const Table &table,
                      const Row &row, StringPool &pool, std::string &err) {
    if (idx < 0 || idx >= static_cast<int>(w.nodes.size())) {
        err = "where: bad node";
        return false;
    }
    const sql::WhereNode &n = w.nodes[static_cast<std::size_t>(idx)];
    switch (n.kind) {
    case sql::WhereNodeKind::Paren:
        return eval_node(w, n.paren_child, table, row, pool, err);
    case sql::WhereNodeKind::AndOr: {
        bool L = eval_node(w, n.bin.left_idx, table, row, pool, err);
        if (!err.empty())
            return false;
        bool R = eval_node(w, n.bin.right_idx, table, row, pool, err);
        if (!err.empty())
            return false;
        if (n.bin.kind == sql::BinBool::And)
            return L && R;
        return L || R;
    }
    case sql::WhereNodeKind::Cmp: {
        const Value *lv = cell_by_name(table, row, n.cmp.left_col.name);
        if (!lv) {
            err = "unknown column in WHERE: " + n.cmp.left_col.name;
            return false;
        }
        Value rv;
        if (n.cmp.rhs_is_col) {
            const Value *p = cell_by_name(table, row, n.cmp.right_col.name);
            if (!p) {
                err = "unknown column in WHERE: " + n.cmp.right_col.name;
                return false;
            }
            rv = *p;
        } else {
            rv = literal_to_value(n.cmp.rhs_lit, pool);
        }
        return cmp_values(n.cmp.op, *lv, rv, pool);
    }
    case sql::WhereNodeKind::Between: {
        const Value *cv = cell_by_name(table, row, n.between.col.name);
        if (!cv) {
            err = "unknown column in BETWEEN";
            return false;
        }
        Value a = literal_to_value(n.between.a, pool);
        Value b = literal_to_value(n.between.b, pool);
        return cmp_values(sql::CmpOp::Ge, *cv, a, pool) &&
               cmp_values(sql::CmpOp::Le, *cv, b, pool);
    }
    case sql::WhereNodeKind::Like: {
        const Value *cv = cell_by_name(table, row, n.like.col.name);
        if (!cv || cv->kind != Value::Kind::String) {
            err = "LIKE requires string column";
            return false;
        }
        try {
            std::regex re(n.like.pattern);
            return std::regex_match(pool.resolve(cv->str_id), re);
        } catch (const std::regex_error &) {
            err = "invalid LIKE regex";
            return false;
        }
    }
    }
    err = "where: internal";
    return false;
}

} // namespace dbms
