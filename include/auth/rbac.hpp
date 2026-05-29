#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dbms {

enum class Perm : std::uint32_t {
  Read = 1,
  WriteData = 2,
  CreateTable = 4,
  DropTable = 8,
  DropDatabase = 16,
  All = Read | WriteData | CreateTable | DropTable | DropDatabase
};

struct UserAccount {
  std::string name;
  std::vector<std::uint8_t> salt;
  std::vector<std::uint8_t> hash;
  std::unordered_set<std::string> groups;
};

class Rbac {
 public:
  bool create_user(const std::string& name, const std::string& password, std::string& err);
  bool create_group(const std::string& name, std::string& err);
  void add_user_to_group(const std::string& user, const std::string& group);

  bool check(const std::string& user, const std::string& db, Perm p) const;

  void grant_default_on_new_database(const std::string& db);
  void grant(const std::string& db, const std::string& principal, std::uint32_t mask);

  const UserAccount* find_user(const std::string& name) const;

  bool verify_password(const std::string& name, const std::string& password) const;

  void load_from_file(const std::string& path);
  void save_to_file(const std::string& path) const;

 private:
  std::unordered_map<std::string, UserAccount> users_;
  std::unordered_set<std::string> groups_;
  // db -> principal -> mask
  std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> grants_;
  std::unordered_map<std::string, std::uint32_t> db_defaults_;
};

std::uint32_t perm_from_string(const std::string& s, std::string& err);

}  // namespace dbms
