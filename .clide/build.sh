#!/usr/bin/env bash

cd "$CLIDE_PATH/../" || exit 1

if ! test -e "configuration"; then
  clide configure || exit 1
fi

export GAB_BUILDTYPE=
export GAB_CCFLAGS=
export GAB_TARGETS=
source configuration || exit 1

export unixflags="-DGAB_PLATFORM_UNIX -D_POSIX_C_SOURCE=200809L"
export winflags="-DGAB_PLATFORM_WIN"

function build {
  echo "Building $1"

  flags="-std=c23 -fPIC -Wall --target=$1 -Iinclude -Ivendor -Lbuild-$1 -DGAB_TARGET_TRIPLE=\"$1\" $GAB_CCFLAGS"

  platform=""
  dynlib_fileending=""

  if [[ "$1" =~ "linux" ]]; then
    platform="$unixflags"
    dynlib_fileending=".so"
  elif [[ "$1" =~ "mac" ]]; then
    platform="$unixflags"
    dynlib_fileending=".dylib"
  elif [[ "$1" =~ "windows" ]]; then
    platform="$winflags"
    dynlib_fileending=".dll"
  fi

  echo "   $flags $platform"

  echo "   Building static cgab library..."
  for file in src/cgab/*.c; do
    name=$(basename "$file" ".c")
    echo "      Building object $name..."
    zig cc $flags $platform -c -o "build-$1/cgab$name.o" "$file" || exit 1
    echo "      $(file "build-$1/cgab$name.o")"
  done
  zig ar rcs "build-$1/libcgab.a" build-"$1"/*.o
  echo "   Done!"
  echo "   $(file "build-$1/libcgab.a")"

  echo "   Building dynamic cgab library..."
  zig cc $flags $platform -shared -o "build-$1/libcgab$dynlib_fileending" build-"$1"/*.o || exit 1
  echo "   Done!"
  echo "   $(file "build-$1/libcgab$dynlib_fileending")"

  echo "   Building gab..."
  # Its important to link with cgab statically here. This is what
  # allows users to download the released binaries and install everything
  # they need from there.
  #
  # potentially need flag -rdynamic to export all libcgab symbols in the gab executable.
  # Locally on linux, this doesn't seem to be a problem - loading shared objects at runtime
  # doesn't require a second libcgab.so linked dynamically at runtime.
  # Verified this with:
  # strace -e trace=open,openat ./build-x86_64-linux-gnu/gab run test
  zig cc $flags $platform -o "build-$1/gab" "build-$1/libcgab.a" src/gab/*.c || exit 1
  echo "   Done!"
  echo "   $(file "build-$1/gab")"

  mkdir -p "build-$1/mod"

  # Compile all src/mod c files into shared objects
  # These modules link *dynamically* to libcgab - since they are loaded at runtime in gab process that already have all the necessary symbols.
  # Linking statically here would be redundant.
  echo "    Building shared libraries..."
  for file in src/mod/*.c; do
    name=$(basename "$file" ".c")
    echo "       Building gab$name..."
    zig cc -shared $flags $platform -lcgab -o "build-$1/mod/c$name$dynlib_fileending" "$file" || exit 1
    echo "       Done!"
    echo "       $(file "build-$1/mod/c$name$dynlib_fileending")"
  done
  echo "   Done!"


  echo "Success!"
}
export -f build

echo "Beginning compilation."
echo "$GAB_TARGETS"

echo "$GAB_TARGETS" | tr ' ' '\n' | parallel mkdir -p "build-{}" '&&' build "{}" '||' exit 1 '&&' echo "Built {}"
