#include "exec/executor.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <variant>

#include "auth/jwt.hpp"
#include "core/persistence.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/where_eval.hpp"

namespace dbms {

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
        ++i;
    return s.substr(i);
}

static std::string json_escape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\')
            o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

static Value lit_to_val(const sql::Literal &lit, StringPool &pool) {
    if (lit.kind == sql::Literal::Null)
        return Value::make_null();
    if (lit.kind == sql::Literal::Int)
        return Value::make_int(lit.int_v);
    return Value::make_string(pool.intern(lit.str));
}

static std::string jwt_secret() {
    const char *e = std::getenv("JWT_SECRET");
    return e && e[0] ? std::string(e)
                     : std::string("course-dbms-dev-secret-change-me");
}

Executor::Executor(std::string data_dir, SharedStringPool pool)
    : data_dir_(std::move(data_dir)), pool_(std::move(pool)) {
    ensure_data_dir(data_dir_);
    std::string err;
    rbac_.create_user("admin", "admin", err);
    (void)err;
}

std::string Executor::journal_path_for(const std::string &db) const {
    return data_dir_ + "/" + db + "/journal.log";
}

void Executor::maybe_journal(const std::string &sql, bool skip,
                             bool is_mutating, const std::string &db) {
    if (skip || !is_mutating || db.empty())
        return;
    ensure_data_dir(data_dir_ + "/" + db);
    Journal::append(journal_path_for(db), now_us(), db, sql);
}

void Executor::persist_current_db() {
    // optional file dump omitted — journal provides durability path for revert
}

static bool is_mutating(const sql::Statement &st) {
    return std::visit(
        [](auto &&s) -> bool {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, sql::StmtSelect> ||
                          std::is_same_v<T, sql::StmtAuthLogin> ||
                          std::is_same_v<T, sql::StmtAuthCreateUser> ||
                          std::is_same_v<T, sql::StmtAuthCreateGroup> ||
                          std::is_same_v<T, sql::StmtAuthGrant>)
                return false;
            if constexpr (std::is_same_v<T, sql::StmtUse>)
                return false;
            return true;
        },
        st);
}

static bool needs_write_perm(const sql::Statement &st) {
    return std::visit(
        [](auto &&s) -> bool {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, sql::StmtInsert> ||
                          std::is_same_v<T, sql::StmtUpdate> ||
                          std::is_same_v<T, sql::StmtDelete>)
                return true;
            if constexpr (std::is_same_v<T, sql::StmtCreateTable> ||
                          std::is_same_v<T, sql::StmtDropTable>)
                return true;
            if constexpr (std::is_same_v<T, sql::StmtCreateDatabase> ||
                          std::is_same_v<T, sql::StmtDropDatabase>)
                return true;
            if constexpr (std::is_same_v<T, sql::StmtRevert>)
                return true;
            return false;
        },
        st);
}

static bool needs_read_perm(const sql::Statement &st) {
    return std::visit(
        [](auto &&s) -> bool {
            using T = std::decay_t<decltype(s)>;
            return std::is_same_v<T, sql::StmtSelect>;
        },
        st);
}

