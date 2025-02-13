#ifndef GAB_PLATFORM_H
#define GAB_PLATFORM_H

/**
 * C11 Threads are not well supported cross-platforms.
 *
 * Even the c-standard-required feature macro __STDC_NOTHREADS__
 * isn't well supported.
 *
 * Instead of mucking about, just check for the standard threads with
 * a good 'ol __has_include.
 *
 * As of 2025, I believe only linux GNU is shipping this (at least in zig's
 * cross compiling toolchain).
 *
 * In the other cases, use our vendored, cthreads submodule.
 */
#if __has_include("threads.h")
#include <threads.h>
#else
#include <cthreads.h>
#endif

/**
 * PLATFORM INTERFACE
 *
 * GAB_DYNLIB_FILEENDING
 * The file ending for dynamic libraries on this platform
 *
 * gab_fisatty(f)
 * Determine if a file stream *f* is a terminal (tty).
 *
 * gab_dynlib
 * The type corresponding to a dynamically loaded library
 *
 * gab_libopen(path)
 * Open a dynamic library (c extension) at *path*.
 *
 * gab_libfind(dynlib, name)
 * Find a symbol *name* in the dynamic library *dynlib*.
 *
 */

#define GAB_DYNLIB_MAIN "gab_lib"
#define GAB_DYNLIB_MAIN_FN a_gab_value* gab_lib(struct gab_triple gab)

#ifdef GAB_PLATFORM_UNIX
#include <unistd.h>
#include <dlfcn.h>

#define gab_fisatty(f) isatty(fileno(f))

#define gab_dynlib void*
#define GAB_DYNLIB_FILEENDING ".so"
#define gab_libopen(path) dlopen(path, RTLD_LAZY)
#define gab_libfind(dynlib, name) dlsym(dynlib, name)

#elifdef GAB_PLATFORM_WIN
#include <io.h>
#include <windows.h>

#define gab_fisatty(f) _isatty(_fileno(f))

#define gab_dynlib HMODULE
#define GAB_DYNLIB_FILEENDING ".dll"
#define gab_libopen(path) LoadLibrary(path)
#define gab_libfind(dynlib, name) ((void*)GetProcAddress(dynlib, path))

#endif

#endif
