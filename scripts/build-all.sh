#!/bin/bash
bash scripts/build.sh make core
bash scripts/build.sh make gfx
bash scripts/build.sh make math
bash scripts/build.sh make windows core
bash scripts/build.sh make windows gfx
bash scripts/build.sh make windows gfx/d3d12
bash scripts/build.sh make windows math
