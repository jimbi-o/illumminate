#!/bin/bash
bash scripts/build.sh make core
bash scripts/build.sh make gfx
bash scripts/build.sh make math
bash scripts/build.sh make windows core
bash scripts/build.sh make windows gfx
bash scripts/build.sh make windows gfx/d3d12
bash scripts/build.sh make windows math
# deprecated: cmake.exe -A x64 -S src/gfx/d3d12 -B build/vs/gfx/d3d12 -T ClangCl # ClangCl did not work w/dxc
# cmake.exe -S src/gfx -B build/vs/gfx -A x64 -DCMAKE_CXX_FLAGS="-Wall" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake.exe --build build/vs/gfx/d3d12 --config Debug
