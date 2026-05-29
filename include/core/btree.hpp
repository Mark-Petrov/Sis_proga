#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "core/value.hpp"

namespace dbms {

class BPlusTreeIndex {
 public:
  using RowId = std::uint64_t;
  using Cmp = std::function<int(const Value&, const Value&)>;

  explicit BPlusTreeIndex(Cmp cmp, int order = 32);
  ~BPlusTreeIndex();

  BPlusTreeIndex(const BPlusTreeIndex&) = delete;
  BPlusTreeIndex& operator=(const BPlusTreeIndex&) = delete;

  bool insert(const Value& key, RowId row);
  bool erase(const Value& key, RowId row);
  void clear();

  std::vector<RowId> find_equal(const Value& key) const;
  std::vector<RowId> range(const Value& low, const Value& high, bool low_incl,
                           bool high_incl) const;

 private:
  struct Node;
  Cmp cmp_;
  int order_;
  Node* root_{nullptr};

  static void free_tree(Node* n);
  Node* find_leaf(Node* r, const Value& k) const;
  int cmpk(const Value& a, const Value& b) const { return cmp_(a, b); }
  void insert_leaf(Node* leaf, const Value& key, RowId row);
  void insert_value(const Value& key, RowId row);
  bool erase_leaf(Node* leaf, const Value& key, RowId row);
};

}  // namespace dbms
