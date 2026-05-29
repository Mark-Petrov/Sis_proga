#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/btree.hpp"
#include "core/row.hpp"
#include "core/value.hpp"

namespace dbms {

enum class ColType { Int, String };

struct ColumnDef {
  std::string name;
  ColType type{ColType::Int};
  bool not_null{false};
  bool indexed{false};
  std::optional<Value> default_value;
  std::optional<std::string> default_str;  // before interning for STRING DEFAULT
};

class StringPool;

class Table {
 public:
  Table(std::string name, std::vector<ColumnDef> columns, SharedStringPool pool);

  const std::string& name() const { return name_; }
  const std::vector<ColumnDef>& columns() const { return cols_; }
  int column_index(const std::string& name) const;

  std::uint64_t insert_row(Row row, std::string& err);
  bool update_row(std::uint64_t rid, Row new_row, std::string& err);
  bool delete_row(std::uint64_t rid, std::string& err);

  const Row* get_row(std::uint64_t rid) const;
  Row* get_row(std::uint64_t rid);

  std::vector<std::uint64_t> all_row_ids() const;

  BPlusTreeIndex* index_for_column(int col_idx);
  const BPlusTreeIndex* index_for_column(int col_idx) const;

  void rebuild_indexes();

  SharedStringPool pool() const { return pool_; }

 private:
  std::string name_;
  std::vector<ColumnDef> cols_;
  SharedStringPool pool_;
  std::unordered_map<std::uint64_t, Row> rows_;
  std::uint64_t next_rid_{1};
  std::vector<std::unique_ptr<BPlusTreeIndex>> indexes_;  // same size as cols_, null if no index

  bool validate_row(const Row& r, std::string& err) const;
  void index_remove_row(std::uint64_t rid, const Row& old_row);
  void index_insert_row(std::uint64_t rid, const Row& new_row);
};

}  // namespace dbms
