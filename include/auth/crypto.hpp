#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dbms {

void pbkdf2_sha256(const std::string& password, const std::vector<std::uint8_t>& salt,
                   int iterations, std::vector<std::uint8_t>& out, std::size_t out_len);

std::vector<std::uint8_t> random_bytes(std::size_t n);

}  // namespace dbms
