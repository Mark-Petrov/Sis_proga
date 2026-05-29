#include "auth/jwt.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace dbms {

static std::string b64url_encode(const std::string& in) {
  static const char* tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(tbl[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  for (char& c : out) {
    if (c == '+') c = '-';
    if (c == '/') c = '_';
  }
  while (!out.empty() && out.back() == '=') out.pop_back();
  return out;
}

static std::string b64url_decode(std::string in) {
  for (char& c : in) {
    if (c == '-') c = '+';
    if (c == '_') c = '/';
  }
  while (in.size() % 4) in.push_back('=');
  static const std::unordered_map<char, int> T = [] {
    std::unordered_map<char, int> m;
    const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) m[tbl[i]] = i;
    return m;
  }();
  std::string out;
  int val = 0, valb = -8;
  for (char c : in) {
    if (T.find(c) == T.end()) break;
    val = (val << 6) + T.at(c);
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

static std::string hmac_sha256(const std::string& key, const std::string& data) {
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int out_len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), out, &out_len);
  return std::string(reinterpret_cast<char*>(out), out_len);
}

std::string jwt_sign_hs256(const std::string& secret, const std::string& sub, int ttl_sec) {
  using namespace std::chrono;
  auto exp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count() + ttl_sec;
  std::string header = R"({"alg":"HS256","typ":"JWT"})";
  std::string payload = "{\"sub\":\"" + sub + "\",\"exp\":" + std::to_string(exp) + "}";
  std::string h = b64url_encode(header);
  std::string p = b64url_encode(payload);
  std::string data = h + "." + p;
  std::string sig = b64url_encode(hmac_sha256(secret, data));
  return data + "." + sig;
}

bool jwt_verify(const std::string& secret, const std::string& token, JwtPayload& out,
                std::string& err) {
  auto p1 = token.find('.');
  auto p2 = token.find('.', p1 + 1);
  if (p1 == std::string::npos || p2 == std::string::npos) {
    err = "bad jwt format";
    return false;
  }
  std::string h = token.substr(0, p1);
  std::string pl = token.substr(p1 + 1, p2 - p1 - 1);
  std::string sg = token.substr(p2 + 1);
  std::string data = h + "." + pl;
  std::string expect = b64url_encode(hmac_sha256(secret, data));
  if (expect != sg) {
    err = "bad jwt signature";
    return false;
  }
  std::string payload = b64url_decode(pl);
  auto ps = payload.find("\"sub\":\"");
  auto pe = payload.find('"', ps + 7);
  if (ps == std::string::npos || pe == std::string::npos) {
    err = "bad jwt payload";
    return false;
  }
  out.sub = payload.substr(ps + 7, pe - (ps + 7));
  out.exp = 0;
  auto es = payload.find("\"exp\":");
  if (es != std::string::npos) {
    es += 6;
    while (es < payload.size() && (payload[es] == ' ' || payload[es] == '\t')) ++es;
    std::size_t j = es;
    while (j < payload.size() && std::isdigit(static_cast<unsigned char>(payload[j]))) ++j;
    out.exp = static_cast<std::uint64_t>(std::stoull(payload.substr(es, j - es)));
  }
  using namespace std::chrono;
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  if (out.exp != 0 && static_cast<std::int64_t>(out.exp) < now) {
    err = "jwt expired";
    return false;
  }
  return true;
}

}  // namespace dbms
