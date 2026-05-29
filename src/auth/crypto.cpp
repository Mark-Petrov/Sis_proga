#include "auth/crypto.hpp"

#include <openssl/rand.h>
#include <openssl/evp.h>

namespace dbms {

std::vector<std::uint8_t> random_bytes(std::size_t n) {
  std::vector<std::uint8_t> b(n);
  RAND_bytes(b.data(), static_cast<int>(n));
  return b;
}

void pbkdf2_sha256(const std::string& password, const std::vector<std::uint8_t>& salt,
                   int iterations, std::vector<std::uint8_t>& out, std::size_t out_len) {
  out.resize(out_len);
  if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                        static_cast<int>(salt.size()), iterations, EVP_sha256(),
                        static_cast<int>(out_len), out.data()) != 1) {
    out.assign(out_len, 0);
  }
}

}  // namespace dbms
