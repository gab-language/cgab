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
#include "threads.h"

/**
 * PLATFORM INTERFACE
 * %--------------------------------%
 * | Dynamic-Shared Library Loading |
 * %--------------------------------%
 * GAB_DYNLIB_FILEENDING
 * The file ending for dynamic libraries on this platform
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
 * %-----------------%
 * | File Operations |
 * %-----------------%
 * gab_osfisatty(f)
 * Determine if a file stream *f* is a terminal (tty).
 *
 * gab_osfisready(f)
 * Check if a FILE* has data to be read.
 *
 * gab_osexepath()
 * return the path of the exe.
 *
 * %----------------------%
 * | Directory Operations |
 * %----------------------%
 * gab_osmkdirp(path)
 * Make a directory at path, if it doesn't exist.
 *
 * %---------------------%
 * | Spawn Sub-processes |
 * %---------------------%
 * gab_osproc(cmd, ...args)
 * Spawn a child process with the given command and arguments.
 *
 * %----------------------------%
 * | Gab/Platform Install Stuff |
 * %----------------------------%
 * gab_osprefix(version)
 * Determine the gab_prefix for the given operating system, with the given gab
 * version.
 *
 * %--------------------------%
 * | Platform Signal Hanlding |
 * %--------------------------%
 * gab_ossignal(signal, handler)
 * Assign a new signal handler *handler* for signal *signal*
 */

#define GAB_DYNLIB_MAIN "gab_lib"

#define GAB_API_INLINE static inline

#ifdef GAB_CORE
#define GAB_API [[__gnu__::__used__]]
#else
#define GAB_API extern
#endif

#define GAB_DYNLIB_MAIN_FN union gab_value_pair gab_lib(struct gab_triple gab)
#define GAB_DYNLIB_NATIVE_FN(module, name)                                     \
  union gab_value_pair gab_mod_##module##_##name(                              \
      struct gab_triple gab, uint64_t argc, gab_value *argv,                   \
      gab_value reentrant)

#define gab_osproc(cmd, ...)                                                   \
  ({                                                                           \
    char *_args[] = {__VA_ARGS__};                                             \
    gab_nosproc(cmd, sizeof(_args) / sizeof(char *), _args);                   \
  })

#define GAB_MAXEXEPATH 2048
static char _exepath[GAB_MAXEXEPATH];

#ifdef GAB_PLATFORM_UNIX
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define gab_osfileno(f) (fileno(f))
#define gab_osfisatty(f) isatty(fileno(f))

#define gab_ossignal(sig, handler) signal(sig, handler)

GAB_API_INLINE bool gab_osfisready(FILE *f) {
  struct pollfd fd = {.fd = fileno(f), .events = POLL_IN};
  return poll(&fd, 1, 0) > 0;
}

#ifdef GAB_PLATFORM_MACOS
#include <mach-o/dyld.h>
#endif

GAB_API_INLINE const char *gab_osexepath() {
#ifdef GAB_PLATFORM_MACOS
  ssize_t size = GAB_MAXEXEPATH;
  if (_NSGetExecutablePath(&_exepath, &size) != 0)
    return nullptr;

  return _exepath;
#elifdef GAB_PLATFORM_LINUX
  ssize_t len = readlink("/proc/self/exe", _exepath, GAB_MAXEXEPATH);

  if (len <= 0)
    return nullptr;

  _exepath[len] = '\0';

  return _exepath;
#else
#error "INVALID GAB UNIX PLATFORM"
#endif
}

#define gab_osdynlib void *
#define gab_oslibopen(path) dlopen(path, RTLD_NOW)
#define gab_oslibfind(dynlib, name) (void (*)(void)) dlsym(dynlib, name)

GAB_API_INLINE const bool gab_osmkdirp(const char *path) {
  int res = mkdir(path, 0755);

  if (res == 0)
    return true; // Directory created

  if (errno == EEXIST)
    return true; // Directory already existed

  printf("ERR: %s\n", strerror(errno));
  // Some other error occurred
  return false;
}

