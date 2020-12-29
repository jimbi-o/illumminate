#include "minimal_for_cpp.h"
#include <sstream>
#include "d3d12_util.h"
#define CALL(function) reinterpret_cast<decltype(&function)>(GetProcAddress(library_, #function))
#define LOAD_DLL_FUNC_TO_VAR(library, function) decltype(&function) function = reinterpret_cast<decltype(function)>(GetProcAddress(library, #function))
#define SET_NAME(obj, name, n)                  \
  {                                             \
    std::wostringstream stream;                 \
    stream << name << " " << n;                 \
    obj->SetName(stream.str().c_str());         \
  }                                             \
  (void(0))
