#include "minimal_for_cpp.h"
#define LoadDllFunction(library, function) decltype(&function) function = reinterpret_cast<decltype(function)>(GetProcAddress(library, #function))
//#define LoadDllVariable(library, var) decltype(var) var = reinterpret_cast<decltype(var)>(GetProcAddress(library, #var))
