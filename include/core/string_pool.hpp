#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbms {

// String interning: unique strings stored once; tables hold StringRef ids.
class StringPool {
 public:
  using Id = std::uint32_t;

  StringPool();
  Id intern(const std::string& s);
  const std::string& resolve(Id id) const;
  std::size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> strings_;
  std::unordered_map<std::string, Id> index_;
};

using SharedStringPool = std::shared_ptr<StringPool>;

}  // namespace dbms
