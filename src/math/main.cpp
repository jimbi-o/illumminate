#ifdef BUILD_WITH_TEST
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
TEST_CASE("minimal") {
  CHECK(true);
}
#else
int main (int argc, char *argv[]) {
  return 0;
}
#endif
