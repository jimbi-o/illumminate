"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 & "cmd" /c ""c:\program files (x86)\microsoft visual studio\2019\community\common7\ide\commonextensions\microsoft\cmake\CMake\bin\cmake.exe" -G "Ninja" -DCMAKE_BUILD_TYPE:STRING="Debug" -DCMAKE_C_COMPILER:FILEPATH="c:/program files (x86)/microsoft visual studio/2019/community/VC/Tools/Llvm/bin/clang-cl.exe" -DCMAKE_C_FLAGS="-m64 -fdiagnostics-absolute-paths /DWIN32 /D_WINDOWS /GR /EHsc" -DCMAKE_CXX_COMPILER:FILEPATH="c:/program files (x86)/microsoft visual studio/2019/community/VC/Tools/Llvm/bin/clang-cl.exe" -DCMAKE_CXX_FLAGS="-m64 -fdiagnostics-absolute-paths /DWIN32 /D_WINDOWS /GR /EHsc -Weverything -Wno-c++98-c++11-c++14-compat -Wno-c++98-compat -Wno-c++98-compat-pedantic" -DCMAKE_MAKE_PROGRAM="c:\program files (x86)\microsoft visual studio\2019\community\common7\ide\commonextensions\microsoft\cmake\Ninja\ninja.exe" -DCMAKE_EXE_LINKER_FLAGS=/machine:x64 -DCMAKE_MODULE_LINKER_FLAGS=/machine:x64 -DCMAKE_MT:FILEPATH="C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/mt.exe" -DCMAKE_SHARED_LINKER_FLAGS=/machine:x64 -DCMAKE_STATIC_LINKER_FLAGS=/machine:x64 -S %1 -B %2"
