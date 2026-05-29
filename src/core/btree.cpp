#include "core/btree.hpp"

#include <algorithm>

namespace dbms {

struct BPlusTreeIndex::Node {
  bool leaf{true};
  Node* parent{nullptr};
  std::vector<Value> keys;
  std::vector<RowId> rows;
  std::vector<Node*> ch;
  Node* next{nullptr};
};

BPlusTreeIndex::BPlusTreeIndex(Cmp cmp, int order) : cmp_(std::move(cmp)), order_(std::max(4, order)) {}

BPlusTreeIndex::~BPlusTreeIndex() { free_tree(root_); }

void BPlusTreeIndex::free_tree(Node* n) {
  if (!n) return;
  if (!n->leaf) {
    for (Node* c : n->ch) free_tree(c);
  }
  delete n;
}

void BPlusTreeIndex::clear() {
  free_tree(root_);
  root_ = nullptr;
}

BPlusTreeIndex::Node* BPlusTreeIndex::find_leaf(Node* r, const Value& k) const {
  Node* cur = r;
  while (cur && !cur->leaf) {
    int i = 0;
    while (i < static_cast<int>(cur->keys.size()) && cmpk(cur->keys[i], k) <= 0) ++i;
    cur = cur->ch[static_cast<std::size_t>(i)];
  }
  return cur;
}

void BPlusTreeIndex::insert_leaf(Node* leaf, const Value& key, RowId row) {
  int pos = 0;
  while (pos < static_cast<int>(leaf->keys.size()) && cmpk(leaf->keys[pos], key) < 0) ++pos;
  leaf->keys.insert(leaf->keys.begin() + pos, key);
  leaf->rows.insert(leaf->rows.begin() + pos, row);
}

void BPlusTreeIndex::insert_value(const Value& key, RowId row) {
  const int MAXK = order_;
  if (!root_) {
    root_ = new Node{};
    root_->leaf = true;
    root_->keys.push_back(key);
    root_->rows.push_back(row);
    return;
  }
  Node* leaf = find_leaf(root_, key);
  insert_leaf(leaf, key, row);

  auto insert_sep = [&](BPlusTreeIndex::Node* parent, const Value& sep, BPlusTreeIndex::Node* left,
                        BPlusTreeIndex::Node* right) {
    int idx = 0;
    while (idx < static_cast<int>(parent->ch.size()) && parent->ch[idx] != left) ++idx;
    parent->keys.insert(parent->keys.begin() + idx, sep);
    parent->ch.insert(parent->ch.begin() + idx + 1, right);
    right->parent = parent;
  };

  Node* cur = leaf;
  while (cur && static_cast<int>(cur->keys.size()) > MAXK) {
    if (cur->leaf) {
      Node* right = new Node{};
      right->leaf = true;
      int mid = static_cast<int>(cur->keys.size()) / 2;
      right->keys.assign(cur->keys.begin() + mid, cur->keys.end());
      right->rows.assign(cur->rows.begin() + mid, cur->rows.end());
      cur->keys.resize(static_cast<std::size_t>(mid));
      cur->rows.resize(static_cast<std::size_t>(mid));
      Value sep = right->keys.front();
      right->next = cur->next;
      cur->next = right;
      if (cur->parent == nullptr) {
        Node* nr = new Node{};
        nr->leaf = false;
        nr->ch.push_back(cur);
        nr->ch.push_back(right);
        nr->keys.push_back(sep);
        cur->parent = nr;
        right->parent = nr;
        root_ = nr;
        cur = nr;
        continue;
      }
      insert_sep(cur->parent, sep, cur, right);
      cur = cur->parent;
      continue;
    }
    // internal split
    Node* right = new Node{};
    right->leaf = false;
    int mid = static_cast<int>(cur->keys.size()) / 2;
    Value up = cur->keys[static_cast<std::size_t>(mid)];
    right->keys.assign(cur->keys.begin() + mid + 1, cur->keys.end());
    right->ch.assign(cur->ch.begin() + mid + 1, cur->ch.end());
    cur->keys.resize(static_cast<std::size_t>(mid));
    cur->ch.resize(static_cast<std::size_t>(mid + 1));
    for (Node* c : cur->ch) c->parent = cur;
    for (Node* c : right->ch) c->parent = right;
    if (cur->parent == nullptr) {
      Node* nr = new Node{};
      nr->leaf = false;
      nr->keys.push_back(up);
      nr->ch.push_back(cur);
      nr->ch.push_back(right);
      cur->parent = nr;
      right->parent = nr;
      root_ = nr;
      cur = nr;
      continue;
    }
    int pidx = 0;
    while (pidx < static_cast<int>(cur->parent->ch.size()) && cur->parent->ch[pidx] != cur) ++pidx;
    cur->parent->keys.insert(cur->parent->keys.begin() + pidx, up);
    cur->parent->ch.insert(cur->parent->ch.begin() + pidx + 1, right);
    right->parent = cur->parent;
    cur = cur->parent;
  }
}

bool BPlusTreeIndex::insert(const Value& key, RowId row) {
  insert_value(key, row);
  return true;
}

bool BPlusTreeIndex::erase_leaf(Node* leaf, const Value& key, RowId row) {
  for (int i = 0; i < static_cast<int>(leaf->keys.size()); ++i) {
    if (cmpk(leaf->keys[i], key) == 0 && leaf->rows[i] == row) {
      leaf->keys.erase(leaf->keys.begin() + i);
      leaf->rows.erase(leaf->rows.begin() + i);
      return true;
    }
  }
  return false;
}

bool BPlusTreeIndex::erase(const Value& key, RowId row) {
  if (!root_) return false;
  Node* leaf = find_leaf(root_, key);
  for (Node* l = leaf; l; l = l->next) {
    if (erase_leaf(l, key, row)) return true;
  }
  return false;
}

std::vector<BPlusTreeIndex::RowId> BPlusTreeIndex::find_equal(const Value& key) const {
  std::vector<RowId> out;
  if (!root_) return out;
  Node* leaf = find_leaf(root_, key);
  for (Node* l = leaf; l; l = l->next) {
    for (int i = 0; i < static_cast<int>(l->keys.size()); ++i) {
      int c = cmpk(l->keys[i], key);
      if (c == 0) out.push_back(l->rows[i]);
      if (c > 0) return out;
    }
  }
  return out;
}

std::vector<BPlusTreeIndex::RowId> BPlusTreeIndex::range(const Value& low, const Value& high,
                                                         bool low_incl, bool high_incl) const {
  std::vector<RowId> out;
  if (!root_) return out;
  Node* leaf = find_leaf(root_, low);
  auto ok = [&](const Value& k) {
    int a = cmpk(k, low);
    int b = cmpk(k, high);
    bool lok = low_incl ? (a >= 0) : (a > 0);
    bool hok = high_incl ? (b <= 0) : (b < 0);
    return lok && hok;
  };
  for (Node* l = leaf; l; l = l->next) {
    for (int i = 0; i < static_cast<int>(l->keys.size()); ++i) {
      if (ok(l->keys[i])) out.push_back(l->rows[i]);
      if (cmpk(l->keys[i], high) > 0) return out;
    }
  }
  return out;
}

}  // namespace dbms
