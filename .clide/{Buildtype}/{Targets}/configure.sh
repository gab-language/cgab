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

echo "#!/usr/bin/env bash" >> configuration
echo "export GAB_CCFLAGS=\""$cflags"\"" >> configuration
echo "export GAB_TARGETS=\""$targets"\"" >> configuration
chmod +x configuration

echo "Success!"
