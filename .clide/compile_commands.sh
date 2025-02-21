#!/usr/bin/env bash

set -euo pipefail

cd "$CLIDE_PATH/../" || exit 1

if ! test -e "configuration"; then
  clide configure || exit 1
fi

export GAB_BUILDTYPE=
export GAB_CCFLAGS=
export GAB_TARGETS=
source configuration || exit 1
targets="$GAB_TARGETS"

export unixflags="-DGAB_PLATFORM_UNIX -D_POSIX_C_SOURCE=200809L"
export winflags="-DGAB_PLATFORM_WIN"

platform=""
dynlib_fileending=""

if [[ "$targets" =~ "linux" ]]; then
  platform="$unixflags"
elif [[ "$targets" =~ "mac" ]]; then
  platform="$unixflags"
elif [[ "$targets" =~ "windows" ]]; then
  platform="$winflags"
fi

flags="-std=c2x -fPIC -Wall -DGAB_CORE -Iinclude -Ivendor -Lbuild-$targets -DGAB_TARGET_TRIPLE=\"$targets\" -DGAB_DYNLIB_FILEENDING=\"$dynlib_fileending\" $GAB_CCFLAGS"

bear -- clang $flags $platform -DGAB_CORE -c -o "build-$targets/libcgab.a" "src/gab/main.c"
