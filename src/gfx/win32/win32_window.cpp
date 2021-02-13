#include "illuminate/illuminate.h"
#include "illuminate/gfx/win32/win32_window.h"
namespace illuminate::gfx::win32 {
namespace {
LRESULT CALLBACK WindowCallbackFunc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  return DefWindowProc(hwnd, msg, wParam, lParam);
}
}
static HWND InitWindow(const char* const title, const uint32_t width, const uint32_t height, WNDPROC proc) {
  WNDCLASSEX wc;
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = proc == nullptr ? WindowCallbackFunc : proc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 2);
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = title;
  wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
  if (!RegisterClassEx(&wc)) {
    auto err = GetLastError();
    logfatal("RegisterClassEx failed. {}", err);
    return nullptr;
  }
  auto hwnd = CreateWindowEx(0,
                             title,
                             title,
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             width, height,
                             nullptr,
                             nullptr,
                             wc.hInstance,
                             nullptr);
  if (!hwnd) {
    auto err = GetLastError();
    logfatal("CreateWindowEx failed. {}", err);
    return nullptr;
  }
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);
  return hwnd;
}
static bool CloseWindow(HWND hwnd, const char* const title) {
  if (!DestroyWindow(hwnd)) {
    logwarn("DestroyWindow failed. {}", GetLastError());
    return false;
  }
  if (!UnregisterClass(title, GetModuleHandle(nullptr))) {
    logwarn("UnregisterClass failed. {}", GetLastError());
    return false;
  }
  return true;
}
namespace {
bool SetWindowStyle(HWND hwnd, const UINT windowStyle) {
  SetLastError(0);
  if (SetWindowLongPtrW(hwnd, GWL_STYLE, windowStyle) != 0) return true;
  auto err = GetLastError();
  if (err == 0) return true;
  logwarn("SetWindowLongPtrW failed. {}", err);
  return false;
}
RECT GetCurrentDisplayRectInfo(HWND hwnd) {
  auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFOEX info = {};
  info.cbSize = sizeof(MONITORINFOEX);
  GetMonitorInfo(monitor, &info);
  return info.rcMonitor;
}
}
static RECT GetCurrentWindowRectInfo(HWND hwnd) {
  RECT rect = {};
  if (!GetWindowRect(hwnd, &rect)) {
    auto err = GetLastError();
    logwarn("GetWindowRect failed. {}", err);
  }
  return rect;
}
namespace {
bool SetWindowPos(HWND hwnd, HWND hWndInsertAfter, const RECT& rect) {
  if (::SetWindowPos(hwnd, hWndInsertAfter, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE)) return true;
  logwarn("SetWindowPos failed. {}", GetLastError());
  return false;
}
}
static bool SetFullscreenMode(HWND hwnd) {
  // not sure if this (FSBW) works for HDR mode.
  // use SetFullscreenState() in such case.
  // also unsure about if FPS caps unlike exclusive fullscreen mode.
  if (!SetWindowStyle(hwnd, WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))) return false;
  if (!SetWindowPos(hwnd, HWND_TOP, GetCurrentDisplayRectInfo(hwnd))) return false;
  ShowWindow(hwnd, SW_MAXIMIZE);
  return true;
}
static bool SetBackToWindowMode(HWND hwnd, const RECT& rect) {
  if (!SetWindowStyle(hwnd, WS_OVERLAPPEDWINDOW)) return false;
  if (!SetWindowPos(hwnd, HWND_NOTOPMOST, rect)) return false;
  ShowWindow(hwnd, SW_NORMAL);
  return true;
}
namespace {
std::vector<WindowCallback> callbacks;
}
bool Window::Init(const char* const title, const uint32_t width, const uint32_t height) {
  title_ = title;
  hwnd_ = illuminate::gfx::win32::InitWindow(title_.c_str(), width, height, CallbackFunc);
  return hwnd_ != nullptr;
}
void Window::Term() {
  std::vector<WindowCallback>().swap(callbacks);
  illuminate::gfx::win32::CloseWindow(hwnd_, title_.c_str());
}
LRESULT Window::CallbackFunc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  for (auto&& func : callbacks) {
    if (func(hwnd, msg, wParam, lParam)) return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}
void Window::AddCallback(WindowCallback&& func) {
  callbacks.push_back(std::move(func));
}
void Window::ProcessMessage() {
  MSG msg = {};
  while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}
}
#include "doctest/doctest.h"
TEST_CASE("win32 windows func test") {
  auto hwnd = illuminate::gfx::win32::InitWindow("test window", 200, 100, nullptr);
  CHECK(hwnd != nullptr);
  auto windowRect = illuminate::gfx::win32::GetCurrentWindowRectInfo(hwnd);
  CHECK(windowRect.right - windowRect.left == 200);
  CHECK(windowRect.bottom - windowRect.top == 100);
  CHECK(illuminate::gfx::win32::SetFullscreenMode(hwnd) == true);
  CHECK(illuminate::gfx::win32::SetBackToWindowMode(hwnd, windowRect) == true);
  CHECK(illuminate::gfx::win32::CloseWindow(hwnd, "test window") == true);
}
TEST_CASE("win32 window class") {
  illuminate::gfx::win32::Window window;
  REQUIRE(window.Init("hello", 100, 200));
  window.AddCallback([&]([[maybe_unused]]HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { loginfo("hello from callback msg:{0:x} wParam:{0:x} lParam:{0:x}", msg, wParam, lParam); return false; });
  window.ProcessMessage();
  window.Term();
}
