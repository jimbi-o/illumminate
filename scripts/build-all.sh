#!/bin/bash
bash scripts/build.sh make core
bash scripts/build.sh make gfx
bash scripts/build.sh make math
bash scripts/build.sh make windows core
bash scripts/build.sh make windows gfx
bash scripts/build.sh make windows gfx/d3d12
bash scripts/build.sh make windows math
# cmake.exe -A x64 -S src/gfx/d3d12 -B build/vs/gfx/d3d12; cmake.exe --build build/vs/gfx/d3d12 --config Debug
# deprecated: cmake.exe -A x64 -S src/gfx/d3d12 -B build/vs/gfx/d3d12 -T ClangCl # ClangCl did not work w/dxc
