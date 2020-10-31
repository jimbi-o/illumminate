#ifndef ILLUMINATE_CORE_STRID_H
#define ILLUMINATE_CORE_STRID_H
#include <cstdint>
#include <functional>
#include <string>
using size_t = std::size_t;
namespace illuminate::core {
using StrHash = uint32_t;
// https://xueyouchao.github.io/2016/11/16/CompileTimeString/
template <size_t N>
constexpr inline StrHash HornerHash(const StrHash prime, const char (&str)[N], const size_t len = N-1)
{
  return (len <= 1) ? static_cast<std::make_unsigned_t<char>>(str[0]) : (prime * HornerHash(prime, str, len-1) + static_cast<std::make_unsigned_t<char>>(str[len-1]));
}
class StrId final {
 public:
  static const StrHash kHashPrime = 31;
  template <size_t N>
  constexpr explicit StrId(const char (&str)[N]) : hash_(HornerHash(kHashPrime, str)) {}
  constexpr StrId() : hash_(0) {}
  constexpr operator uint32_t() const { return hash_; }
  constexpr bool operator==(const StrId& id) const { return hash_ == id.hash_; } // for unordered_map
  constexpr StrHash GetHash() const { return hash_; }
 private:
  StrHash hash_;
};
}
// for unordered_map
namespace std {
template <>
struct hash<illuminate::core::StrId> {
  std::size_t operator()(const illuminate::core::StrId& id) const { return id.GetHash(); }
};
template <>
struct less<illuminate::core::StrId> {
  bool operator()(const illuminate::core::StrId& l, const illuminate::core::StrId& r) const { return r.GetHash() < l.GetHash(); }
};
}
using StrId = illuminate::core::StrId;
#define SID(str) illuminate::core::HornerHash(StrId::kHashPrime, str)
#endif
