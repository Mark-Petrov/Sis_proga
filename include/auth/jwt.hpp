#pragma once
#include <string>

namespace dbms {

struct JwtPayload {
  std::string sub;
  std::uint64_t exp{0};
};

std::string jwt_sign_hs256(const std::string& secret, const std::string& sub, int ttl_sec);
bool jwt_verify(const std::string& secret, const std::string& token, JwtPayload& out,
                std::string& err);

}  // namespace dbms
