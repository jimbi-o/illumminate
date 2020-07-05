---
layout: post
title:  "cmake+ninja+clang on win10 without VS"
date:   2020-06-28 15:30:00 +0900
categories: renderer
---

VSなしでcmake+ninja+clang。

しようと思ったけどしんどかったのでVS経由でcmake+ninja+clang。

もしんどかったのでvs+cmake+clang。

1. cmakeでVSのプロジェクトを生成
1. ninjaファイル生成を指示
1. clangでビルドするよう指定
  * 諦めた。

```
# clang OK
cd ~/dev/illumminate/; cmake.exe -S src/gfx/win32 -B build -G "Visual Studio 16 2019" -A x64 -T"ClangCL" -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release" -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake.exe --build build --config RelWithDebInfo && ./build/RelWithDebInfo/illuminategfxwin32.exe
```

---

色々諦めた作業ログ

# ninja

1. ninjaを落として解凍して適当な場所に配置
  * <https://github.com/ninja-build/ninja/releases>
1. ninja.exeにPATH通す
  * WSL一旦落とさないとPATH更新されない(screen新しく開けても根本のbashが保持してるPATH持ってくる)

あとはパスにninja.exeがあればcmakeが勝手に見つけてくれる様子。

## refs

* https://ninja-build.org/

# clang

1. Visual Studio Installer > 変更 > Desktop development with C++ > C++ Clang tools for Windows (Windows用C++ Clangツール)
1. generatorにninja指定してcmakeしてみる
  * `cd ~/dev/illumminate/; cmake.exe -S src/gfx/win32 -B build -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake.exe --build build && ./build/illuminategfxwin32.exe`
1. `CMAKE_CXX_COMPILER`が無いと言われるので追加

```
cd ~/dev/illumminate/; cmake.exe -S src/gfx/win32 -B build -GNinja -DCMAKE_C_COMPILER='C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/Llvm/x64/bin/clang.exe' -DCMAKE_CXX_COMPILER='C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/Llvm/x64/bin/clang.exe' -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER_ID="Clang" -DCMAKE_CXX_COMPILER_ID="Clang" && cmake.exe --build build && ./build/illuminategfxwin32.exe

cd ~/dev/illumminate/; cmake.exe -E env LDFLAGS="-Fuse-ld=lld" cmake.exe -H. -G Ninja -Bbuild -DCMAKE_C_COMPILER:PATH="C:\MeineProgramme\LLVM\bin\clang.exe" -DCMAKE_CXX_COMPILER:PATH="C:\MeineProgramme\LLVM\bin\clang++.exe" -DCMAKE_C_COMPILER_ID="Clang" -DCMAKE_CXX_COMPILER_ID="Clang" -DCMAKE_SYSTEM_NAME="Generic"
```

## refs

* <https://docs.microsoft.com/en-us/cpp/build/clang-support-msbuild?view=vs-2019>

