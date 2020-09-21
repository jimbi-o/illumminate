#ifndef ILLUMINATE_WIN32_H
#define ILLUMINATE_WIN32_H
#include <cstdint>
#include <string>
#include <functional>
#include <Windows.h>
namespace illuminate::gfx::win32 {
HANDLE CreateEventHandle();
bool CloseHandle(HANDLE handle);
}
#endif
