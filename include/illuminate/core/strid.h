#ifndef ILLUMINATE_CORE_STRID_H
#define ILLUMINATE_CORE_STRID_H
#include <cstdint>
#include <functional>
#include <string>
#include "string.h"
using size_t = std::size_t;
#define ENABLE_STRID_DEBUG_STR
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
  constexpr explicit StrId(const char (&str)[N]) : hash_(HornerHash(kHashPrime, str)), str_(RegisterHash(hash_, str)) {}
  constexpr StrId(const StrId& strid) : hash_(strid.hash_), str_(strid.str_) {}
  constexpr StrId(StrId&& strid) : hash_(std::move(strid.hash_)), str_(std::move(strid.str_)) {}
  constexpr StrId() : hash_(0), str_(nullptr) {}
  constexpr StrId operator=(const StrId& strid)  { hash_ = strid.hash_; str_ = strid.str_; return *this; }
  constexpr StrId operator=(StrId&& strid)  { hash_ = std::move(strid.hash_); str_ = std::move(strid.str_); return *this; }
  constexpr operator uint32_t() const { return hash_; }
  constexpr bool operator==(const StrId& id) const { return hash_ == id.hash_; } // for unordered_map
  constexpr StrHash GetHash() const { return hash_; }
 private:
  template <size_t N>
  constexpr static const char* RegisterHash([[maybe_unused]] const StrHash hash, const char (&str)[N]) {
#ifdef ENABLE_STRID_DEBUG_STR
    // TODO hash collision check
    if (debug_buffer_index + N + 1 >= 1024) debug_buffer_index = 0;
    auto ret = &debug_buffer[debug_buffer_index];
    for (uint32_t i = 0; i < N; i++) {
      debug_buffer[debug_buffer_index] = str[i];
      debug_buffer_index++;
    }
    debug_buffer[N] = '\0';
    debug_buffer_index++;
    return ret;
#endif
    return nullptr;
  }
#ifdef ENABLE_STRID_DEBUG_STR
  static const uint32_t debug_buffer_len = 1024;
  static char debug_buffer[debug_buffer_len];
  static uint32_t debug_buffer_index;
#endif
  StrHash hash_;
  [[maybe_unused]] const uint8_t _pad[4]{};
  [[maybe_unused]] const char* str_;
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
