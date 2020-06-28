#include "win32_window.h"
#include "doctest/doctest.h"
#include "logger.h"
TEST_CASE("test") {
  loginfo("hello world");
  CHECK(true);
}
