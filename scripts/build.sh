#!/bin/bash
# ./scripts/build.sh syntax path/to/src.cpp
# ./scripts/build.sh make path/to/src -> make&build w/unix path
# ./scripts/build.sh build path/to/src -> build only w/unix path
# ./scripts/build.sh run path/to/src -> run w/unix path
# ./scripts/build.sh syntax windows path/to/src.cpp
# ./scripts/build.sh make windows path/to/src -> make&build w/windows path
# ./scripts/build.sh build windows path/to/src -> build only w/windows path
# ./scripts/build.sh run windows path/to/src -> run w/windows path
# TODO build RelWithDebugInfo,Release (not Debug only)
MakeUnix()
{
	cmake -S src/$1 -B build/unix/$1 -G Ninja -DCMAKE_CXX_COMPILER=clang++-11 -DCMAKE_CXX_FLAGS="-Weverything -Wno-c++98-c++11-c++14-compat -Wno-c++98-compat -Wno-c++98-compat-pedantic"
}
BuildUnix()
{
	cmake --build build/unix/$1
}
CheckSyntaxUnix()
{
	clang++-11 -fsyntax-only -std=c++20 -Weverything -Wno-c++98-c++11-c++14-compat -Wno-c++98-compat -Wno-c++98-compat-pedantic $1
}
MakeWindows()
{
	cmd.exe /c "scripts\make.bat src/$1 build/windows/$1"
}
BuildWindows()
{
	cmd.exe /c "scripts\build.bat build/windows/$1"
}
CheckSyntaxWindows()
{
	"/mnt/c/program files (x86)/microsoft visual studio/2019/community/VC/Tools/Llvm/bin/clang-cl.exe" -fsyntax-only /std:c++latest -Weverything $1
}

mode=$1
if [ "$2" = "windows" ]; then
	platform="windows"
	shift
else
	platform="unix"
fi
name="$2"

if [ "$mode" = "make" ]; then
	if [ "$platform" = "windows" ]; then
		MakeWindows $name
		BuildWindows $name
	else
		MakeUnix $name
		BuildUnix $name
	fi
elif [ "$mode" = "build" ]; then
	if [ "$platform" = "windows" ]; then
		BuildWindows $name
	else
		BuildUnix $name
	fi
else
	if [ "$platform" = "windows" ]; then
		CheckSyntaxWindows $name
	else
		CheckSyntaxUnix $name
	fi
fi