GAB_API_INLINE const char *gab_osprefix(const char *v) {
  char *home = getenv("HOME");

  if (!home)
    return nullptr;

  v_char str = {0};

  v_char_spush(&str, s_char_cstr(home));
  v_char_spush(&str, s_char_cstr("/gab/"));
  v_char_spush(&str, s_char_cstr(v));
  v_char_push(&str, '\0');

  return str.data;
}

GAB_API_INLINE int gab_nosproc(char *cmd, size_t nargs, char *args[]) {
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
  char *cmd_args[nargs + 2];

  cmd_args[0] = cmd;
  memcpy(cmd_args + 1, args, sizeof(const char *) * nargs);
  cmd_args[nargs + 1] = nullptr;

  /*printf("[CMD]: %s", cmd);*/
  /*for (size_t i = 1; i < nargs + 1; i++) {*/
  /*  printf(" %s", cmd_args[i]);*/
  /*}*/
  /*printf("\n");*/

  int code = execvp(cmd, cmd_args);

  if (code < 0)
    printf("[Error]: %s\n", strerror(errno));

  return 1;
}

#elifdef GAB_PLATFORM_WIN
#include <io.h>
#include <shlobj.h>
#include <signal.h>
#include <stdio.h>
#include <tchar.h>
#include <wchar.h>
#include <windows.h>

#define gab_ossignal(sig, handler) signal(sig, handler)

#define gab_osfisatty(f) _isatty(_fileno(f))

#define gab_osdynlib HMODULE
#define gab_oslibopen(path) LoadLibraryA(path)
#define gab_oslibfind(dynlib, name)                                            \
  ((void (*)(void))GetProcAddress(dynlib, name))
#define gab_osmkdirp(path) mkdir(path)

GAB_API_INLINE const char *gab_osexepath() {
  char buffer[GAB_MAXEXEPATH];
  DWORD len = GetModuleFileNameA(NULL, buffer, GAB_MAXEXEPATH);

  if (len == 0)
    return nullptr;

  mbstate_t state = {0};
  size_t length = wcsrtombs(NULL, &buffer, 0, &state);

  if (length == -1)
    return nullptr;

  if (length >= GAB_MAXEXEPATH)
    return nullptr;

  wcsrtombs(_exepath, &buffer, length, &state);

  return _exepath;
}

GAB_API_INLINE const char *gab_osprefix(const char *v) {
  PWSTR path = NULL;

  HRESULT status = SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &path);

  if (!SUCCEEDED(status))
    return nullptr;

  mbstate_t state = {0};
  size_t length = wcsrtombs(NULL, &path, 0, &state);

  if (length == -1)
    return nullptr;

  char buffer[length + 1];
  wcsrtombs(buffer, &path, length, &state);

  v_char str = {0};

  v_char_spush(&str, s_char_cstr(buffer));
  v_char_spush(&str, s_char_cstr("/gab/"));
  v_char_spush(&str, s_char_cstr(v));
  v_char_push(&str, '\0');

  return str.data;
}

GAB_API_INLINE bool gab_osfisready(FILE *f) {
  HANDLE *h = (HANDLE)_get_osfhandle(_fileno(f));
  DWORD result = WaitForSingleObject(h, 0);
  return result == WAIT_OBJECT_0;
}

GAB_API_INLINE int gab_nosproc(char *cmd, size_t nargs, char *args[]) {
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

#elifdef GAB_PLATFORM_WASI
#include <signal.h>

#define gab_osdynlib void *
#define gab_oslibopen(path) (nullptr)
#define gab_oslibfind(dynlib, name) (nullptr)
GAB_API_INLINE const char *gab_osprefix(const char *v) { return ""; }

#define gab_ossignal(sig, handler) signal(sig, handler)

GAB_API_INLINE int gab_nosproc(char *cmd, size_t nargs, char *args[]) {
  return 1;
}

GAB_API_INLINE const bool gab_osmkdirp(const char *path) { return false; }
#endif

#endif
