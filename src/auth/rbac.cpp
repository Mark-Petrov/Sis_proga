#include "auth/rbac.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "auth/crypto.hpp"

namespace dbms {

static const int kIters = 100000;
static const std::size_t kHashLen = 32;

std::uint32_t perm_from_string(const std::string& s, std::string& err) {
  err.clear();
  std::string sl = s;
  for (char& c : sl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (sl == "read") return static_cast<std::uint32_t>(Perm::Read);
  if (sl == "write") return static_cast<std::uint32_t>(Perm::WriteData);
  if (sl == "create_table") return static_cast<std::uint32_t>(Perm::CreateTable);
  if (sl == "drop_table") return static_cast<std::uint32_t>(Perm::DropTable);
  if (sl == "drop_database") return static_cast<std::uint32_t>(Perm::DropDatabase);
  err = "unknown permission: " + s;
  return 0;
}

bool Rbac::create_user(const std::string& name, const std::string& password, std::string& err) {
  if (users_.count(name)) {
    err = "user exists";
    return false;
  }
  UserAccount u;
  u.name = name;
  u.salt = random_bytes(16);
  pbkdf2_sha256(password, u.salt, kIters, u.hash, kHashLen);
  users_[name] = std::move(u);
  return true;
}

bool Rbac::create_group(const std::string& name, std::string& err) {
  if (groups_.count(name)) {
    err = "group exists";
    return false;
  }
  groups_.insert(name);
  return true;
}

void Rbac::add_user_to_group(const std::string& user, const std::string& group) {
  auto it = users_.find(user);
  if (it != users_.end()) it->second.groups.insert(group);
}

const UserAccount* Rbac::find_user(const std::string& name) const {
  auto it = users_.find(name);
  return it == users_.end() ? nullptr : &it->second;
}

bool Rbac::verify_password(const std::string& name, const std::string& password) const {
  auto it = users_.find(name);
  if (it == users_.end()) return false;
  std::vector<std::uint8_t> out;
  pbkdf2_sha256(password, it->second.salt, kIters, out, kHashLen);
  return out.size() == it->second.hash.size() &&
         std::equal(out.begin(), out.end(), it->second.hash.begin());
}

void Rbac::grant_default_on_new_database(const std::string& db) {
  db_defaults_[db] = static_cast<std::uint32_t>(Perm::All);
}

void Rbac::grant(const std::string& db, const std::string& principal, std::uint32_t mask) {
  grants_[db][principal] |= mask;
}

bool Rbac::check(const std::string& user, const std::string& db, Perm p) const {
  std::uint32_t mask = static_cast<std::uint32_t>(p);
  auto d = db_defaults_.find(db);
  if (d != db_defaults_.end() && (d->second & mask)) return true;
  auto git = grants_.find(db);
  if (git != grants_.end()) {
    auto ui = git->second.find("user:" + user);
    if (ui != git->second.end() && (ui->second & mask)) return true;
    auto uacc = users_.find(user);
    if (uacc != users_.end()) {
      for (const std::string& g : uacc->second.groups) {
        auto gi = git->second.find("group:" + g);
        if (gi != git->second.end() && (gi->second & mask)) return true;
      }
    }
  }
  return false;
}

void Rbac::load_from_file(const std::string& path) {
  (void)path;
}

void Rbac::save_to_file(const std::string& path) const {
  (void)path;
}

}  // namespace dbms
