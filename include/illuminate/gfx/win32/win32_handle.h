#ifndef __ILLUMINATE_WIN32_H__
#define __ILLUMINATE_WIN32_H__
#include <cstdint>
#include <string>
#include <functional>
#include <Windows.h>
namespace illuminate::gfx::win32 {
HANDLE CreateEventHandle();
bool CloseHandle(HANDLE handle);
}
#endif
