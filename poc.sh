#!/bin/bash
set -e

echo "Cleaning /tmp build artifacts..."
rm -f /tmp/*.o /tmp/*.a /tmp/gabd-det

make -C vendor/unthread CC="zig cc --target=x86_64-linux-gnu" bin/unthread.o

# Necessary for musl.
# make -C vendor/ucontext CC="zig cc --target=x86_64-linux-musl" ARCH=x86_64

echo "Building cgab object files with musl..."
for f in src/cgab/*.c; do
  echo "  Compiling $(basename $f)..."
  zig cc -std=c23 -fPIC -Wall -c \
    --target=x86_64-linux-gnu \
    -fno-sanitize=undefined \
    -Iinclude -isystemvendor \
    -Ivendor/unthread/include \
    -DGAB_TARGET_TRIPLE=\"x86_64-linux-gnu\" \
    -DGAB_DYNLIB_FILEENDING=\".so\" \
    -DGAB_PLATFORM_UNIX -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
    -DGAB_PLATFORM_LINUX -DQIO_LINUX -DRGFW_USE_XDL \
    -isystem vendor/x11-headers \
    -o "/tmp/$(basename $f .c).o" \
    "$f"
done

echo "Archiving libcgab-gnu.a..."
zig ar rcs /tmp/libcgab-gnu.a /tmp/*.o

echo "Building gab object files with musl..."
for f in src/gab/*.c; do
  echo "  Compiling $(basename $f)..."
  zig cc -std=c23 -fPIC -Wall -c \
    --target=x86_64-linux-gnu \
    -fno-sanitize=undefined \
    -Iinclude -isystemvendor \
    -Ivendor/unthread/include \
    -DGAB_TARGET_TRIPLE=\"x86_64-linux-gnu\" \
    -DGAB_DYNLIB_FILEENDING=\".so\" \
    -DGAB_PLATFORM_UNIX -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
    -DGAB_PLATFORM_LINUX -DQIO_LINUX -DRGFW_USE_XDL \
    -isystem vendor/x11-headers \
    -o "/tmp/$(basename $f .c).o" \
    "$f"
done

echo "Linking gabd with unthread"
zig cc  \
  --target=x86_64-linux-gnu \
  -fno-sanitize=undefined \
  -Ivendor/unthread/include \
  -rdynamic \
  -o gabd \
  vendor/unthread/bin/unthread.o \
  /tmp/main.o \
  /tmp/libcgab-gnu.a

echo "Build complete!"
