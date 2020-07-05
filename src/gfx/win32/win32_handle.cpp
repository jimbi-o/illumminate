#include "win32_handle.h"
#include "logger.h"
#include "doctest/doctest.h"
namespace illuminate::gfx::win32 {
HANDLE CreateEventHandle() {
  auto handle = CreateEvent(nullptr, false, false, nullptr);
  if (handle == nullptr) {
    auto err = GetLastError();
    logwarn("CreateEvent failed. {}", err);
  }
  return handle;
}
bool CloseHandle(HANDLE handle) {
  if (::CloseHandle(handle)) return true;
  logwarn("CloseHandle failed. {}", GetLastError());
  return false;
}
}
#include "doctest/doctest.h"
TEST_CASE("win32 event handle test") {
  auto handle = illuminate::gfx::win32::CreateEventHandle();
  CHECK(handle != nullptr);
  CHECK(illuminate::gfx::win32::CloseHandle(handle) == true);
}
