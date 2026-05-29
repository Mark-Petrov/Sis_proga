#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/row.hpp"
#include "core/table.hpp"
#include "sql/ast.hpp"

namespace dbms {

class StringPool;

bool eval_where(const sql::WhereClause& w, int root, const Table& table, const Row& row,
                StringPool& pool, std::string& err);

}  // namespace dbms
