#ifndef __ILLUMINATE_CORE_STRID_H__
#define __ILLUMINATE_CORE_STRID_H__
#include <cstdint>
#include <functional>
#define STRID_DEBUG_STR_ENABLED
using size_t = std::size_t;
namespace illuminate::core {
template <typename C, typename V>
constexpr bool IsContaining(const C& container, const V& val) {
  return container.find(val) != container.end();
}
using HashResult = uint32_t;
// https://xueyouchao.github.io/2016/11/16/CompileTimeString/
template <size_t N>
constexpr inline HashResult HornerHash(const HashResult prime, const char (&str)[N], const size_t len = N-1)
{
  return (len <= 1) ? str[0] : (prime * HornerHash(prime, str, len-1) + str[len-1]);
}
class StrId {
 public:
  static const HashResult kHashPrime = 31;
#ifndef STRID_DEBUG_STR_ENABLED
  constexpr StrId(const HashResult hash) : hash_(hash) {}
  constexpr StrId() : hash_(0) {}
#else
  StrId(const HashResult hash, const char* const str) : hash_(hash), str_(str) {}
  StrId() : hash_(0), str_() {}
#endif
  constexpr operator uint32_t() const { return hash_; }
  constexpr bool operator==(const StrId& id) const { return hash_ == id.hash_; } // for unordered_map
  constexpr HashResult GetHash() const { return hash_; }
 private:
  HashResult hash_;
#ifdef STRID_DEBUG_STR_ENABLED
  std::string str_;
#endif
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
#ifndef STRID_DEBUG_STR_ENABLED
#define SID(str) illuminate::core::StrId(illuminate::core::HornerHash(illuminate::core::StrId::kHashPrime,str))
#else
#define SID(str) illuminate::core::StrId(illuminate::core::HornerHash(illuminate::core::StrId::kHashPrime,str), str)
#endif
using StrId = illuminate::core::StrId;
#endif
#define HASH(str) illuminate::core::HornerHash(illuminate::core::StrId::kHashPrime,str)
