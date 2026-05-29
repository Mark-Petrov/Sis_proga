#include "core/string_pool.hpp"

namespace dbms {

StringPool::StringPool() {
  strings_.push_back("");  // id 0 reserved as empty marker
}

StringPool::Id StringPool::intern(const std::string& s) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = index_.find(s);
  if (it != index_.end()) return it->second;
  Id id = static_cast<Id>(strings_.size());
  index_.emplace(s, id);
  strings_.push_back(s);
  return id;
}

const std::string& StringPool::resolve(Id id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (id >= strings_.size()) {
    static const std::string kEmpty;
    return kEmpty;
  }
  return strings_[id];
}

std::size_t StringPool::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return strings_.size();
}

}  // namespace dbms
