#include "illuminate/math/vector.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "doctest/doctest.h"
TEST_CASE("vector") {
  using namespace illuminate::math;
  vec4 v0(1.0f,2.0f,3.0f,4.0f);
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 2.0f);
  CHECK(v0[2] == 3.0f);
  CHECK(v0[3] == 4.0f);
  v0 += 2.0f;
  CHECK(v0[0] == 3.0f);
  CHECK(v0[1] == 4.0f);
  CHECK(v0[2] == 5.0f);
  CHECK(v0[3] == 6.0f);
  v0 -= 2.0f;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 2.0f);
  CHECK(v0[2] == 3.0f);
  CHECK(v0[3] == 4.0f);
  v0 *= 3.0f;
  CHECK(v0[0] == 3.0f);
  CHECK(v0[1] == 6.0f);
  CHECK(v0[2] == 9.0f);
  CHECK(v0[3] == 12.0f);
  v0 /= 3.0f;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 2.0f);
  CHECK(v0[2] == 3.0f);
  CHECK(v0[3] == 4.0f);
  auto v1 = v0;
  CHECK((v0 == v1));
  v0 += v1;
  CHECK(v0[0] == 2.0f);
  CHECK(v0[1] == 4.0f);
  CHECK(v0[2] == 6.0f);
  CHECK(v0[3] == 8.0f);
  v0 -= v1;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 2.0f);
  CHECK(v0[2] == 3.0f);
  CHECK(v0[3] == 4.0f);
  v0 *= v1;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 4.0f);
  CHECK(v0[2] == 9.0f);
  CHECK(v0[3] == 16.0f);
  v0 /= v1;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 2.0f);
  CHECK(v0[2] == 3.0f);
  CHECK(v0[3] == 4.0f);
  auto v2(v0);
  CHECK(v0 == v2);
  v0 = v1 + v2;
  CHECK(v0[0] == 2.0f);
  CHECK(v0[1] == 4.0f);
  CHECK(v0[2] == 6.0f);
  CHECK(v0[3] == 8.0f);
  v0 = v1 - v2;
  CHECK(v0[0] == 0.0f);
  CHECK(v0[1] == 0.0f);
  CHECK(v0[2] == 0.0f);
  CHECK(v0[3] == 0.0f);
  v0 = v1 * v2;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 4.0f);
  CHECK(v0[2] == 9.0f);
  CHECK(v0[3] == 16.0f);
  v0 = v1 / v2;
  CHECK(v0[0] == 1.0f);
  CHECK(v0[1] == 1.0f);
  CHECK(v0[2] == 1.0f);
  CHECK(v0[3] == 1.0f);
  v0 = v1 + v0;
  CHECK(v0[0] == 2.0f);
  CHECK(v0[1] == 3.0f);
  CHECK(v0[2] == 4.0f);
  CHECK(v0[3] == 5.0f);
  v0[1] = 5.0f;
  CHECK(v0[1] == 5.0f);
  auto sv = v0 + 1.0f;
  CHECK(sv[0] == 3.0f);
  CHECK(sv[1] == 6.0f);
  CHECK(sv[2] == 5.0f);
  CHECK(sv[3] == 6.0f);
  sv = sv - 1.0f;
  CHECK(sv[0] == 2.0f);
  CHECK(sv[1] == 5.0f);
  CHECK(sv[2] == 4.0f);
  CHECK(sv[3] == 5.0f);
  sv = 1.0f + sv;
  CHECK(sv[0] == 3.0f);
  CHECK(sv[1] == 6.0f);
  CHECK(sv[2] == 5.0f);
  CHECK(sv[3] == 6.0f);
  sv = 1.0f - sv;
  CHECK(sv[0] == -2.0f);
  CHECK(sv[1] == -5.0f);
  CHECK(sv[2] == -4.0f);
  CHECK(sv[3] == -5.0f);
  sv = sv * 3.0f;
  CHECK(sv[0] == -6.0f);
  CHECK(sv[1] == -15.0f);
  CHECK(sv[2] == -12.0f);
  CHECK(sv[3] == -15.0f);
  sv = sv / 3.0f;
  CHECK(sv[0] == -2.0f);
  CHECK(sv[1] == -5.0f);
  CHECK(sv[2] == -4.0f);
  CHECK(sv[3] == -5.0f);
  sv = 3.0f * sv;
  CHECK(sv[0] == -6.0f);
  CHECK(sv[1] == -15.0f);
  CHECK(sv[2] == -12.0f);
  CHECK(sv[3] == -15.0f);
  sv = 30.0f / sv;
  CHECK(sv[0] == -5.0f);
  CHECK(sv[1] == -2.0f);
  CHECK(sv[2] == -2.5f);
  CHECK(sv[3] == -2.0f);
  auto s = sum(sv);
  CHECK(s == -11.5f);
  sv.normalize();
  CHECK(sv.dist() == 1.0f);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
