#!/usr/bin/env bash

cd "$CLIDE_PATH/../" || exit 1

function gen-libgrapheme() {
  cd vendor/libgrapheme || exit 1

  # Compile natively to run the generators.
  make CC="zig cc" libgrapheme.a

  # Clean up native outputs
  rm libgrapheme.a
  rm src/*.o

  cd "$CLIDE_PATH/../" || exit 1
}

function build-libgrapheme() {
  cd vendor/libgrapheme || exit 1

  make CC="zig cc --target=$1" libgrapheme.a
  mv libgrapheme.a "$CLIDE_PATH/../$2"
  rm src/*.o

  cd "$CLIDE_PATH/../" || exit 1
}

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

  platform=""
  dynlib_fileending=""
  platform_bundle=""

  if [[ "$1" =~ "linux" ]]; then
    platform="$unixflags"
    platform_bundle="-shared"
    dynlib_fileending=".so"
  elif [[ "$1" =~ "mac" ]]; then
    platform="$unixflags"
    platform_bundle="-shared"
    dynlib_fileending=".so"
  elif [[ "$1" =~ "windows" ]]; then
    platform="$winflags"
    dynlib_fileending=".dll"
    platform_bundle="-shared"
  fi

  flags="-std=c23 -fPIC -Wall --target=$1 -Iinclude -Ivendor -Lbuild-$1 -DGAB_TARGET_TRIPLE=\"$1\" -DGAB_DYNLIB_FILEENDING=\"$dynlib_fileending\" $GAB_CCFLAGS"

  echo "   $flags $platform ($platform_bundle)"

  echo "   Building static cgab library..."
  for file in src/cgab/*.c; do
    name=$(basename "$file" ".c")
    echo "      Building object $name..."
    zig cc $flags $platform -DGAB_CORE -c -o "build-$1/cgab$name.o" "$file" || exit 1
    echo "      $(file "build-$1/cgab$name.o")"
  done
  zig ar rcs "build-$1/libcgab.a" build-"$1"/*.o
  zig ranlib "build-$1/libcgab.a"
  echo "   Done!"
  echo "   $(file "build-$1/libcgab.a")"

  echo "   Building gab..."
  # Its important to link with cgab statically here. This is what
  # allows users to download the released binaries and install everything
  # they need from there.
  #
  # need flag -rdynamic to export all libcgab symbols in the gab executable.
  # This way they are available to gab modules written in c which are loaded at runtime.
  # any .dll or .so or .so modules, ie: everything built in the following step
  zig cc $flags $platform -rdynamic -DGAB_CORE -o "build-$1/gab" "build-$1/libcgab.a" src/gab/*.c || exit 1
  echo "   Done!"
  echo "   $(file "build-$1/gab")"

  mkdir -p "build-$1/mod"

  echo "   Building static shared library dependencies"
  build-libgrapheme "$1" "build-$1/libgrapheme.a"

  # Compile all src/mod c files into shared objects / dlls
  # These modules link *dynamically* to libcgab - they are loaded only at runtime by a process which is *already* exporting libcgab's symbols.
  # There *could* be some versioning issues but we're ignoring that for now.
  # The flag -undefined dynamic_lookup allows these symbols to remain undefined in the shared libraries we compile here. Normally, you'd
  # have to specify that they link to cgab somehow (by including it statically or with -lcgab). We can just leave them undefined for this use
  # case, and its actually ideal because of some nuances:
  # - linking statically would cause each so to carry its own libcgab. This is 3.2M of code for an otherwise tiny (20k) module.
  # - linking dynamically requires users to download libcgab.so, a dependency we are otherwise avoiding. This would defeat the purpose of
  #   linking the gab binary statically - I want the user to download the latest binary and be able to run it to do *everything*
  echo "    Building shared libraries..."
  for file in src/mod/*.c; do
    name=$(basename "$file" ".c")
    echo "       Building gab$name..."
    zig cc $platform_bundle  -undefined dynamic_lookup $flags $platform -o "build-$1/mod/$name$dynlib_fileending" -lgrapheme "$file" || exit 1
    echo "       Done!"
    echo "       $(file "build-$1/mod/$name$dynlib_fileending")"
  done
  echo "   Done!"


  echo "Success!"
}

export -f build
export -f build-libgrapheme

echo "Building dependency"
gen-libgrapheme

echo "Beginning compilation."
echo "$GAB_TARGETS"

echo "$GAB_TARGETS" | tr ' ' '\n' | parallel mkdir -p "build-{}" '&&' build "{}" '||' exit 1 '&&' echo "Built {}"
