#ifndef GAB_PLATFORM_H
#define GAB_PLATFORM_H

#include "core.h"

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
 * In the other cases, use our vendored, cthreads submodule as a cross platform
 * replacement until c11 threads is supported.
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
 * gab_osdynlib
 * The type corresponding to a dynamically loaded library
 *
 * gab_oslibopen(path)
 * Open a dynamic library (c extension) at *path*.
 *
 * gab_oslibfind(dynlib, name)
 * Find a symbol *name* in the dynamic library *dynlib*.
 *
 * gab_osproc(cmd, ...args)
 * Spawn a child process with the given command and arguments.
 *
 * gab_osprefix()
 * Determine the gab_prefix for the given operating system.
 */

#define GAB_DYNLIB_MAIN "gab_lib"
#define GAB_DYNLIB_MAIN_FN a_gab_value *gab_lib(struct gab_triple gab)

#define gab_osproc(cmd, ...)                                                   \
  ({                                                                           \
    char *_args[] = {__VA_ARGS__};                                       \
    gab_nosproc(cmd, sizeof(_args) / sizeof(const char *), _args);             \
  })

#ifdef GAB_PLATFORM_UNIX
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define gab_fisatty(f) isatty(fileno(f))

#define gab_osdynlib void *
#define GAB_DYNLIB_FILEENDING ".so"
#define gab_oslibopen(path) dlopen(path, RTLD_NOW)
#define gab_oslibfind(dynlib, name) dlsym(dynlib, name)

static const char *gab_osprefix() {
  char *home = getenv("HOME");

  if (!home)
    return nullptr;

  return home;
}

static int gab_nosproc(char *cmd, size_t nargs, const char *args[]) {
  pid_t pid = fork();

  if (pid < 0)
    return 1;

  if (pid > 0) {
    for (;;) {
      int status = 0;
      if (waitpid(pid, &status, 0) < 0) {
        printf("[ERROR]: Could not wait for child. %s\n", strerror(errno));
        return 1;
      }

      if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        /*printf("[INFO]: %s completed with status code %d\n", cmd, code);*/
        return code;
      }

      if (WIFSIGNALED(status)) {
        int code = WTERMSIG(status);
        /*printf("[INFO]: %s exited with status code %d\n", cmd, code);*/
        return code;
      }
    }
  }

  // This implicitly appends a null-value, as it is initialized to zero.
  char *cmd_args[nargs + 2] = {};

  cmd_args[0] = cmd;
  memcpy(cmd_args + 1, args, sizeof(const char *) * nargs);
  cmd_args[nargs + 1] = nullptr;

  printf("[CMD]: %s", cmd);
  for (size_t i = 1; i < nargs + 1; i++) {
    printf(" %s", cmd_args[i]);
  }
  printf("\n");

  int code = execvp(cmd, cmd_args);

  printf("Failed to execute command\n");
  if (code < 0)
    printf("Error: %s\n", strerror(errno));

  return 1;
}

#elifdef GAB_PLATFORM_WIN
#include <io.h>
#include <shlobj.h>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>

#define gab_fisatty(f) _isatty(_fileno(f))

#define gab_osdynlib HMODULE
#define GAB_DYNLIB_FILEENDING ".dll"
#define gab_oslibopen(path) LoadLibrary(path)
#define gab_oslibfind(dynlib, name) ((void *)GetProcAddress(dynlib, path))

static const char *gab_osprefix() {
  PWSTR path = NULL;

  HRESULT status =
      SHGetKnownFolderPath(&FOLDERID_AppDataProgramData, 0, NULL, &path);

  if (SUCCEEDED(status))
    return path;

  return nullptr;
}

static int gab_nosproc(const char *cmd, size_t nargs, const char *args[]) {
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  v_char sb = {};

  v_char_spush(&sb, s_char_cstr(cmd));
  v_char_push(&sb, ' ');

  for (size_t i = 0; i < nargs; i++) {
    v_char_spush(&sb, s_char_cstr(args[i]));
    v_char_push(&sb, ' ');
  }

  v_char_push(&sb, '\0');

  // Start the child process.
  if (!CreateProcess(NULL,    // No module name (use command line)
                     sb.data, // Command line
                     NULL,    // Process handle not inheritable
                     NULL,    // Thread handle not inheritable
                     FALSE,   // Set handle inheritance to FALSE
                     0,       // No creation flags
                     NULL,    // Use parent's environment block
                     NULL,    // Use parent's starting directory
                     &si,     // Pointer to STARTUPINFO structure
                     &pi)     // Pointer to PROCESS_INFORMATION structure
  ) {
    return 1;
  }

  // Wait until child process exits.
  WaitForSingleObject(pi.hProcess, INFINITE);

  // Close process and thread handles.
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 0;
}

#endif

#endif
