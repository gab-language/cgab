#!/usr/bin/env bash

echo "Beginning configuration."
echo "  <buildtype>"
echo "$buildtype"
echo "  <targets>"
echo "$targets"

cd "$CLIDE_PATH/../" || exit

rm -f configuration # Remove the conf file if it exists

export cflags
export binflags

# Create a small script which will export the variables we need, and then 
case "$buildtype" in
  debug)          cflags="-g -O0 -fsanitize=address,undefined,leak,memory -DcGAB_THREADS_NATIVE" ;;
  debugoptimized) cflags="-g -O2 -DcGAB_THREADS_NATIVE -DNDEBUG" ;;
  deterministic)  cflags="-g -O0 -fsanitize=address,undefined,leak,memory -Ivendor/unthread/include" ;;
  deterministicoptimized)  cflags="-g -O2 -Ivendor/unthread/include" ;;
  release)        cflags="-Os -DcGAB_THREADS_NATIVE -DNDEBUG"    ;;
esac

# For the deterministic build, we have to include the unthread.o library.
case "$buildtype" in
  debug)          binflags="" ;;
  debugoptimized) binflags="" ;;
  deterministic)  binflags="vendor/unthread/bin/unthread.o" ;;
  deterministicoptimized)  binflags="vendor/unthread/bin/unthread.o" ;;
  release)        binflags=""    ;;
esac

export unixflags="-DGAB_PLATFORM_UNIX -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE"
export winflags="-DGAB_PLATFORM_WIN"
export wasmflags="-DGAB_PLATFORM_WASI -D_WASI_EMULATED_SIGNAL -lwasi-emulated-signal -D_WASI_EMULATED_PROCESS_CLOCKS -lwasi-emulated-process-clocks"
export dynlib_fileending=""

function configure_target() {
  if [[ "$1" =~ "linux" ]]; then
    cflags="$cflags $unixflags -DGAB_PLATFORM_LINUX -isystem vendor/x11-headers"
    dynlib_fileending=".so"
  elif [[ "$1" =~ "mac" ]]; then
    cflags="$cflags $unixflags -D_DARWIN_C_SOURCE=1 -DGAB_PLATFORM_MACOS -isystem vendor/xcode-frameworks/include -L vendor/xcode-frameworks/lib -F vendor/xcode-frameworks/Frameworks"
    dynlib_fileending=".dylib"
  elif [[ "$1" =~ "windows" ]]; then
    cflags="$cflags $winflags -DOEMRESOURCE"
    dynlib_fileending=".dll"
  elif [[ "$1" =~ "wasm" ]]; then
    cflags="$cflags $wasmflags -DQIO_LINUX"
    dynlib_fileending=".so"
  fi

  echo "#!/usr/bin/env bash" >> "$1.configuration"
  echo "mkdir -p build-$1" >> "$1.configuration"
  echo "mkdir -p build-$1/mod" >> "$1.configuration"
  echo "export GAB_CCFLAGS=\""$cflags"\"" >> "$1.configuration"
  echo "export GAB_BINARYFLAGS=\""$binflags"\"" >> "$1.configuration"
  echo "export GAB_TARGETS=\""$1"\"" >> "$1.configuration"
  echo "export GAB_BUILDTYPE=\""$buildtype"\"" >> "$1.configuration"
  echo "export GAB_DYNLIB_FILEENDING=\""$dynlib_fileending"\"" >> "$1.configuration"

  echo "export chttp_FLAGS=\"-lllhttp\"" >> "$1.configuration"
  echo "export cstrings_FLAGS=\"-lgrapheme\"" >> "$1.configuration"

  if [[ "$1" =~ "linux" ]]; then
    echo "export cgui_FLAGS=\"-DRGFW_USE_XDL\"" >> "$1.configuration"
    echo "export cio_FLAGS=\"-lbearssl -DQIO_LINUX\"" >> "$1.configuration"
  elif [[ "$1" =~ "windows" ]]; then
    echo "export cgui_FLAGS=\"\"" >> "$1.configuration"
    echo "export cio_FLAGS=\"-lbearssl -DQIO_WINDOWS\"" >> "$1.configuration"
  elif [[ "$1" =~ "mac" ]]; then
    echo "export cgui_FLAGS=\"-DRGFW_NO_IOKIT -framework Cocoa\"" >> "$1.configuration"
    echo "export cio_FLAGS=\"-lbearssl -DQIO_MACOS\"" >> "$1.configuration"
  fi

  chmod +x "$1.configuration"
}
export -f configure_target

echo $targets | tr ' ' '\n' |  parallel configure_target  || exit 1

echo "Success!"
