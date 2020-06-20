#ifdef BUILD_WITH_TEST
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
TEST_CASE("minimal") {
  CHECK(true);
}
#else
int main (int argc, char *argv[]) {
  spdlog::set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
  PrintBuildSettings();
  return 0;
}
#endif
