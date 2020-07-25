# illumminate

realtime 3d graphics renderer

# build and run example

```bash
# math lib

## ninja
cmake -S src/math -B build -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build && ./build/illuminatemath

## visual studio
cmake.exe -S src/math -B buildvs -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" && cmake.exe --build buildvs --config RelWithDebInfo && ./buildvs/RelWithDebInfo/illuminatemath.exe

# core

## syntax check

clang -std=c++17 -I ../ -I ../../include -I ../../include/illuminate/core -I ../../build/_deps/doctest-src/ -I ../../build/_deps/spdlog-src/include/ -fsyntax-only strid.cpp

## ninja
cmake -S src/core -B buildcore -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build buildcore && ./buildcore/illuminatecore

## visual studio
cmake.exe -S src/core -B buildcorevs -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" && cmake.exe --build buildcorevs --config RelWithDebInfo && ./buildcorevs/RelWithDebInfo/illuminatecore.exe

# win32 lib

## visual studio
cmake.exe -S src/gfx/win32 -B build -G "Visual Studio 16 2019" -A x64 -T"ClangCL" -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake.exe --build build --config RelWithDebInfo && ./build/RelWithDebInfo/illuminategfxwin32.exe

# renderer

## ninja
cmake -S src/gfx -B buildgfx -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build buildgfx && ./buildgfx/illuminategfx

## visual studio
cmake.exe -S src/gfx -B buildgfxvs -G "Visual Studio 16 2019" -A x64 -T"ClangCL" -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake.exe --build buildgfxvs --config RelWithDebInfo && ./buildgfxvs/RelWithDebInfo/illuminategfx.exe

# renderer d3d12

## visual studio
cmake.exe -S src/gfx/d3d12 -B buildgfxd3d12 -G "Visual Studio 16 2019" -A x64 -T"ClangCL" -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake.exe --build buildgfxd3d12 --config RelWithDebInfo && ./buildgfxd3d12/RelWithDebInfo/illuminategfxd3d12.exe

```
