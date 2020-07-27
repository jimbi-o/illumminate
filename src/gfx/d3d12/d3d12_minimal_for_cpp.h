#include "minimal_for_cpp.h"
#define CALL(function) reinterpret_cast<decltype(&function)>(GetProcAddress(library_, #function))
#define LOAD_DLL_FUNC_TO_VAR(library, function) decltype(&function) function = reinterpret_cast<decltype(function)>(GetProcAddress(library, #function))