ExecResult Executor::execute(const std::string &sql_in,
                             const std::string &client_id,
                             const std::string &jwt_or_empty,
                             bool allow_mutations, bool skip_journal) {
    (void)client_id;
    std::string sql = trim(sql_in);
    if (!sql.empty() && sql.back() == ';')
        sql.pop_back();
    sql = trim(sql);

    JwtPayload jp;
    std::string jerr;
    if (!jwt_or_empty.empty()) {
        if (!jwt_verify(jwt_secret(), jwt_or_empty, jp, jerr))
            return ExecResult{false, "auth: " + jerr, "{}", false, ""};
        rbac_user_ = jp.sub;
    }

    sql::Lexer lex(sql + " ");
    sql::Parser parser(lex);
    sql::Statement st;
    try {
        st = parser.parse_one();
    } catch (const sql::ParseError &e) {
        return ExecResult{false, e.message, "{}", false, ""};
    }

    Database *cur = catalog_.current();
    std::string dbn = cur ? cur->name() : "";
    if (!rbac_user_.empty() && cur) {
        if (needs_read_perm(st) && !rbac_.check(rbac_user_, dbn, Perm::Read))
            return ExecResult{false, "forbidden: read", "{}", false, ""};
        if (needs_write_perm(st) &&
            !rbac_.check(rbac_user_, dbn, Perm::WriteData) && is_mutating(st))
            if (!std::holds_alternative<sql::StmtCreateTable>(st) &&
                !std::holds_alternative<sql::StmtDropTable>(st))
                if (!std::holds_alternative<sql::StmtCreateDatabase>(st) &&
                    !std::holds_alternative<sql::StmtDropDatabase>(st))
                    return ExecResult{false, "forbidden: write", "{}", false,
                                      ""};
    }

    bool mut = is_mutating(st);
    ExecResult res = exec_statement(st, allow_mutations, skip_journal);
    bool journal_it = res.ok && mut && !skip_journal &&
                      !std::holds_alternative<sql::StmtRevert>(st);
    if (journal_it) {
        std::string jdb = dbn;
        if (auto *cd = std::get_if<sql::StmtCreateDatabase>(&st))
            jdb = cd->name;
        else if (catalog_.current())
            jdb = catalog_.current()->name();
        maybe_journal(sql_in, skip_journal, mut, jdb);
    }
    return res;
}

static Table *resolve_table(Catalog &cat, const sql::Name &n,
                            std::string &err) {
    Database *db = nullptr;
    if (!n.db.empty())
        db = cat.get_database(n.db);
    else
        db = cat.current();
    if (!db) {
        err = "no current database (USE ...)";
        return nullptr;
    }
    Table *t = db->get_table(n.table);
    if (!t)
        err = "table not found: " + n.full();
    return t;
}

static bool extract_index_eq(const Table &t, const sql::WhereClause &w,
                             int root, int &colidx, Value &key,
                             StringPool &pool) {
    if (root < 0 || root >= static_cast<int>(w.nodes.size()))
        return false;
    const sql::WhereNode &n = w.nodes[static_cast<std::size_t>(root)];
    if (n.kind == sql::WhereNodeKind::Paren)
        return extract_index_eq(t, w, n.paren_child, colidx, key, pool);
    if (n.kind == sql::WhereNodeKind::AndOr &&
        n.bin.kind == sql::BinBool::And) {
        if (extract_index_eq(t, w, n.bin.left_idx, colidx, key, pool))
            return true;
        return extract_index_eq(t, w, n.bin.right_idx, colidx, key, pool);
    }
    if (n.kind == sql::WhereNodeKind::Cmp && n.cmp.op == sql::CmpOp::Eq &&
        !n.cmp.rhs_is_col) {
        int ci = t.column_index(n.cmp.left_col.name);
        if (ci >= 0 && t.columns()[static_cast<std::size_t>(ci)].indexed) {
            colidx = ci;
            key = lit_to_val(n.cmp.rhs_lit, pool);
            return true;
        }
    }
    return false;
}

static std::vector<std::uint64_t> candidate_rows(Table &t,
                                                 const sql::WhereClause &w,
                                                 int root, StringPool &pool,
                                                 std::string &err) {
    int ci = -1;
    Value key = Value::make_null();
    if (w.root >= 0 && extract_index_eq(t, w, w.root, ci, key, pool)) {
        if (auto *ix = t.index_for_column(ci)) {
            auto ids = ix->find_equal(key);
            if (!ids.empty())
                return ids;
        }
    }
    (void)err;
    return t.all_row_ids();
}

static std::string cell_to_json(const Value &v, const StringPool &pool) {
    if (v.is_null())
        return "null";
    if (v.kind == Value::Kind::Int)
        return std::to_string(v.int_v);
    return std::string("\"") + json_escape(pool.resolve(v.str_id)) + "\"";
}

