#ifndef __ILLUMINATE_MATH_VECTOR_H__
#define __ILLUMINATE_MATH_VECTOR_H__
#include <cstdint>
#include <math.h>
#include "immintrin.h"
namespace illuminate::math {
using simd_vec = __m128;
union RawVector {
  simd_vec data;
  float array[4];
};
inline float hadd(const simd_vec& v) {
  // https://stackoverflow.com/questions/6996764/fastest-way-to-do-horizontal-sse-vector-sum-or-other-reduction/35270026#35270026
  auto moved = _mm_movehdup_ps(v);
  auto sum = _mm_add_ps(v, moved);
  moved = _mm_movehl_ps(moved, sum);
  sum = _mm_add_ss(sum, moved);
  return _mm_cvtss_f32(sum);
}
class vector {
 public:
  vector() {
    vec() = set_zero();
  }
  vector(const float c) {
    vec() = set_val(c);
  }
  vector(const float f0, const float f1, const float f2, const float f3) {
    vec() = set_val(f0, f1, f2, f3);
  }
  vector(const float f0, const float f1, const float f2) {
    vec() = set_val(f0, f1, f2, 0.0f);
  }
  vector(const simd_vec&& v) {
    vec() = std::move(v);
  }
  vector(const vector& v) : data(v.data) {
  }
  vector operator=(const vector& v) {
    vec() = v.vec();
    return *this;
  }
  bool operator==(const vector& v) const {
    auto cmp = _mm_cmpeq_ps(vec(), v.vec());
    auto result = _mm_movemask_epi8(_mm_castps_si128(cmp));
    return result == 0xFFFF;
  }
  constexpr float operator[](const uint32_t index) const {
    return array()[index];
  }
  float& operator[](const uint32_t index) {
    return array()[index];
  }
  vector& operator+=(const float c) {
    auto v = set_val(c);
    vec() = _mm_add_ps(vec(), v);
    return *this;
  }
  vector& operator-=(const float c) {
    auto v = set_val(c);
    vec() = _mm_sub_ps(vec(), v);
    return *this;
  }
  vector& operator*=(const float c) {
    auto v = set_val(c);
    vec() = _mm_mul_ps(vec(), v);
    return *this;
  }
  vector& operator/=(const float c) {
    auto v = set_val(c);
    vec() = _mm_div_ps(vec(), v);
    return *this;
  }
  vector& operator+=(const vector& v) {
    vec() = _mm_add_ps(vec(), v.vec());
    return *this;
  }
  vector& operator-=(const vector& v) {
    vec() = _mm_sub_ps(vec(), v.vec());
    return *this;
  }
  vector& operator*=(const vector& v) {
    vec() = _mm_mul_ps(vec(), v.vec());
    return *this;
  }
  vector& operator/=(const vector& v) {
    vec() = _mm_div_ps(vec(), v.vec());
    return *this;
  }
  vector& rcp() {
    vec() = _mm_rcp_ps(vec());
    return *this;
  }
  float dist() const {
    auto sq = _mm_mul_ps(vec(), vec());
    auto sum = hadd(sq);
    return sqrt(sum);
  }
  vector& normalize() {
    float d = dist();
    (*this) /= d;
    return *this;
  }
  friend vector operator+(const vector& v, const float c) {
    auto v2 = vector(c);
    v2.vec() = _mm_add_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vector operator-(const vector& v, const float c) {
    auto v2 = vector(c);
    v2.vec() = _mm_sub_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vector operator*(const vector& v, const float c) {
    auto v2 = vector(c);
    v2.vec() = _mm_mul_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vector operator/(const vector& v, const float c) {
    auto v2 = vector(c);
    v2.vec() = _mm_div_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vector operator+(const float c, const vector& v) {
    auto v2 = vector(c);
    v2.vec() = _mm_add_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vector operator-(const float c, const vector& v) {
    auto v2 = vector(c);
    v2.vec() = _mm_sub_ps(v2.vec(), v.vec());
    return v2;
  }
  friend vector operator*(const float c, const vector& v) {
    auto v2 = vector(c);
    v2.vec() = _mm_mul_ps(v.vec(), v2.vec());
    return v2;
  }
  friend vector operator/(const float c, const vector& v) {
    auto v2 = vector(c);
    v2.vec() = _mm_div_ps(v2.vec(), v.vec());
    return v2;
  }
  friend vector operator+(const vector& a, const vector& b) {
    auto result = _mm_add_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vector operator-(const vector& a, const vector& b) {
    auto result = _mm_sub_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vector operator*(const vector& a, const vector& b) {
    auto result = _mm_mul_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vector operator/(const vector& a, const vector& b) {
    auto result = _mm_div_ps(a.vec(), b.vec());
    return {std::move(result)};
  }
  friend vector rcp(const vector& v) {
    auto result = _mm_rcp_ps(v.vec());
    return {std::move(result)};
  }
  friend float sum(const vector& v) {
    return hadd(v.vec());
  }
 private:
  static inline simd_vec set_zero() { return _mm_setzero_ps(); }
  static inline simd_vec set_val(const float c) { return _mm_set1_ps(c); }
  static inline simd_vec set_val(const float f0, const float f1, const float f2, const float f3) { return _mm_setr_ps(f0, f1, f2, f3); }
  inline const simd_vec& vec() const { return data.data; }
  inline simd_vec& vec() { return data.data; }
  inline constexpr const float* array() const { return data.array; }
  inline float* array() { return data.array; }
  RawVector data;
};
using vec4 = vector;
using vec3 = vector;
}
#endif
