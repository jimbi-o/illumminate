# illumminate

realtime 3d graphics renderer

# build and run example

```bash
# math lib

## ninja
cmake -S src/math -B build -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build && ./build/illuminatemath

## visual studio
cmake.exe -S src/math -B buildvs -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" && cmake.exe --build buildvs --config RelWithDebInfo && ./buildvs/RelWithDebInfo/illuminatemath.exe


# win32 lib

## ninja
cmake -S src/gfx/win32 -B build -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build && ./build/illuminategfxwin32

## visual studio
cmake.exe -S src/gfx/win32 -B buildvs -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" && cmake.exe --build buildvs --config RelWithDebInfo && ./buildvs/RelWithDebInfo/illuminategfxwin32.exe
```