ExecResult Executor::exec_statement(const sql::Statement &st,
                                    bool allow_mutations, bool skip_journal) {
    (void)skip_journal;
    return std::visit(
        [&](auto &&s) -> ExecResult {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, sql::StmtCreateDatabase>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                if (!catalog_.create_database(s.name, err))
                    return ExecResult{false, err, "{}", false, ""};
                rbac_.grant_default_on_new_database(s.name);
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtDropDatabase>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                if (!catalog_.drop_database(s.name, err))
                    return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtUse>) {
                std::string err;
                catalog_.set_current(s.name, err);
                if (!err.empty())
                    return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtCreateTable>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                Database *db = s.table.db.empty()
                                   ? catalog_.current()
                                   : catalog_.get_database(s.table.db);
                if (!db)
                    return ExecResult{false, "no database", "{}", false, ""};
                std::vector<ColumnDef> cols = s.columns;
                for (auto &c : cols) {
                    if (c.default_str) {
                        c.default_value =
                            Value::make_string(pool_->intern(*c.default_str));
                        c.default_str.reset();
                    }
                }
                if (!db->create_table(s.table.table, std::move(cols), pool_,
                                      err))
                    return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtDropTable>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                Database *db = s.table.db.empty()
                                   ? catalog_.current()
                                   : catalog_.get_database(s.table.db);
                if (!db)
                    return ExecResult{false, "no database", "{}", false, ""};
                if (!db->drop_table(s.table.table, err))
                    return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtInsert>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                Table *tab = resolve_table(catalog_, s.table, err);
                if (!tab)
                    return ExecResult{false, err, "{}", false, ""};
                for (const auto &tup : s.rows) {
                    if (tup.size() != s.cols.size())
                        return ExecResult{false, "INSERT column count mismatch",
                                          "{}", false, ""};
                    Row row;
                    row.cells.resize(tab->columns().size(), Value::make_null());
                    for (std::size_t k = 0; k < s.cols.size(); ++k) {
                        int idx = tab->column_index(s.cols[k]);
                        if (idx < 0)
                            return ExecResult{false,
                                              "unknown column " + s.cols[k],
                                              "{}", false, ""};
                        row.cells[static_cast<std::size_t>(idx)] =
                            lit_to_val(tup[k], *pool_);
                    }
                    for (int i = 0; i < static_cast<int>(tab->columns().size());
                         ++i) {
                        if (!row.cells[static_cast<std::size_t>(i)].is_null())
                            continue;
                        const ColumnDef &cd =
                            tab->columns()[static_cast<std::size_t>(i)];
                        if (cd.default_value)
                            row.cells[static_cast<std::size_t>(i)] =
                                *cd.default_value;
                    }
                    if (!tab->insert_row(std::move(row), err))
                        return ExecResult{false, err, "{}", false, ""};
                }
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtUpdate>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                Table *tab = resolve_table(catalog_, s.table, err);
                if (!tab)
                    return ExecResult{false, err, "{}", false, ""};
                auto ids =
                    candidate_rows(*tab, s.where, s.where.root, *pool_, err);
                for (std::uint64_t rid : ids) {
                    Row *rw = tab->get_row(rid);
                    if (!rw)
                        continue;
                    if (!eval_where(s.where, s.where.root, *tab, *rw, *pool_,
                                    err))
                        continue;
                    Row nr = *rw;
                    for (const auto &pr : s.sets) {
                        int ix = tab->column_index(pr.first);
                        if (ix < 0)
                            return ExecResult{false, "unknown column", "{}",
                                              false, ""};
                        nr.cells[static_cast<std::size_t>(ix)] =
                            lit_to_val(pr.second, *pool_);
                    }
                    if (!tab->update_row(rid, std::move(nr), err))
                        return ExecResult{false, err, "{}", false, ""};
                }
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtDelete>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                Table *tab = resolve_table(catalog_, s.table, err);
                if (!tab)
                    return ExecResult{false, err, "{}", false, ""};
                auto ids =
                    candidate_rows(*tab, s.where, s.where.root, *pool_, err);
                std::vector<std::uint64_t> del;
                for (std::uint64_t rid : ids) {
                    const Row *rw = tab->get_row(rid);
                    if (!rw)
                        continue;
                    if (eval_where(s.where, s.where.root, *tab, *rw, *pool_,
                                   err))
                        del.push_back(rid);
                }
                for (std::uint64_t rid : del)
                    if (!tab->delete_row(rid, err))
                        return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtSelect>) {
                std::string err;
                Table *tab = resolve_table(catalog_, s.table, err);
                if (!tab)
                    return ExecResult{false, err, "{}", false, ""};
                bool any_agg = false;
                for (const auto &it : s.items)
                    if (it.agg != sql::AggFn::None)
                        any_agg = true;
                if (any_agg) {
                    auto ids = tab->all_row_ids();
                    std::vector<const Row *> match;
                    for (std::uint64_t rid : ids) {
                        const Row *rw = tab->get_row(rid);
                        if (!rw)
                            continue;
                        if (s.where.root < 0 ||
                            eval_where(s.where, s.where.root, *tab, *rw, *pool_,
                                       err))
                            match.push_back(rw);
                    }
                    std::ostringstream js;
                    js << "[{";
                    bool firstf = true;
                    for (const auto &it : s.items) {
                        if (!firstf)
                            js << ",";
                        firstf = false;
                        std::string outn =
                            it.alias.empty() ? it.col.name : it.alias;
                        if (it.agg == sql::AggFn::Count && it.col.name == "*")
                            outn = it.alias.empty() ? "_count" : it.alias;
                        js << "\"" << json_escape(outn) << "\":";
                        if (it.agg == sql::AggFn::Count) {
                            js << match.size();
                        } else if (it.agg == sql::AggFn::Sum ||
                                   it.agg == sql::AggFn::Avg) {
                            int cix = tab->column_index(it.col.name);
                            if (cix < 0)
                                return ExecResult{false, "bad agg col", "{}",
                                                  false, ""};
                            long double sum = 0;
                            int cnt = 0;
                            for (const Row *rw : match) {
                                const Value &v =
                                    rw->cells[static_cast<std::size_t>(cix)];
                                if (v.is_null())
                                    continue;
                                if (v.kind != Value::Kind::Int)
                                    return ExecResult{false,
                                                      "SUM/AVG on non-int",
                                                      "{}", false, ""};
                                sum += static_cast<long double>(v.int_v);
                                ++cnt;
                            }
                            if (it.agg == sql::AggFn::Sum)
                                js << static_cast<long long>(sum);
                            else
                                js << (cnt ? std::to_string(
                                                 static_cast<double>(sum / cnt))
                                           : std::string("null"));
                        } else {
                            return ExecResult{false, "aggregate required", "{}",
                                              false, ""};
                        }
                    }
                    js << "}]";
                    return ExecResult{true, "ok", js.str(), false, ""};
                }
                auto ids =
                    candidate_rows(*tab, s.where, s.where.root, *pool_, err);
                std::ostringstream js;
                js << "[";
                bool first = true;
                for (std::uint64_t rid : ids) {
                    const Row *rw = tab->get_row(rid);
                    if (!rw)
                        continue;
                    if (s.where.root >= 0 &&
                        !eval_where(s.where, s.where.root, *tab, *rw, *pool_,
                                    err))
                        continue;
                    if (!first)
                        js << ",";
                    first = false;
                    js << "{";
                    bool fc = true;
                    for (const auto &it : s.items) {
                        if (it.col.name == "*") {
                            for (int c = 0;
                                 c < static_cast<int>(tab->columns().size());
                                 ++c) {
                                if (!fc)
                                    js << ",";
                                fc = false;
                                const std::string &nm =
                                    tab->columns()[static_cast<std::size_t>(c)]
                                        .name;
                                js << "\"" << json_escape(nm) << "\":"
                                   << cell_to_json(
                                          rw->cells[static_cast<std::size_t>(
                                              c)],
                                          *pool_);
                            }
                        } else {
                            int cix = tab->column_index(it.col.name);
                            if (cix < 0)
                                return ExecResult{false, "unknown column", "{}",
                                                  false, ""};
                            if (!fc)
                                js << ",";
                            fc = false;
                            std::string nm =
                                it.alias.empty() ? it.col.name : it.alias;
                            js << "\"" << json_escape(nm) << "\":"
                               << cell_to_json(
                                      rw->cells[static_cast<std::size_t>(cix)],
                                      *pool_);
                        }
                    }
                    js << "}";
                }
                js << "]";
                return ExecResult{true, "ok", js.str(), false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtRevert>) {
                std::string err;
                if (!allow_mutations)
                    return ExecResult{false, "mutations disabled", "{}", false,
                                      ""};
                Database *db = catalog_.current();
                if (!db)
                    return ExecResult{false, "USE database first", "{}", false,
                                      ""};
                std::string err2;
                std::uint64_t ts = parse_ts_string(s.ts, err2);
                if (!err2.empty())
                    return ExecResult{false, err2, "{}", false, ""};
                if (!db->get_table(s.table_hint.table))
                    return ExecResult{false, "unknown table in REVERT hint",
                                      "{}", false, ""};
                std::string path = journal_path_for(db->name());
                auto recs = Journal::read(path);
                std::sort(recs.begin(), recs.end(),
                          [](const JournalRecord &a, const JournalRecord &b) {
                              return a.ts_us < b.ts_us;
                          });
                std::vector<std::string> names;
                for (const auto &kv : db->tables())
                    names.push_back(kv.first);
                for (const std::string &tn : names) {
                    std::string e2;
                    db->drop_table(tn, e2);
                }
                for (const auto &r : recs) {
                    if (r.db != db->name())
                        continue;
                    if (r.ts_us > ts)
                        continue;
                    sql::Lexer lx(r.sql + " ");
                    sql::Parser px(lx);
                    try {
                        sql::Statement st2 = px.parse_one();
                        ExecResult er = this->exec_statement(st2, true, true);
                        if (!er.ok)
                            return er;
                    } catch (const sql::ParseError &e) {
                        return ExecResult{
                            false, std::string("revert replay: ") + e.message,
                            "{}", false, ""};
                    }
                }
                return ExecResult{true, "reverted", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtAuthCreateUser>) {
                std::string err;
                if (!rbac_.create_user(s.user, s.password, err))
                    return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtAuthCreateGroup>) {
                std::string err;
                if (!rbac_.create_group(s.group, err))
                    return ExecResult{false, err, "{}", false, ""};
                return ExecResult{true, "ok", "{}", false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtAuthLogin>) {
                if (!rbac_.verify_password(s.user, s.password))
                    return ExecResult{false, "bad credentials", "{}", false,
                                      ""};
                std::string tok = jwt_sign_hs256(jwt_secret(), s.user, 3600);
                return ExecResult{true, "ok",
                                  std::string("{\"token\":\"") +
                                      json_escape(tok) + "\"}",
                                  false, ""};
            }
            if constexpr (std::is_same_v<T, sql::StmtAuthGrant>) {
                std::string err;
                std::uint32_t m = perm_from_string(s.permission, err);
                if (!err.empty())
                    return ExecResult{false, err, "{}", false, ""};
                rbac_.grant(s.db, s.principal, m);
                return ExecResult{true, "ok", "{}", false, ""};
            }
            return ExecResult{false, "unsupported", "{}", false, ""};
        },
        st);
}

} // namespace dbms
