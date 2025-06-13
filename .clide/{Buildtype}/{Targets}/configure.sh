#!/usr/bin/env bash

echo "Beginning configuration."
echo "  <buildtype>"
echo "$buildtype"
echo "  <targets>"
echo "$targets"


cd "$CLIDE_PATH/../" || exit

rm -f configuration # Remove the conf file if it exists

export cflags

# Create a small script which will export the variables we need, and then 
case "$buildtype" in
  debug)          cflags="-g -O0 -fsanitize=address,undefined" ;;
  debugoptimized) cflags="-g -O2" ;;
  release)        cflags="-O3 -DNDEBUG"    ;;
esac

unixflags="-DGAB_PLATFORM_UNIX -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE"
winflags="-DGAB_PLATFORM_WIN"
dynlib_fileending=""

if [[ "$targets" =~ "linux" ]]; then
  cflags="$cflags $unixflags -DQIO_LINUX -DRGFW_USE_XDL -isystem vendor/x11-headers"
  dynlib_fileending=".so"
elif [[ "$targets" =~ "mac" ]]; then
  cflags="$cflags $unixflags -DQIO_MACOS -DRGFW_NO_IOKIT -isystem vendor/xcode-frameworks/include"
  dynlib_fileending=".so"
elif [[ "$targets" =~ "windows" ]]; then
  cflags="$cflags $winflags -DQIO_WINDOWS -DOEMRESOURCE"
  dynlib_fileending=".dll"
fi

echo "#!/usr/bin/env bash" >> configuration
echo "export GAB_CCFLAGS=\""$cflags"\"" >> configuration
echo "export GAB_TARGETS=\""$targets"\"" >> configuration
echo "export GAB_DYNLIB_FILEENDING=\""$dynlib_fileending"\"" >> configuration
echo "mkdir -p build-$targets" >> configuration
echo "mkdir -p build-$targets/mod" >> configuration

chmod +x configuration

echo "Success!"
