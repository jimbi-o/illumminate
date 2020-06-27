#ifndef __ILLUMINATE_MATH_VECTOR_H__
#define __ILLUMINATE_MATH_VECTOR_H__
#include <cstdint>
#include "immintrin.h"
namespace illuminate::math {
using simd_vec = __m128;
union RawVector {
  simd_vec data;
  float array[4];
};
class vec4 {
 public:
  vec4() {
    vec() = set_zero();
  }
  vec4(const float c) {
    vec() = set_val(c);
  }
  vec4(const float f0, const float f1, const float f2, const float f3) {
    vec() = set_val(f0, f1, f2, f3);
  }
  vec4(const simd_vec&& v) {
    vec() = std::move(v);
  }
  vec4(const vec4& v) : data(v.data) {
  }
  vec4 operator=(const vec4& v) {
    vec() = v.vec();
    return *this;
  }
  bool operator==(const vec4& v) const {
    auto cmp = _mm_cmpeq_ps(vec(), v.vec());
    auto result = _mm_movemask_epi8(cmp);
    return result == 0xFFFF;
  }
  constexpr float operator[](const uint32_t index) const {
    return array()[index];
  }
  float& operator[](const uint32_t index) {
    return array()[index];
  }
  vec4& operator+=(const float c) {
    auto v = set_val(c);
    vec() = _mm_add_ps(vec(), v);
    return *this;
  }
  vec4& operator-=(const float c) {
    auto v = set_val(c);
    vec() = _mm_sub_ps(vec(), v);
    return *this;
  }
  vec4& operator*=(const float c) {
    auto v = set_val(c);
    vec() = _mm_mul_ps(vec(), v);
    return *this;
  }
  vec4& operator/=(const float c) {
    auto v = set_val(c);
    vec() = _mm_div_ps(vec(), v);
    return *this;
  }
  vec4& operator+=(const vec4& v) {
    vec() = _mm_add_ps(vec(), v.vec());
    return *this;
  }
  vec4& operator-=(const vec4& v) {
    vec() = _mm_sub_ps(vec(), v.vec());
    return *this;
  }
  vec4& operator*=(const vec4& v) {
    vec() = _mm_mul_ps(vec(), v.vec());
    return *this;
  }
  vec4& operator/=(const vec4& v) {
    vec() = _mm_div_ps(vec(), v.vec());
    return *this;
  }
  friend vec4 operator+(const vec4& v, const float c) {
    auto v2 = vec4(c);
    v2.vec() = _mm_add_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vec4 operator-(const vec4& v, const float c) {
    auto v2 = vec4(c);
    v2.vec() = _mm_sub_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vec4 operator*(const vec4& v, const float c) {
    auto v2 = vec4(c);
    v2.vec() = _mm_mul_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vec4 operator/(const vec4& v, const float c) {
    auto v2 = vec4(c);
    v2.vec() = _mm_div_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vec4 operator+(const float c, const vec4& v) {
    auto v2 = vec4(c);
    v2.vec() = _mm_add_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vec4 operator-(const float c, const vec4& v) {
    auto v2 = vec4(c);
    v2.vec() = _mm_sub_ps(v2.vec(), v.vec());
    return v2;
  }
  friend vec4 operator*(const float c, const vec4& v) {
    auto v2 = vec4(c);
    v2.vec() = _mm_mul_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vec4 operator/(const float c, const vec4& v) {
    auto v2 = vec4(c);
    v2.vec() = _mm_div_ps(v2.vec(), v.vec());
    return v2;
  }
  friend vec4 operator+(const vec4& a, const vec4& b) {
    auto result = _mm_add_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vec4 operator-(const vec4& a, const vec4& b) {
    auto result = _mm_sub_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vec4 operator*(const vec4& a, const vec4& b) {
    auto result = _mm_mul_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vec4 operator/(const vec4& a, const vec4& b) {
    auto result = _mm_div_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
 private:
  static inline simd_vec set_zero() { return _mm_setzero_ps(); }
  static inline simd_vec set_val(const float c) { return _mm_set1_ps(c); }
  static inline simd_vec set_val(const float f0, const float f1, const float f2, const float f3) { return _mm_setr_ps(f0, f1, f2, f3); }
  inline const simd_vec& vec() const { return data.data; }
  inline simd_vec& vec() { return data.data; }
  inline const float* array() const { return data.array; }
  inline float* array() { return data.array; }
  RawVector data;
};
}
#endif
