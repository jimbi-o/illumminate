#ifdef BUILD_WITH_TEST
#include "immintrin.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
TEST_CASE("minimal") {
  __m128 a;
  CHECK(true);
}
#else
int main (int argc, char *argv[]) {
  return 0;
}
#endif
