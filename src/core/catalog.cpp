#include "core/catalog.hpp"

namespace dbms {

bool Catalog::create_database(const std::string& name, std::string& err) {
  if (dbs_.count(name)) {
    err = "database already exists: " + name;
    return false;
  }
  dbs_[name] = std::make_unique<Database>(name);
  return true;
}

bool Catalog::drop_database(const std::string& name, std::string& err) {
  if (!dbs_.count(name)) {
    err = "database not found: " + name;
    return false;
  }
  if (current_ == name) current_.clear();
  dbs_.erase(name);
  return true;
}

Database* Catalog::get_database(const std::string& name) {
  auto it = dbs_.find(name);
  return it == dbs_.end() ? nullptr : it->second.get();
}

const Database* Catalog::get_database(const std::string& name) const {
  auto it = dbs_.find(name);
  return it == dbs_.end() ? nullptr : it->second.get();
}

void Catalog::set_current(const std::string& name, std::string& err) {
  if (!dbs_.count(name)) {
    err = "database not found: " + name;
    return;
  }
  current_ = name;
}

Database* Catalog::current() { return current_.empty() ? nullptr : get_database(current_); }

const Database* Catalog::current() const {
  return current_.empty() ? nullptr : get_database(current_);
}

}  // namespace dbms
