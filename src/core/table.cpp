#include "core/table.hpp"

#include <sstream>

namespace dbms {

static BPlusTreeIndex::Cmp make_cmp(const StringPool* pool) {
  return [pool](const Value& a, const Value& b) { return value_cmp(a, b, *pool); };
}

Table::Table(std::string name, std::vector<ColumnDef> columns, SharedStringPool pool)
    : name_(std::move(name)), cols_(std::move(columns)), pool_(std::move(pool)) {
  indexes_.resize(cols_.size());
  for (std::size_t i = 0; i < cols_.size(); ++i) {
    if (cols_[i].indexed) {
      indexes_[i] = std::make_unique<BPlusTreeIndex>(make_cmp(pool_.get()), 32);
    }
  }
}

int Table::column_index(const std::string& name) const {
  for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
    if (cols_[i].name == name) return i;
  }
  return -1;
}

bool Table::validate_row(const Row& r, std::string& err) const {
  if (r.cells.size() != cols_.size()) {
    err = "row width mismatch";
    return false;
  }
  for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
    const Value& v = r.cells[i];
    if (cols_[i].not_null || cols_[i].indexed) {
      if (v.is_null()) {
        err = "NULL in NOT NULL/INDEXED column `" + cols_[i].name + "`";
        return false;
      }
    }
    if (!v.is_null()) {
      if (cols_[i].type == ColType::Int && v.kind != Value::Kind::Int) {
        err = "type mismatch for `" + cols_[i].name + "`";
        return false;
      }
      if (cols_[i].type == ColType::String && v.kind != Value::Kind::String) {
        err = "type mismatch for `" + cols_[i].name + "`";
        return false;
      }
    }
  }
  return true;
}

void Table::index_remove_row(std::uint64_t rid, const Row& old_row) {
  (void)rid;
  for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
    if (!indexes_[i]) continue;
    const Value& k = old_row.cells[i];
    if (!k.is_null()) indexes_[i]->erase(k, rid);
  }
}

void Table::index_insert_row(std::uint64_t rid, const Row& new_row) {
  for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
    if (!indexes_[i]) continue;
    const Value& k = new_row.cells[i];
    if (!k.is_null()) indexes_[i]->insert(k, rid);
  }
}

std::uint64_t Table::insert_row(Row row, std::string& err) {
  if (!validate_row(row, err)) return 0;
  for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
    if (!indexes_[i]) continue;
    const Value& k = row.cells[i];
    if (!k.is_null()) {
      auto ex = indexes_[i]->find_equal(k);
      if (!ex.empty()) {
        err = "duplicate INDEXED value for column `" + cols_[i].name + "`";
        return 0;
      }
    }
  }
  std::uint64_t rid = next_rid_++;
  rows_[rid] = std::move(row);
  index_insert_row(rid, rows_[rid]);
  return rid;
}

bool Table::update_row(std::uint64_t rid, Row new_row, std::string& err) {
  auto it = rows_.find(rid);
  if (it == rows_.end()) {
    err = "row not found";
    return false;
  }
  if (!validate_row(new_row, err)) return false;
  Row old = it->second;
  for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
    if (!indexes_[i]) continue;
    const Value& k = new_row.cells[i];
    if (!k.is_null()) {
      auto ex = indexes_[i]->find_equal(k);
      for (std::uint64_t x : ex) {
        if (x != rid) {
          err = "duplicate INDEXED value";
          return false;
        }
      }
    }
  }
  index_remove_row(rid, old);
  it->second = std::move(new_row);
  index_insert_row(rid, it->second);
  return true;
}

bool Table::delete_row(std::uint64_t rid, std::string& err) {
  auto it = rows_.find(rid);
  if (it == rows_.end()) {
    err = "row not found";
    return false;
  }
  index_remove_row(rid, it->second);
  rows_.erase(it);
  return true;
}

const Row* Table::get_row(std::uint64_t rid) const {
  auto it = rows_.find(rid);
  return it == rows_.end() ? nullptr : &it->second;
}

Row* Table::get_row(std::uint64_t rid) {
  auto it = rows_.find(rid);
  return it == rows_.end() ? nullptr : &it->second;
}

std::vector<std::uint64_t> Table::all_row_ids() const {
  std::vector<std::uint64_t> ids;
  ids.reserve(rows_.size());
  for (const auto& kv : rows_) ids.push_back(kv.first);
  return ids;
}

BPlusTreeIndex* Table::index_for_column(int col_idx) {
  if (col_idx < 0 || col_idx >= static_cast<int>(indexes_.size())) return nullptr;
  return indexes_[col_idx].get();
}

const BPlusTreeIndex* Table::index_for_column(int col_idx) const {
  if (col_idx < 0 || col_idx >= static_cast<int>(indexes_.size())) return nullptr;
  return indexes_[col_idx].get();
}

void Table::rebuild_indexes() {
  for (auto& idx : indexes_) {
    if (idx) idx->clear();
  }
  for (const auto& kv : rows_) {
    index_insert_row(kv.first, kv.second);
  }
}

}  // namespace dbms
