#include "miniz/amalgamation/miniz.c"
#include "miniz/amalgamation/miniz.h"

#include "gab.h"
#include "platform.h"
#include <locale.h>
#include <stddef.h>

#include "crossline/crossline.c"
#include "crossline/crossline.h"

#include <stdio.h>

#define T struct gab_package
#define NAME pkg
#include "vector.h"

#define TOSTRING(x) #x
#define STR(x) TOSTRING(x)

#define MAIN_MODULE "gab\\main"

#ifdef GAB_PLATFORM_UNIX
#define GAB_SYMLINK_RECOMMENDATION "ln -sf %sgab /usr/local/bin"
#elifdef GAB_PLATFORM_WIN
#define GAB_SYMLINK_RECOMMENDATION                                             \
  "New-Item -ItemType SymbolicLink -Path %s\gab -Target "                      \
  "\"Some\\Directory\\In\\Path\""
#elifdef GAB_PLATFORM_WASI
#define GAB_SYMLINK_RECOMMENDATION ""
#endif

struct gab_triple gab;

mz_zip_archive zip = {0};

/*
 * OS Signal handler for when SIGINT is caught
 */
void propagate_term(int) { gab_sigterm(gab); }

void clierror(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "[" GAB_RED "gab" GAB_RESET "] ");
  vfprintf(stderr, fmt, args);

  va_end(args);
}

void clisuccess(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "[" GAB_GREEN "gab" GAB_RESET "] ");
  vfprintf(stderr, fmt, args);

  va_end(args);
}

void cliwarn(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "[" GAB_YELLOW "gab" GAB_RESET "] ");
  vfprintf(stderr, fmt, args);

  va_end(args);
}

void cliinfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "[gab] ");
  vfprintf(stderr, fmt, args);

  va_end(args);
}

void print_err(struct gab_triple gab, gab_value err) {
  const char *str = gab_errtocs(gab, err);
  fprintf(stderr, "%s\n", str);
}

void pop_and_printerr(struct gab_triple gab) {
  while (gab_signaling(gab))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigpropagate(gab);
      break;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      continue;
    }

  gab_value *errors = gab_egerrs(gab.eg);

  if (!errors)
    return;

  for (gab_value *err = errors; *err != gab_nil; err++) {
    assert(gab_valkind(*err) == kGAB_RECORD);
    const char *errstr = gab_errtocs(gab, *err);
    assert(errstr != nullptr);

    if (errstr)
      fputs(errstr, stderr);
  };

  free(errors);
}

bool check_and_printerr(union gab_value_pair *res) {
  if (res->status == gab_ctimeout)
    *res = gab_fibawait(gab, res->vresult);

  if (!gab.eg)
    return false;

  while (gab_signaling(gab))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigpropagate(gab);
      break;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      continue;
    }

  // if (res->status == gab_cvalid && res->aresult->data[0] != gab_ok)
  //   while (gab_egalive(gab.eg) > 1) // The GC thread will stay alive
  //     gab_busywait(gab);

  pop_and_printerr(gab);

  if (res->status != gab_cvalid) {
    if (res->status == gab_cinvalid && res->vresult &&
        res->vresult != gab_cinvalid) {
      assert(gab_valkind(res->vresult) == kGAB_RECORD);
      const char *errstr = gab_errtocs(gab, res->vresult);
      assert(errstr != nullptr);

      if (errstr) {
        fputs(errstr, stderr);
        fflush(stderr);
      }
    }

    return false;
  }

  if (res->aresult->data[0] != gab_ok) {
    const char *errstr = gab_errtocs(gab, res->aresult->data[1]);
    assert(errstr != nullptr);
    fputs(errstr, stderr);
    fflush(stderr);
    return a_gab_value_destroy(res->aresult), false;
  }

  return true;
}

int copy_file(FILE *in, FILE *out) {
  char buffer[8192]; // 8 KB buffer
  size_t n;

  while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
    if (fwrite(buffer, 1, n, out) != n) {
      return -1; // write error
    }
  }

  if (ferror(in)) {
    return -1; // read error
  }

  return 0; // success
}

/*
 * TODO @cgab @api: Think about how module requiring works, and try to make it
 * consistent to bundles, importing libraries, etc.
 *
 * Instead of having one root for ".", we might need to add some sort of notion
 * for the package you're in.
 * - Its possible we should try and detect that inmain here, and add a root for
 * the cwd as a package
 *
 * - Import files like 'github.com/gab-language/cgab@0.0.5/mod/cstrings'.use
 *   -> This is kind of ugly. Maybe 'cstrings' .use (from:
 * 'github.com/gab-language/cgab@0.0.5)
 *   -> Or Maybe: 'github.com/gab-language/cgab@0.0.5' .use 'cstrings'
 */

#ifdef GAB_PLATFORM_WIN
typedef union gab_value_pair (WINAPI*dynlib_fn)(struct gab_triple);
#else
typedef union gab_value_pair (*dynlib_fn)(struct gab_triple);
#endif

union gab_value_pair gab_use_dynlib(struct gab_triple gab, const char *path,
                                    size_t len, const char **sargs,
                                    gab_value *vargs) {
  gab_osdynlib lib = gab_oslibopen(path);

  if (lib == nullptr) {
#ifdef GAB_PLATFORM_UNIX
    return gab_panicf(gab, "Failed to load module '$': $",
                      gab_string(gab, path), gab_string(gab, dlerror()));
#elifdef GAB_PLATFORM_WASI
    return gab_panicf(gab, "Failed to load module '$'", gab_string(gab, path));
#elifdef GAB_PLATFORM_WIN
    {
      int error = GetLastError();
      char buffer[128];
      if (FormatMessageA(
              FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
              error, 0, buffer, sizeof(buffer) / sizeof(char), NULL))
        return gab_panicf(gab, "Failed to load module '$': $",
                          gab_string(gab, path), gab_string(gab, buffer));

      return gab_panicf(gab, "Failed to load module '$'",
                        gab_string(gab, path));
    }
#endif
  }

  dynlib_fn mod = gab_oslibfind(lib, GAB_DYNLIB_MAIN);

  if (mod == nullptr)
#ifdef GAB_PLATFORM_UNIX
    return gab_panicf(gab, "Failed to load module '$': $",
                      gab_string(gab, path), gab_string(gab, dlerror()));
#elifdef GAB_PLATFORM_WASI
    return gab_panicf(gab, "Failed to load module '$'", gab_string(gab, path));
#elifdef GAB_PLATFORM_WIN
  {
    int error = GetLastError();
    char buffer[128];
    if (FormatMessageA(
            FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
            error, 0, buffer, sizeof(buffer) / sizeof(char), NULL))
      return gab_panicf(gab, "Failed to load module '$': $",
                        gab_string(gab, path), gab_string(gab, buffer));

    return gab_panicf(gab, "Failed to load module '$'", gab_string(gab, path));
  }
#endif

  union gab_value_pair res = mod(gab);

  // At this point, mod should have reported any errors.
  /*gab.flags |= fGAB_ERR_QUIET;*/

  if (res.status != gab_cvalid)
    return gab_panicf(gab, "Failed to load c module.");

  if (res.aresult->data[0] != gab_ok)
    return gab_panicf(gab,
                      "Failed to load module: module returned $, expected $",
                      res.aresult->data[0], gab_ok);

  if (gab_segmodput(gab.eg, path, res.aresult) == nullptr)
    return gab_panicf(gab, "Failed to cache c module.");

  return res;
}

union gab_value_pair gab_use_zip_dynlib(struct gab_triple gab, const char *path,
                                        size_t len, const char **sargs,
                                        gab_value *vargs) {
  int idx = mz_zip_reader_locate_file(&zip, path, "", 0);
  if (idx < 0)
    return gab_panicf(gab, "Failed to load module");

  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip, idx, &stat)) {
    mz_zip_error e = mz_zip_get_last_error(&zip);
    const char *estr = mz_zip_get_error_string(e);
    return gab_panicf(gab, "Failed to load module: $", gab_string(gab, estr));
  };

  v_char temppath = {0};
  v_char_spush(&temppath, s_char_cstr("gab@" GAB_VERSION_TAG "/"));
  v_char_spush(&temppath, s_char_cstr(path));
  v_char_push(&temppath, '\0');

  char *dst = gab_osprefix_temp(temppath.data);

  FILE *tried = fopen(dst, "r");
  if (tried) {
    fclose(tried);
    goto exists;
  }

  char *slash = strrchr(dst, '/');
  *slash = '\0';

  int result = gab_osmkdirp(dst);

  if (result)
    return gab_panicf(gab, "Failed to create temporary file folder: $.",
                      gab_string(gab, dst));

  *slash = '/';

  /**
   * Should also check if the file exists, and then we don't need to do
   * extraction. Maybe we should just extract in memory, and then compare the
   * two byte-for-byte to ensure they are the same.
   *
   * TODO @cgab @cli: This should really be organized per-process, as multiple
   * gab-apps can be opened at the same time, and we don't want them to stomp
   * over each other. *maybe* this is fixed by checking if the file exists
   * first.
   */

  if (!mz_zip_reader_extract_file_to_file(&zip, stat.m_filename, dst, 0)) {
    mz_zip_error e = mz_zip_get_last_error(&zip);
    const char *estr = mz_zip_get_error_string(e);
    return gab_panicf(gab, "Failed to load zipped module to $: $",
                      gab_string(gab, dst), gab_string(gab, estr));
  }

exists:
  union gab_value_pair res = gab_use_dynlib(gab, dst, len, sargs, vargs);

  free(dst);

  return res;
}

union gab_value_pair gab_use_zip_source(struct gab_triple gab, const char *path,
                                        size_t len, const char **sargs,
                                        gab_value *vargs) {
  int idx = mz_zip_reader_locate_file(&zip, path, "", 0);
  if (idx < 0)
    return gab_panicf(gab, "Failed to load module");

  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip, idx, &stat)) {
    mz_zip_error e = mz_zip_get_last_error(&zip);
    const char *estr = mz_zip_get_error_string(e);
    return gab_panicf(gab, "Failed to load module: $", gab_string(gab, estr));
  };

  size_t sz;
  void *src = mz_zip_reader_extract_file_to_heap(&zip, stat.m_filename, &sz, 0);

  if (!src) {
    mz_zip_error e = mz_zip_get_last_error(&zip);
    const char *estr = mz_zip_get_error_string(e);
    return gab_panicf(gab, "Failed to load module: $", gab_string(gab, estr));
  }

  union gab_value_pair fiber = gab_exec(gab, (struct gab_exec_argt){
                                                 .name = path,
                                                 .source_len = sz,
                                                 .source = (const char *)src,
                                                 .flags = gab.flags,
                                                 .len = len,
                                                 .sargv = sargs,
                                                 .argv = vargs,
                                             });
  a_char_destroy(src);

  return fiber;
}

union gab_value_pair gab_use_source(struct gab_triple gab, const char *path,
                                    size_t len, const char **sargs,
                                    gab_value *vargs) {
  a_char *src = gab_osread(path);

  if (src == nullptr) {
    gab_value reason = gab_string(gab, strerror(errno));
    return gab_panicf(gab, "Failed to load module: $", reason);
  }

  union gab_value_pair fiber =
      gab_exec(gab, (struct gab_exec_argt){
                        .name = path,
                        .source = (const char *)src->data,
                        .flags = gab.flags,
                        .len = len,
                        .sargv = sargs,
                        .argv = vargs,
                    });

  a_char_destroy(src);

  return fiber;
}

bool file_exister(const char *path) {

  FILE *f = fopen(path, "r");

  if (f)
    fclose(f);

  return f != nullptr;
}

bool zip_exister(const char *path) {
  int res = mz_zip_reader_locate_file(&zip, path, nullptr, 0);
  return res >= 0;
}

static struct gab_package default_modules[] = {
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Shapes"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Messages"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Strings"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Binaries"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Numbers"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Blocks"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Records"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Fibers"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Channels"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Ranges"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Transducers"},
    {"github.com/gab-language/cgab@" GAB_VERSION_TAG, "Io"},
    {}, // List terminator.
};
static const size_t ndefault_modules = LEN_CARRAY(default_modules) - 1;

#define GAB_NATIVE_MODULE_SUFFIX                                               \
  ".cgab-" GAB_VERSION_TAG "-" GAB_TARGET_TRIPLE GAB_DYNLIB_FILEENDING

static const struct gab_resource native_file_resources[] = {
    {"", GAB_NATIVE_MODULE_SUFFIX, gab_use_dynlib, file_exister},
    {"mod/", GAB_NATIVE_MODULE_SUFFIX, gab_use_dynlib, file_exister},

    {"", "mod.gab", gab_use_source, file_exister},
    {"", ".gab", gab_use_source, file_exister},

    {"mod/", "mod.gab", gab_use_source, file_exister},
    {"mod/", ".gab", gab_use_source, file_exister},

    {}, // List terminator.
};

static const size_t nnative_file_resources =
    LEN_CARRAY(native_file_resources) - 1;

static const struct gab_resource native_zip_resources[] = {
    {"", GAB_NATIVE_MODULE_SUFFIX, gab_use_zip_dynlib, zip_exister},
    {"mod/", GAB_NATIVE_MODULE_SUFFIX, gab_use_zip_dynlib, zip_exister},

    {"", "mod.gab", gab_use_zip_source, zip_exister},
    {"", ".gab", gab_use_zip_source, zip_exister},

    {"mod/", "mod.gab", gab_use_zip_source, zip_exister},
    {"mod/", ".gab", gab_use_zip_source, zip_exister},

    {}, // List terminator.
};

static const char *roots[4] = {};

static char prompt_buffer[4096];
char *readline(const char *prompt) {
  return crossline_readline(prompt, prompt_buffer, sizeof(prompt_buffer));
}

const char *welcome_message = "  ________   ___  |\n"
                              " / ___/ _ | / _ ) | v" GAB_VERSION_TAG "\n"
                              "/ (_ / __ |/ _  | |  on: " GAB_TARGET_TRIPLE "\n"
                              "\\___/_/ |_/____/  |  in: " GAB_BUILDTYPE "\n";

int run_repl(int flags, uint32_t wait, size_t nmodules,
             struct gab_package *packages) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .jobs = 8,
          .wait = wait,
          .packages = packages,
          .roots = roots,
          .resources = native_file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  const char *sargs[res.aresult->len];
  for (int i = 0; i < res.aresult->len; i++)
    sargs[i] = packages[i].alias    ? packages[i].alias
               : packages[i].module ? packages[i].module
                                    : packages[i].package;

  gab_repl(gab, (struct gab_repl_argt){
                    .name = MAIN_MODULE,
                    .flags = flags,
                    .welcome_message = welcome_message,
                    .prompt_prefix = ">>> ",
                    .promptmore_prefix = "|   ",
                    .result_prefix = "",
                    .readline = readline,
                    .len = nmodules,
                    .sargv = sargs,
                    .argv = res.aresult->data + 1, // Skip initial ok:
                });

  a_gab_value_destroy(res.aresult);

  return gab_destroy(gab), 0;
}

int run_string(const char *string, int flags, uint32_t wait, size_t jobs,
               size_t nmodules, struct gab_package *packages) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .wait = wait,
          .jobs = jobs,
          .packages = packages,
          .roots = roots,
          .resources = native_file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 0;

  const char *sargs[res.aresult->len];
  for (int i = 0; i < res.aresult->len; i++)
    sargs[i] = packages[i].alias    ? packages[i].alias
               : packages[i].module ? packages[i].module
                                    : packages[i].package;

  // This is a weird case where we actually want to include the null terminator
  s_char src = s_char_create(string, strlen(string) + 1);

  union gab_value_pair run_res =
      gab_exec(gab, (struct gab_exec_argt){
                        .name = MAIN_MODULE,
                        .source = (char *)src.data,
                        .flags = flags,
                        .len = nmodules,
                        .sargv = sargs,
                        .argv = res.aresult->data + 1,
                    });

  a_gab_value_destroy(res.aresult);

  if (!check_and_printerr(&run_res))
    return gab_destroy(gab), 1;

  return a_gab_value_destroy(run_res.aresult), gab_destroy(gab), 0;
}

int run_bundle(const char *mod) {
  gab_ossignal(SIGINT, propagate_term);

  size_t len = strlen(mod);

  if (len > 4 && !strncmp(mod + len - 4, ".exe", 4))
    len -= 4;

  // Scans backwards from the extension to the first '\' or '/'
  // This pulls the last component of a path out.
  size_t modlen = 0;
  for (size_t i = len - 1; i > 0; i--) {
    modlen++;
    if (mod[i - 1] == '\\' || mod[i - 1] == '/') {
      mod = mod + i;
      break;
    }
  }

  // Scan for a '.' in the remaining name.
  const char *dot = strchr(mod, '.');

  if (dot && dot - mod < modlen)
    modlen = dot - mod;

  struct gab_package *packages = default_modules;

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .jobs = 8,
          .packages = packages,
          /* Unique to bundled apps, the only root is for *within* the bundle.
           */
          .roots =
              (const char *[]){
                  "",
                  nullptr,
              },
          .resources = native_zip_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  const char *sargs[res.aresult->len];
  for (int i = 0; i < res.aresult->len; i++)
    sargs[i] = packages[i].alias    ? packages[i].alias
               : packages[i].module ? packages[i].module
                                    : packages[i].package;

  union gab_value_pair run_res =
      gab_use(gab, (struct gab_use_argt){
                       .vpackage_name = gab_nstring(gab, modlen, mod),
                       .len = ndefault_modules,
                       .sargv = sargs,
                       .argv = res.aresult->data + 1,
                   });

  if (!check_and_printerr(&run_res))
    return gab_destroy(gab), 1;

  gab_sigterm(gab);

  a_gab_value_destroy(res.aresult);

  return gab_destroy(gab), 0;
}

int run_file(const char *path, int flags, uint32_t wait, size_t jobs,
             size_t nmodules, struct gab_package *packages) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .wait = wait,
          .jobs = jobs,
          .packages = packages,
          .roots = roots,
          .resources = native_file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  const char *sargs[res.aresult->len];
  for (int i = 0; i < res.aresult->len; i++)
    sargs[i] = packages[i].alias    ? packages[i].alias
               : packages[i].module ? packages[i].module
                                    : packages[i].package;

  union gab_value_pair run_res = gab_use(gab, (struct gab_use_argt){
                                                  .flags = flags,
                                                  .spackage_name = path,
                                                  .len = nmodules,
                                                  .sargv = sargs,
                                                  .argv = res.aresult->data + 1,
                                              });

  a_gab_value_destroy(res.aresult);

  if (!check_and_printerr(&run_res))
    return gab_destroy(gab), 1;

  return a_gab_value_destroy(run_res.aresult), gab_destroy(gab), 0;
}

bool add_package(mz_zip_archive *zip_o, const char **roots,
                 const struct gab_resource *resources, s_char package,
                 struct gab_module_res *mod) {

  char cstr_package[package.len + 1];
  memcpy(cstr_package, package.data, package.len);
  cstr_package[package.len] = '\0';

  *mod = gab_mresolve(roots, resources, cstr_package, nullptr);

  if (!mod->path)
    return false;

  /*
   * Detect if the module we've found is already a bundle.
   * If it is, flatten it into this bundle.
   */
  mz_zip_archive zip_r = {0};
  if (mz_zip_reader_init_file(&zip_r, mod->path->data, 0)) {
    size_t files = mz_zip_reader_get_num_files(&zip_r);

    if (files) {
      for (size_t i = 0; i < files; i++)
        if (!mz_zip_writer_add_from_zip_reader(zip_o, &zip_r, i))
          return false; // TODO @cgab @cli: Log this err

      return true;
    }
  }

  // TODO @cli @qol: Allow the user to configure the SPEED/COMPRESSION tradeoff
  // here.
  return mz_zip_writer_add_file(zip_o, mod->package_path, mod->path->data,
                                nullptr, 0, MZ_BEST_SPEED);
}

struct step {
  enum {
    kSTEP_NONE,
    kSTEP_MKDIRP,
    kSTEP_FETCH,
    kSTEP_EXTRACT,
    kSTEP_UNZIP,
    kSTEP_RM,
    kSTEP_ARCHIVE_OPEN,
    kSTEP_ARCHIVE_ADD_PACKAGE,
    kSTEP_ARCHIVE_FINALIZE,
  } k;

  union {
    struct {
      const char *path;
      mz_zip_archive *zip;
      const char *initial_data_path;
      const char *initial_data_fallback_path;
    } archive_open;

    struct {
      mz_zip_archive *zip;
      const char **roots;
      const struct gab_resource *resources;
      s_char package;
      struct gab_module_res mod_out;
    } archive_add_package;

    struct {
      mz_zip_archive *zip;
      long *size_out;
    } archive_finalize;

    struct {
      const char *path;
    } mkdirp;

    struct {
      const char *path;
    } rm;

    struct {
      const char *url;
      const char *dst;
    } fetch;

    struct {
      const char *src;
      const char *dst;
    } extract;

    struct {
      const char *src;
      const char *dst;
    } unzip;
  } as;
};

#define T struct step
#define NAME step
#include "vector.h"

int step(struct step *step) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return 1;
  case kSTEP_MKDIRP:
    return gab_osmkdirp(step->as.mkdirp.path);
  case kSTEP_RM:
    return gab_osrm(step->as.rm.path);
  case kSTEP_FETCH:
    return gab_osproc("curl", "-f", "-s", "-L", "-o", step->as.fetch.dst,
                      step->as.fetch.url);
  case kSTEP_EXTRACT:
    return gab_osproc("tar", "xzf", step->as.extract.src, "-C",
                      step->as.extract.dst);
  case kSTEP_UNZIP: {
    mz_zip_archive zip_o = {0};
    if (!mz_zip_reader_init_file(&zip_o, step->as.unzip.src, 0))
      return 1;

    uint64_t nfiles = mz_zip_reader_get_num_files(&zip_o);

    for (uint64_t n = 0; n < nfiles; n++) {
      mz_zip_archive_file_stat stat;
      if (!mz_zip_reader_file_stat(&zip_o, n, &stat))
        return 1;

      if (stat.m_is_directory)
        continue;

      /*
       * Each filename should begin with the same prefix as in *dst*.
       *
       * For example, the package `github.com/gab-language/cgab@0.0.5`
       *
       * will resolve to url, which will fetch a bundle `cgab-<gab
       * version>-<platform-triple>`
       *
       * This bundle is a zip file, which contains the modules served in the
       * package.
       *
       * These modules should start with a path which matches the package name.
       *
       * `github.com/gab-language/cgab@0.0.5/<module>`
       */
      if (memcmp(step->as.unzip.dst, stat.m_filename,
                 strlen(step->as.unzip.dst)))
        return 2;

      v_char filename = {0};
      v_char_spush(&filename, s_char_cstr(stat.m_filename));
      v_char_push(&filename, '\0');

      /*
       * Replace the last trailing '/' with a null byte.
       *
       * This gives us the path of the parent directory to the filename.
       *
       * Make sure all directories in this path exist.
       *
       * Then replace the null byte with the original '/'
       */
      char *slash = strrchr(filename.data, '/');
      *slash = '\0';
      gab_osmkdirp(filename.data);
      *slash = '/';

      if (!mz_zip_reader_extract_file_to_file(&zip_o, stat.m_filename,
                                              filename.data, 0))
        return 2;

      v_char_destroy(&filename);
    }

    return 0;
  }
  case kSTEP_ARCHIVE_OPEN: {
    FILE *archive = fopen(step->as.archive_open.path, "w");

    if (!archive)
      return 1;

    if (step->as.archive_open.initial_data_path) {
      FILE *f = fopen(step->as.archive_open.initial_data_path, "r");

      if (!f && step->as.archive_open.initial_data_fallback_path)
        f = fopen(step->as.archive_open.initial_data_fallback_path, "r");

      if (!f)
        return 1;

      int res = copy_file(f, archive);

      if (res)
        return res;

      fclose(f);
    }

    return !mz_zip_writer_init_cfile(step->as.archive_open.zip, archive, 0);
  }
  case kSTEP_ARCHIVE_ADD_PACKAGE:
    return !add_package(step->as.archive_add_package.zip,
                        step->as.archive_add_package.roots,
                        step->as.archive_add_package.resources,
                        step->as.archive_add_package.package,
                        &step->as.archive_add_package.mod_out);
  case kSTEP_ARCHIVE_FINALIZE: {
    mz_zip_archive *zip = step->as.archive_finalize.zip;
    FILE *bundle_f = zip->m_pState->m_pFile;

    if (!mz_zip_writer_finalize_archive(zip)) {
      mz_zip_error e = mz_zip_get_last_error(zip);
      const char *estr = mz_zip_get_error_string(e);
      clierror("Failed to finalize zip archive: %s.\n", estr);
      mz_zip_writer_end(zip);
      return 1;
    }

    if (!mz_zip_writer_end(zip)) {
      mz_zip_error e = mz_zip_get_last_error(zip);
      const char *estr = mz_zip_get_error_string(e);
      clierror("Failed to cleanup zip archive: %s.\n", estr);
      return 1;
    }

    if (step->as.archive_finalize.size_out)
      *step->as.archive_finalize.size_out = ftell(bundle_f);

    fclose(bundle_f);
  }
    return 0;
  }
}

int unstep(struct step *step) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return 1;
  case kSTEP_MKDIRP:
    return gab_osrm(step->as.mkdirp.path);
  case kSTEP_FETCH:
    return gab_osrm(step->as.fetch.dst);
  case kSTEP_EXTRACT:
    return gab_osrm(step->as.unzip.dst);
  case kSTEP_UNZIP:
    return gab_osrm(step->as.unzip.dst);
  case kSTEP_RM:
    return 0;
  case kSTEP_ARCHIVE_OPEN: {
    // Close file, finalize mz archive, remove file
    FILE *f = step->as.archive_open.zip->m_pState->m_pFile;

    fclose(f);

    gab_osrm(step->as.archive_open.path);

    mz_zip_writer_end(step->as.archive_open.zip);

    return 0;
  }
  case kSTEP_ARCHIVE_ADD_PACKAGE:
    return 0;
  case kSTEP_ARCHIVE_FINALIZE:
    return 0;
  }
}

void elogstep(struct step *step, int i, int res) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return;
  case kSTEP_MKDIRP:
    return clierror("Step %i failed: %i\n", i, res);
  case kSTEP_FETCH:
    if (res == 22)
      return clierror("Step %i failed: Resource %s not found\n", i,
                      step->as.fetch.url);

    return clierror("Step %i failed: %i\n", i, res);
  case kSTEP_EXTRACT:
    return clierror("Step %i failed: %i\n", i, res);
  case kSTEP_UNZIP:
    switch (res) {
    case 2:
      return clierror("Step %i failed: A module within this package did not "
                      "have a prefix which matched the specified package.\n");
    default:
      return clierror("Step %i failed: %i\n", i, res);
    }
  case kSTEP_RM:
    return clierror("Step %i failed: %i\n", i, res);
  case kSTEP_ARCHIVE_OPEN:
    return clierror("Step %i failed: %i\n", i, res);
  case kSTEP_ARCHIVE_ADD_PACKAGE:
    if (!step->as.archive_add_package.mod_out.path)
      return clierror("Step %i failed: Failed to resolve module %.*s\n", i,
                      step->as.archive_add_package.package.len,
                      step->as.archive_add_package.package.data);

    return clierror("Step %i failed: %i\n", i, res);
  case kSTEP_ARCHIVE_FINALIZE:
    return clierror("Step %i failed: %i\n", i, res);
  }
}

void slogstep(struct step *step, int i) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return;
  case kSTEP_MKDIRP:
    return clisuccess(" %2i Created  %s\n", i, step->as.mkdirp.path);
  case kSTEP_FETCH:
    return clisuccess(" %2i Fetched  %s\n", i, step->as.fetch.url);
  case kSTEP_EXTRACT:
    return clisuccess(" %2i Extracted %s\n", i, step->as.extract.src);
  case kSTEP_UNZIP:
    return clisuccess(" %2i Unzipped %s\n", i, step->as.unzip.src);
  case kSTEP_RM:
    return clisuccess(" %2i Removed %s\n", i, step->as.rm.path);
  case kSTEP_ARCHIVE_OPEN:
    return clisuccess(" %2i Opened bundle %s\n", i, step->as.archive_open.path);
  case kSTEP_ARCHIVE_ADD_PACKAGE:
    return clisuccess(" %2i Added module %.*s (%s)\n", i,
                      step->as.archive_add_package.package.len,
                      step->as.archive_add_package.package.data,
                      step->as.archive_add_package.mod_out.path->data);
  case kSTEP_ARCHIVE_FINALIZE:
    return clisuccess(" %2i Finalized bundle.\n", i);
  }
}

void logstep(struct step *step, int i) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return;
  case kSTEP_MKDIRP:
    return cliinfo(" %2i Create " GAB_MAGENTA "%s" GAB_RESET "\n", i,
                   step->as.mkdirp.path);
  case kSTEP_FETCH:
    return cliinfo(" %2i Download " GAB_MAGENTA "%s" GAB_RESET "\n", i,
                   step->as.fetch.url);
  case kSTEP_EXTRACT:
    return cliinfo(" %2i Extract " GAB_MAGENTA "%s" GAB_RESET "\n", i,
                   step->as.extract.src);
  case kSTEP_UNZIP:
    return cliinfo(" %2i Unzip " GAB_MAGENTA "%s" GAB_RESET "\n", i,
                   step->as.unzip.src);
  case kSTEP_RM:
    return cliinfo(" %2i Remove " GAB_MAGENTA "%s" GAB_RESET "\n", i,
                   step->as.rm.path);
  case kSTEP_ARCHIVE_OPEN:
    return cliinfo(" %2i Open " GAB_MAGENTA "%s" GAB_RESET "\n", i,
                   step->as.archive_open.path);
  case kSTEP_ARCHIVE_ADD_PACKAGE:
    return cliinfo(" %2i Resolve and embed " GAB_MAGENTA "%.*s" GAB_RESET "\n",
                   i, step->as.archive_add_package.package.len,
                   step->as.archive_add_package.package.data);
  case kSTEP_ARCHIVE_FINALIZE:
    return cliinfo(" %2i Finalize bundle\n", i);
  }
}

void logsteps(int len, struct step steps[len]) {
  cliinfo("The following steps will be taken:\n");

  for (int i = 0; i < len; i++)
    logstep(steps + i, i);
}

int execute_steps(int len, struct step steps[len], bool noisy) {
  for (int i = 0; i < len; i++) {
    int result = step(steps + i);

    if (result) {
      elogstep(steps + i, i, result);

      // Unwind the steps we have taken, as this one failed.
      for (int j = i - 1; j >= 0; j--)
        unstep(steps + j);

      return 1;
    }

    if (noisy)
      slogstep(steps + i, i);
  }

  return 0;
}

struct command_arguments {
  uint32_t argc, flags, wait, njobs;
  const char **argv, *platform;
  v_s_char packages;
  v_s_char package_aliases;
};

struct option {
  const char *name;
  const char *desc;
  char shorthand;
  bool takes_argument;
  int flag;
  bool (*handler_f)(struct command_arguments *args);
};

#define MAX_OPTIONS 8
#define MAX_EXAMPLES 8

struct command {
  const char *name;
  const char *desc;
  const char *long_desc;
  const char *example[MAX_EXAMPLES];
  int (*handler)(struct command_arguments *);
  struct option options[MAX_OPTIONS];
};

int get(struct command_arguments *args);
int run(struct command_arguments *args);
int exec(struct command_arguments *args);
int repl(struct command_arguments *args);
int help(struct command_arguments *args);
int welcome(struct command_arguments *args);
int build(struct command_arguments *args);
int init(struct command_arguments *args);
int info(struct command_arguments *args);

#define DEFAULT_COMMAND commands[0]

enum cliflag {
  FLAG_DUMP_AST = fGAB_AST_DUMP,
  FLAG_DUMP_BC = fGAB_BUILD_DUMP,
  FLAG_STRUCT_ERR = fGAB_ERR_STRUCTURED,
  FLAG_BUILD_TARGET = 1 << 4,
  FLAG_STEP_AUTOCONFIRM = 1 << 5,
  FLAG_STEP_VERBOSE = 1 << 6,
};

const struct option dumpast_option = {
    "dast",
    "Dump compiled ast to stdout",
    'a',
    .flag = FLAG_DUMP_AST,
};

const struct option dumpbytecode_option = {
    "dbc",
    "Dump compiled bytecode to stdout",
    'd',
    .flag = FLAG_DUMP_BC,
};

const struct option structured_err_option = {
    "sterr",
    "Instead of pretty-printing errors, use a structured output",
    's',
    .flag = FLAG_STRUCT_ERR,
};

bool target_handler(struct command_arguments *args) {
  const char *flag = *args->argv;
  args->argv++;
  args->argc--;

  if (args->argc <= 0) {
    clierror("No argument to flag '%s'.\n", flag);
    return false;
  }

  args->flags |= FLAG_BUILD_TARGET;
  const char *platform = *args->argv;
  args->argv++;
  args->argc--;

  args->platform = platform;

  return true;
}

bool jobs_handler(struct command_arguments *args) {
  const char *flag = *args->argv;
  args->argv++;
  args->argc--;

  if (args->argc <= 0) {
    clierror("No argument to flag '%s'.\n", flag);
    return false;
  }

  const char *jobs = *args->argv;
  args->argv++;
  args->argc--;

  uint32_t njobs = 0;

  njobs = atoll(jobs);
  if (njobs == 0) {
    clierror("Specify a number of jobs greater than 0.");
    return false;
  }

  args->njobs = njobs;
  return true;
}

bool busywait_handler(struct command_arguments *args) {
  const char *flag = *args->argv;
  args->argv++;
  args->argc--;

  const char *arg = strchr(flag, '=');

  if (!(arg && strlen(arg)) && args->argc <= 0) {
    clierror("No argument to flag '%s'.\n", flag);
    return false;
  }

  if (arg) {
    arg++;
  } else {
    arg = *args->argv;
    args->argv++;
    args->argc--;
  }

  uint32_t nwait = 0;

  if (!strcmp(arg, "none"))
    goto fin;

  if (!strcmp(arg, "no"))
    goto fin;

  nwait = atoll(arg);
  if (nwait == 0) {
    clierror("Specify a busy-wait greater than 0, or use none|no.");
    return false;
  }

fin:
  args->wait = nwait;
  return true;
}

bool module_handler(struct command_arguments *args) {
  const char *flag = *args->argv;
  args->argv++;
  args->argc--;

  if (args->argc <= 0) {
    clierror("No argument to flag '%s'.\n", flag);
    return false;
  }

  const char *mod = *args->argv;
  args->argv++;
  args->argc--;

  int begin = 0;
  int len = 0;
  for (int i = 0; i < strlen(mod); i++) {
    if (mod[i] == ',') {
      if (len)
        v_s_char_push(&args->packages, s_char_create(mod + begin, len));

      len = 0;
      begin = i + 1;
      continue;
    }

    len++;
  }

  if (len)
    v_s_char_push(&args->packages, s_char_create(mod + begin, len));

  return true;
}

const struct option busywait_option = {
    "busy",
    "Configure the gab engine's behavior while 'busy-waiting'",
    'w',
    .handler_f = busywait_handler,
};

const struct option modules_option = {
    "mods",
    "Load a comma-separated list of modules",
    'm',
    .handler_f = module_handler,
};

const struct option step_verbose_option = {
    "noisy",
    "Verbosely explain steps that will be taken",
    'n',
    .flag = FLAG_STEP_VERBOSE,
};

const struct option target_option = {
    "target",
    "Set the target platform of the operation",
    't',
    .flag = FLAG_BUILD_TARGET,
    .handler_f = target_handler,
};

const struct option jobs_option = {
    "jobs",
    "Specify the maximum number of threads which Gab may spawn in "
    "parallel",
    'j',
    .handler_f = jobs_handler,
};

static struct command commands[] = {
    {
        "welcome",
        "Print the welcome message",
        "\tPrint the welcome message",
        .example =
            {
                "gab",
            },
        .handler = welcome,
    },
    {
        "help",
        "Print this message, or describe the subcommand given by <arg>",
        "\tWith no arguments, prints a general help message summarizing all "
        "available subcommands and their flags.\n\t"
        "With a subcommand given by <arg>, print more specific information "
        "related to that subcommand.",
        .example =
            {
                "gab help get",
            },
        .handler = help,
    },
    {
        "get",
        "Install the package given by <arg>",
        "\tInstall packages from remote hosts.\n\n"
        "\t<arg> should have the shape <package>@<tag>."
        "\n\n\t<package> should correspond to a valid package, or the reserved "
        "'gab' package."
        "\n\n\t<tag> should be a valid tag of the aforementioned package."
        "\n\n\tWhen the <package> argument is the 'gab' package, gab "
        "*itself* is installed for the version <tag>."
        "\n\tThis installation includes the `github.com/gab-language/cgab` "
        "package, among other binary and development files.\n\t"
        "\n\tOtherwise, <package> is downloaded at <tag>, and installed "
        "among the modules for gab@" GAB_VERSION_TAG ".\n\n\t"
        "To download a package, gab needs:\n\n"
        "\t\t1. A host for the repository. This is found in the package name "
        "itself.\n"
        "\t\t2. A tag, corresponding to a release.\n"
        "\t\t3. A supported gab platform.\n"
        "\t\t4. A supported gab version.\n\n\t"
        "Using the last two items, gab constructs a bundle name like so:\n\n"
        "\t\tcgab-<gab version>-<gab platform>\n\t\tcgab-" GAB_VERSION_TAG
        "-" GAB_TARGET_TRIPLE "\n\n\t"
        "Using the first two items and the bundle name, gab constructs a url "
        "like so:\n\n"
        "\t\thttp://<pkg>/releases/download/<tag>/<bundle name>\n\t\t"
        "http://github.com/gab-language/cgab/releases/download/0.0.5/"
        "cgab-0.0.5-x86_64-linux-gnu\n\n\t"
        "Gab downloads this artifact, and unzips it into the packages <install "
        "location>.\n\t"
        "At this point, the package is installed.",
        .example =
            {
                "gab get gab@0.0.5",
                "gab get github.com/<user>/<repository>@1.2",
            },
        .handler = get,
        {
            step_verbose_option,
            target_option,
            {
                "yes",
                "Automatically confirm 'yes' when prompted",
                'y',
                .flag = FLAG_STEP_AUTOCONFIRM,
            },
        },
    },
    {
        "info",
        "Log information about the local gab environment.",
        "Dump compile-time configuration about this binary, as well as list "
        "the targets installed locally",
        .example =
            {
                "gab info",
            },
        .handler = info,
    },
    // TODO @cli: Determine if this is how we want packages to work.
    // {
    //     "init",
    //     "Initialize a package and/or module within a project.",
    //     "",
    //     .example =
    //         {
    //             "gab init",
    //         },
    //     .handler = init,
    // },
    {
        "build",
        "Build a standalone executable for the module <arg>.",
        "\tBundle the module <arg> and any modules given with -m into a "
        "single executable.\n\tWhen stdin is a file or a pipe, modules "
        "will be read line-by-line from stdin.\n\n\t"
        "Multiple platforms are supported:\n\t"
        "\tx86_64-linux-gnu    (Linux Intel)\n\t"
        "\taarch64-linux-gnu   (Linux ARM)\n\t"
        // "\tx86_64-windows-gnu  (Windows Intel)\n"
        // "\taarch64-windows-gnu (Windows ARM)\n"
        "\tx86_64-macos-none   (MacOS Intel)\n\t"
        "\taarch64-macos-none  (MacOS ARM)\n\n\t"
        "The executable produced will be named <arg>.exe. When invoked, will "
        "behave as if the user typed `gab use <arg>`.\n\t"
        "You may remove the .exe extension, but the filename is used to "
        "determine the entrypoint.\n\t"
        "The executable itself is distributable as a stand-alone binary. "
        "Users need not install anything, or even know anything about "
        "gab.\n\n\t"
        "If no entrypoint <arg> is supplied, then gab will build the modules "
        "into a library-bundle instead.\n\t"
        "These bundles are named for the gab version and platform they are "
        "built for.\n\t"
        "They look like this:\n\n\t"
        "\tcgab-0.0.5-x86_64-linux-gnu\n\n\t"
        "See `gab help get` for more information on these library bundles.",
        .example =
            {
                "gab build -m IO,Strings my_app",
                "gab build my_app < list_of_modules.txt",
            },
        .handler = build,
        {
            modules_option,
            step_verbose_option,
            target_option,
        },
    },
    {
        "run",
        "Compile and run the module at path <args>",
        "\tExpects one argument, the name of the module to run. "
        "The module is invoked as if by '<arg>'.use.\n\n\t"
        "The search path begins at the first root. Roots and resources are "
        "checked in descending order.\n\t"
        "Each resource is checked at each root before moving on to the next.\n"
        "\n\tThe roots are:"
        "\n\t\t./"
        "\n\t\t<install_dir>"
        "\n\t\t<install_dir>/github.com/gab-language/cgab@" GAB_VERSION_TAG "\n"
        "\n\tThe resources are:"
        "\n\t\t<arg>.gab"
        "\n\t\tmod/<arg>.gab"
        "\n\t\t<arg>/mod.gab"
        "\n\t\t<arg>.[so | dylib | dll]"
        "\n\t\tmod/<arg>.[so | dylib | dll]",
        .example =
            {
                "gab run -m Json,http -j 16 my_project",
            },
        .handler = run,
        {
            dumpast_option,
            dumpbytecode_option,
            structured_err_option,
            modules_option,
            busywait_option,
            jobs_option,
        },
    },
    {
        "exec",
        "Execute the string <args>",
        "\tExecute the string <arg>",
        .example =
            {
                "gab exec -a -d \"'hello'.println\"",
            },
        .handler = exec,
        {
            dumpast_option,
            dumpbytecode_option,
            structured_err_option,
            modules_option,
            busywait_option,
            jobs_option,
        },
    },
    {
        "repl",
        "Enter the REPL",
        "\tA REPL is a convenient tool for experimentation.\n"
        "\tIt is useful for developement as well - set up with editor plugins "
        "to evaluate code in the REPL.",
        .example =
            {
                "gab repl -m Json",
            },
        .handler = repl,
        {
            dumpast_option,
            dumpbytecode_option,
            modules_option,
            busywait_option,
            jobs_option,
        },
    },
};

int checksteps(struct command_arguments *args, int len,
               struct step steps[len]) {
  bool has_fetch = false;
  for (int i = 0; i < len; i++) {
    if (steps[i].k == kSTEP_FETCH)
      has_fetch = true;
  }

  if (has_fetch && gab_osfisatty(stdin) &&
      !(args->flags & FLAG_STEP_AUTOCONFIRM)) {
    cliwarn("This plan will download resources from the internet. Use the -n "
            "or --noisy flag to view the plan. Be sure these are sources you "
            "trust!\n\tExecute this plan? (y,n) ");

    int ch = getc(stdin);

    if (ch != 'y' && ch != 'Y')
      return 1;

    cliinfo("Confirmed - following plan.\n");
  }

  return 0;
}

#define N_COMMANDS (LEN_CARRAY(commands))

struct command_arguments parse_options(int argc, const char **argv,
                                       struct command command) {
  struct command_arguments args = {
      .argc = argc,
      .argv = argv,
      .njobs = cGAB_DEFAULT_NJOBS,
      .wait = cGAB_DEFAULT_WAIT_NS,
  };

  v_s_char_create(&args.packages, 32);

  while (args.argc) {
    const char *arg = *args.argv;
    if (arg[0] != '-')
      return args;

    if (arg[1] == '-') {
      for (int j = 0; j < MAX_OPTIONS; j++) {
        struct option opt = command.options[j];

        if (opt.name && !strcmp(arg + 2, opt.name)) {
          if (opt.handler_f) {
            if (!opt.handler_f(&args))
              exit(1);
          } else {
            args.flags |= opt.flag, args.argc--, args.argv++;
          }

          goto next;
        }
      }

      clierror("Unrecognized flag '%s'.\n", arg);
      exit(1);
    } else {
      for (int j = 0; j < MAX_OPTIONS; j++) {
        struct option opt = command.options[j];

        if (opt.name && arg[1] == opt.shorthand) {
          if (opt.handler_f) {
            if (!opt.handler_f(&args))
              exit(1);
          } else {
            args.flags |= opt.flag, args.argc--, args.argv++;
          }

          goto next;
        }
      }

      clierror("Unrecognized flag '%s'.\n", arg);
      exit(1);
    }

  next:
  }

  return args;
}

#define GAB_RELEASE_DOWNLOAD_URL                                               \
  "http://github.com/gab-language/cgab/releases/download/"

const char *split_pkg(char *pkg) {
  char *cursor = strchr(pkg, '@');

  if (!cursor)
    return cursor;

  *cursor = '\0';

  return ++cursor;
}

/*
 * Get the install location for a given gab target and gab version.
 */
const char *install_location(const char *target, const char *tag,
                             const char *package) {
  int taglen = strlen(tag);
  int pkglen = package ? strlen(package) : 0;

  size_t targetlen = strlen(target);
  char locbuf[taglen + targetlen + pkglen + 4];
  strncpy(locbuf, tag, taglen);
  locbuf[taglen] = '-';
  strncpy(locbuf + taglen + 1, target, targetlen);
  locbuf[taglen + targetlen + 1] = '/';

  if (package) {
    strncpy(locbuf + taglen + targetlen + 2, package, pkglen);
    locbuf[taglen + targetlen + pkglen + 2] = '/';
    locbuf[taglen + targetlen + pkglen + 3] = '\0';
  } else {
    locbuf[taglen + targetlen + 2] = '\0';
  }

  return gab_osprefix_install(locbuf);
}

struct host {
  const char *hostname, *pattern;
};

struct host known_hosts[] = {
    {"github", "http://<package>/releases/download/<tag>/<resource>"},
};

char *url_from_package(const char *package, const char *tag,
                       const char *resource) {
  char host[1024];

  const char *dot = strchr(package, '.');

  if (!dot)
    return nullptr;

  size_t hostlen = dot - package;
  if (!hostlen)
    return nullptr;

  memcpy(host, package, hostlen);

  for (int i = 0; i < LEN_CARRAY(known_hosts); i++) {
    struct host *known_host = known_hosts + i;

    if (strcmp(host, known_host->hostname))
      continue;

    // We have a match.

    v_char url = {};
    const char *cursor = known_host->pattern;

    for (;;) {
      char *pre_pattern = strchr(cursor, '<');

      if (!pre_pattern) {
        v_char_spush(&url, s_char_cstr(cursor));
        return url.data;
      }

      assert(pre_pattern > cursor);

      pre_pattern++;

      size_t len = pre_pattern - cursor;

      if (!len)
        return nullptr;

      assert(len);

      v_char_spush(&url, s_char_create(cursor, len - 1));

      const char *post_pattern = strchr(cursor, '>');
      size_t pattern_len = post_pattern - pre_pattern;

      if (!pattern_len)
        return nullptr;

      assert(pattern_len);

      if (!strncmp(pre_pattern, "package", pattern_len)) {
        v_char_spush(&url, s_char_cstr(package));
      } else if (!strncmp(pre_pattern, "tag", pattern_len)) {
        v_char_spush(&url, s_char_cstr(tag));
      } else if (!strncmp(pre_pattern, "resource", pattern_len)) {
        v_char_spush(&url, s_char_cstr(resource));
      } else {
        return nullptr;
      };

      cursor += (len + pattern_len + 1);
    }
  }

  return nullptr;
};

int get_package(v_step *steps, struct command_arguments *args,
                const char *package, const char *resource,
                const char *gab_target, const char *gab_tag) {

  // Split the requested package into its package and tag.
  const size_t pkglen = strlen(package);

  char pkgbuf[pkglen + 1];

  strncpy(pkgbuf, package, pkglen);
  pkgbuf[pkglen] = '\0';

  // Now pkg and tag point to our package and tag.
  const char *pkg = strlen(pkgbuf) ? pkgbuf : nullptr;
  const char *tag = split_pkg(pkgbuf);

  if (!tag)
    return clierror("Could not resolve tag for '%s'.\n\tTry `gab "
                    "help get`.",
                    package),
           1;

  if (!pkg)
    return clierror(
               "Could not resolve package for '%s'.\n\tTry `gab help get`.",
               package),
           1;

  v_char bundle = {0};

  if (resource) {
    v_char_spush(&bundle, s_char_cstr(resource));
    v_char_push(&bundle, '.');
  }

  v_char_spush(&bundle, s_char_cstr("cgab-"));
  v_char_spush(&bundle, s_char_cstr(gab_tag));
  v_char_push(&bundle, '-');
  v_char_spush(&bundle, s_char_cstr(gab_target));

  if (resource) {
    v_char_spush(&bundle, s_char_cstr(".exe"));
  }

  v_char_push(&bundle, '\0');

  const char *url = url_from_package(pkg, tag, bundle.data);

  if (!url)
    return clierror("Unknown host for package '%s'", package), 1;

  const char *install_dir = install_location(gab_target, gab_tag, nullptr);

  v_char bundle_dst = {};
  v_char_spush(&bundle_dst, s_char_cstr(install_dir));
  v_char_spush(&bundle_dst, s_char_cstr(package));
  v_char_push(&bundle_dst, '/');
  v_char_spush(&bundle_dst, s_char_cstr(bundle.data));
  v_char_push(&bundle_dst, '\0');

  v_char pkg_dst = {};
  v_char_spush(&pkg_dst, s_char_cstr(install_dir));
  v_char_spush(&pkg_dst, s_char_cstr(package));
  v_char_push(&pkg_dst, '\0');

  v_step_push(steps, (struct step){
                         kSTEP_MKDIRP,
                         .as.mkdirp.path = pkg_dst.data,
                     });

  v_step_push(steps, (struct step){
                         kSTEP_FETCH,
                         .as.fetch.url = url,
                         .as.fetch.dst = bundle_dst.data,
                     });

  if (!resource)
    v_step_push(steps, (struct step){
                           kSTEP_UNZIP,
                           .as.unzip.src = bundle_dst.data,
                           .as.unzip.dst = pkg_dst.data,
                       });

  return 0;
}

int download_gab(v_step *steps, struct command_arguments *args,
                 const char *gab_target, const char *gab_tag) {

  const char *location_prefix = install_location(gab_target, gab_tag, nullptr);

  if (location_prefix == nullptr) {
    clierror("Could not determine installation prefix.\n");
    return 1;
  }

  v_char binary_location = {};
  v_char_spush(&binary_location, s_char_cstr(location_prefix));
  v_char_spush(&binary_location, s_char_cstr("gab"));
  v_char_push(&binary_location, '\0');

  v_char binary_url = {};
  v_char_spush(&binary_url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&binary_url, s_char_cstr(gab_tag));
  v_char_spush(&binary_url, s_char_cstr("/gab-release-"));
  v_char_spush(&binary_url, s_char_cstr(gab_target));
  v_char_push(&binary_url, '\0');

  // Fetch dev files (libcgab.a, headers)
  v_char dev_url = {};
  v_char_spush(&dev_url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&dev_url, s_char_cstr(gab_tag));
  v_char_spush(&dev_url, s_char_cstr("/gab-release-"));
  v_char_spush(&dev_url, s_char_cstr(gab_target));
  v_char_spush(&dev_url, s_char_cstr("-dev"));
  v_char_push(&dev_url, '\0');

  v_char dev_location = {};
  v_char_spush(&dev_location, s_char_cstr(location_prefix));
  v_char_spush(&dev_location, s_char_cstr("dev"));
  v_char_push(&dev_location, '\0');

  v_char dev_extract_location = {};
  v_char_spush(&dev_extract_location, s_char_cstr(location_prefix));
  v_char_push(&dev_extract_location, '\0');

  /*
   * Fetch the standard libraries package.
   */
  v_char package = {};
  v_char_spush(&package, s_char_cstr("github.com/gab-language/cgab@"));
  v_char_spush(&package, s_char_cstr(gab_tag));
  v_char_push(&package, '\0');

  get_package(steps, args, package.data, nullptr, gab_target, gab_tag);

  v_step_push(steps, (struct step){
                         kSTEP_FETCH,
                         .as.fetch.url = binary_url.data,
                         .as.fetch.dst = binary_location.data,
                     });

  v_step_push(steps, (struct step){
                         kSTEP_FETCH,
                         .as.fetch.url = dev_url.data,
                         .as.fetch.dst = dev_location.data,
                     });

  v_step_push(steps, (struct step){
                         kSTEP_EXTRACT,
                         .as.unzip.src = dev_location.data,
                         .as.unzip.dst = dev_extract_location.data,
                     });

  v_step_push(steps, (struct step){
                         kSTEP_RM,
                         .as.rm.path = dev_location.data,
                     });

  return 0;
}

const char *platform = GAB_TARGET_TRIPLE;
const char *dynlib_fileending = GAB_DYNLIB_FILEENDING;
int update_platform(struct command_arguments *args) {
  platform = args->platform;

  if (!strcmp(platform, "x86_64-linux-gnu"))
    dynlib_fileending = ".so";
  else if (!strcmp(platform, "x86_64-macos-none"))
    dynlib_fileending = ".dylib";
  else if (!strcmp(platform, "x86_64-windows-gnu"))
    dynlib_fileending = ".dll";
  else if (!strcmp(platform, "aarch64-linux-gnu"))
    dynlib_fileending = ".so";
  else if (!strcmp(platform, "aarch64-macos-none"))
    dynlib_fileending = ".dylib";
  else if (!strcmp(platform, "aarch64-windows-gnu"))
    dynlib_fileending = ".dll";
  else
    return clierror("Unrecognized platform '%s'.\n", platform), 1;

  return 0;
};

int get(struct command_arguments *args) {
  if (args->flags & FLAG_BUILD_TARGET)
    if (update_platform(args))
      return 1;

  const char *pkg = args->argc ? args->argv[0] : "@";
  args->argc--;
  args->argv++;

  // Split the requested package into its package and tag.
  const size_t pkglen = strlen(pkg);
  char pkgbuf[pkglen + 4];
  strncpy(pkgbuf, pkg, pkglen);
  pkgbuf[pkglen] = '\0';

  const char *tag = split_pkg(pkgbuf);

  if (!tag) {
    clierror("Could not resolve package and tag for '%s'.\n\tNote: Packages "
             "have the format <url>@<tag>",
             pkgbuf);
    return false;
  }

  /* Copy the tag into a new buffer */
  const size_t taglen = strlen(tag);
  char tagbuf[taglen + 10];

  strncpy(tagbuf, tag, taglen);
  tagbuf[taglen] = '\0';

  /* Now we can check that the pkg exists, and default to Gab if it doesnt. */
  if (!strlen(pkgbuf))
    strncpy(pkgbuf, "gab", 4);

  /*
   * If we didn't find a tag in the package, then that *might* be an
   * unrecoverable error. If the user meant to download the builtin Gab package,
   * then we have a sane default. Otherwise, we error.
   */
  if (!taglen) {
    if (!strcmp(pkgbuf, "gab")) {
      strncpy(tagbuf, GAB_VERSION_TAG, 10 + taglen);
    } else {
      clierror("To download a package, a tag must be specfied. Try " GAB_GREEN
               "%s" GAB_RESET "@" GAB_YELLOW "<some tag>" GAB_RESET ".\n",
               pkgbuf);
      return 1;
    }
  }

  const char *resource = args->argc ? args->argv[0] : nullptr;
  args->argc--;
  args->argv++;

  v_step steps = {0};
  int res = 0;

  // If we match the special Gab package, then defer to that helper.
  if (!strcmp(pkgbuf, "gab"))
    res = download_gab(&steps, args, platform, tagbuf);
  else
    res = get_package(&steps, args, pkg, resource, platform, GAB_VERSION_TAG);

  if (res)
    return res;

  if (args->flags & FLAG_STEP_VERBOSE)
    logsteps(steps.len, steps.data);

  if (checksteps(args, steps.len, steps.data))
    return clierror("Installation cancelled\n"), 1;

  if (execute_steps(steps.len, steps.data, args->flags & FLAG_STEP_VERBOSE))
    return clierror("Installation failed\n"), 1;

  clisuccess("Installation complete\n");
  return 0;
}

int init_modules(v_pkg *modules, struct command_arguments *args) {

  // Append default modules.
  for (int i = 0; i < ndefault_modules; i++)
    v_pkg_push(modules, default_modules[i]);

  // Append modules requested by user.
  for (int i = 0; i < args->packages.len; i++) {
    s_char pkg = v_s_char_val_at(&args->packages, i);

    char *str = calloc(pkg.len + 1, 1);
    memcpy(str, pkg.data, pkg.len);

    v_pkg_push(modules, (struct gab_package){str});
  }

  // Push a terminator module to the list
  v_pkg_push(modules, (struct gab_package){});

  size_t nmodules = modules->len;
  assert(nmodules > 0);

  return nmodules;
}

int run(struct command_arguments *args) {
  if (args->argc < 1) {
    clierror("Missing module argument to subcommand 'run'\n");
    return 1;
  }

  const char *path = args->argv[0];

  v_pkg modules = {0};
  int nmodules = init_modules(&modules, args);

  int res = run_file(path, args->flags, args->wait, args->njobs, nmodules - 1,
                     modules.data);

  v_pkg_destroy(&modules);

  return res;
}

int exec(struct command_arguments *args) {
  if (args->argc < 1) {
    clierror("Missing code argument to subcommand 'exec'.\n");
    return 1;
  }

  v_pkg modules = {0};
  int nmodules = init_modules(&modules, args);

  int res = run_string(args->argv[0], args->flags, args->wait, args->njobs,
                       nmodules - 1, modules.data);

  v_pkg_destroy(&modules);

  return res;
}

int repl(struct command_arguments *args) {
  v_pkg modules = {0};
  int nmodules = init_modules(&modules, args);

  int res = run_repl(args->flags, args->wait, nmodules - 1, modules.data);

  v_pkg_destroy(&modules);

  return res;
}

void cmd_summary(int i) {
  struct command cmd = commands[i];
  printf("\n\tgab %-8s [opts] <args>\t%s", cmd.name, cmd.desc);
}

void cmd_details(int i) {
  struct command cmd = commands[i];
  printf("USAGE:\n\tgab %4s [opts] <args>\n\nDESCRIPTION:\n%s\n\nEXAMPLES:",
         cmd.name, cmd.long_desc);

  for (const char **example = cmd.example; *example; example++) {
    printf("\n\t%s", *example);
  }

  if (cmd.options[0].name == nullptr)
    return;

  printf("\n\nFLAGS:\n");
  for (int j = 0; j < MAX_OPTIONS; j++) {
    struct option opt = cmd.options[j];

    if (!opt.name)
      break;

    printf("\t--%-8s\t-%c\t%s.\n", opt.name, opt.shorthand, opt.desc);
  }
}

int welcome(struct command_arguments *args) {
  printf(
      "%s\n%s", welcome_message,
      "To get started, run `gab help` for a list of commands."
      "\n\nIf you've just downloaded gab, welcome! Run `gab get` to complete "
      "your installation.");
  return 0;
}

struct {
  char *name, *value;
} compile_info[] = {
    {"default workers", STR(cGAB_DEFAULT_NJOBS)},
    {"send-cache len", STR(cGAB_SEND_CACHE_LEN)},
    {"str-hash len", STR(cGAB_STRING_HASHLEN)},
    {"superinsts?", STR(cGAB_SUPERINSTRUCTIONS)},
    {"tailcall?", STR(cGAB_TAILCALL)},
    {"likely?", STR(cGAB_LIKELY)},
    {"eg-idle tries", STR(cGAB_WORKER_IDLE_TRIES)},
    {"vm-put tries", STR(cGAB_VM_CHANNEL_PUT_TRIES)},
    {"vm-take tries", STR(cGAB_VM_CHANNEL_TAKE_TRIES)},
    {"busywait-ns", STR(cGAB_DEFAULT_WAIT_NS)},
    {"dict load", STR(cGAB_DICT_MAX_LOAD)},
    {"worker qmax", STR(cGAB_WORKER_LOCALQUEUE_MAX)},
    {"max frames", STR(cGAB_FRAMES_MAX)},
    {"max stack", STR(cGAB_STACK_MAX)},
    {"res stack", STR(cGAB_RESOURCE_MAX)},
};

struct {
  const char *name, *target;
} possible_targets[] = {
    {"x64 linux", "x86_64-linux-gnu"},
    {"x64 macos", "x86_64-macos-none"},
    {"x64 windows", "x86_64-windows-gnu"},
    {"arm linux", "aarch64-linux-gnu"},
    {"arm macos", "aarch64-macos-none"},
    {"arm windows", "aarch64-windows-gnu"},
};

int info(struct command_arguments *args) {
  printf("%s\n%17s\n", welcome_message, "CONFIGURATION");

  for (int i = 0; i < LEN_CARRAY(compile_info); i++) {
    printf("%17s | %s\n", compile_info[i].name, compile_info[i].value);
  }

  printf("\n%17s\n", GAB_VERSION_TAG " TARGETS");

  for (int i = 0; i < LEN_CARRAY(possible_targets); i++) {
    const char *target = possible_targets[i].target;
    const char *loc = install_location(target, GAB_VERSION_TAG, nullptr);
    bool exists = file_exister(loc);
    printf("%17s | %s\n", possible_targets[i].name,
           exists ? loc : "not installed");
  }
  return 0;
}

int help(struct command_arguments *args) {
  if (args->argc < 1) {
    printf("To see more details about each command, "
           "run:\n\n\tgab help <cmd>\n\nThe available commands are:\n");

    // Print command summaries
    for (int i = 0; i < N_COMMANDS; i++)
      cmd_summary(i);

    return 0;
  }

  const char *subcommand = args->argv[0];

  for (int i = 0; i < N_COMMANDS; i++) {
    struct command cmd = commands[i];
    if (!strcmp(subcommand, cmd.name)) {
      cmd_details(i);
      return 0;
    }
  }

  clierror("Unrecognized subcommand '%s'.\n", subcommand);
  return 1;
}

#define MODULE_NAME_MAX 2048

int build_exe(struct command_arguments *args, const char *module) {
  v_char bundle = {};
  v_char_spush(&bundle, s_char_cstr(module));
  v_char_spush(&bundle, s_char_cstr(".cgab-"));
  v_char_spush(&bundle, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&bundle, '-');
  v_char_spush(&bundle, s_char_cstr(platform));
  v_char_spush(&bundle, s_char_cstr(".exe"));
  v_char_push(&bundle, '\0');

  v_char exepath = {};
  v_char_spush(&exepath, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&exepath, '-');
  v_char_spush(&exepath, s_char_cstr(platform));
  v_char_push(&exepath, '/');
  v_char_push(&exepath, '\0');
  const char *path = gab_osprefix_install(exepath.data);

  v_char_destroy(&exepath);
  v_char_spush(&exepath, s_char_cstr(path));
  v_char_spush(&exepath, s_char_cstr("gab"));
  v_char_push(&exepath, '\0');

  v_s_char_push(&args->packages, s_char_cstr(module));

  struct gab_resource platform_file_resources[nnative_file_resources + 2];

  memset(platform_file_resources, 0, sizeof platform_file_resources);

  memcpy(platform_file_resources + 1, native_file_resources,
         sizeof(native_file_resources));

  v_char platform_dynlib_suffix = {0};
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(".cgab-"));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&platform_dynlib_suffix, '-');
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(platform));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(dynlib_fileending));
  v_char_push(&platform_dynlib_suffix, '\0');

  // Replace the native DYNLIBFILEENDING witht the platform-specific one.
  // TODO @cli @bug: This is kinda manual and bug prone if we change resources.
  platform_file_resources[1].suffix = platform_dynlib_suffix.data;
  platform_file_resources[2].suffix = platform_dynlib_suffix.data;

  v_char platform_bundle_suffix = {0};
  v_char_spush(&platform_bundle_suffix, s_char_cstr("cgab-"));
  v_char_spush(&platform_bundle_suffix, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&platform_bundle_suffix, '-');
  v_char_spush(&platform_bundle_suffix, s_char_cstr(platform));
  v_char_push(&platform_bundle_suffix, '\0');

  platform_file_resources[0] = (struct gab_resource){
      .prefix = "",
      .suffix = platform_bundle_suffix.data,
      .exister = file_exister,
      .loader = nullptr,
  };

  /*
   * We need our own custom roots here when building a bundled app.
   * This is because we can cross-compile our builds for os/architectures other
   * than our own.
   **/
  const char *platform_roots[] = {
      "./",
      install_location(platform, GAB_VERSION_TAG, nullptr),
      nullptr,
  };

  mz_zip_archive zip_o = {0};

  v_step steps = {0};

  bool is_native = !strcmp(platform, GAB_TARGET_TRIPLE);

  v_step_push(&steps, (struct step){
                          kSTEP_ARCHIVE_OPEN,
                          .as.archive_open.path = bundle.data,
                          .as.archive_open.zip = &zip_o,
                          .as.archive_open.initial_data_path = exepath.data,
                          .as.archive_open.initial_data_fallback_path =
                              is_native ? gab_osexepath() : nullptr,
                      });

  for (int i = 0; i < args->packages.len; i++)
    v_step_push(&steps,
                (struct step){
                    kSTEP_ARCHIVE_ADD_PACKAGE,
                    .as.archive_add_package.zip = &zip_o,
                    .as.archive_add_package.roots = platform_roots,
                    .as.archive_add_package.resources = platform_file_resources,
                    .as.archive_add_package.package =
                        v_s_char_val_at(&args->packages, i),

                });

  long size = 0;

  v_step_push(&steps, (struct step){
                          kSTEP_ARCHIVE_FINALIZE,
                          .as.archive_finalize.zip = &zip_o,
                          .as.archive_finalize.size_out = &size,
                      });

  if (args->flags & FLAG_STEP_VERBOSE)
    logsteps(steps.len, steps.data);

  if (checksteps(args, steps.len, steps.data))
    return clierror("Installation cancelled.\n"), 1;

  if (execute_steps(steps.len, steps.data, args->flags & FLAG_STEP_VERBOSE))
    return clierror("Bundle creation failed.\n"), 1;

#if GAB_PLATFORM_UNIX
  if (chmod(bundle.data, 0755) != 0) {
    return clierror("Failed to chmod bundle"), 1;
  }
#endif

  clisuccess("Created bundled executable " GAB_CYAN "%s" GAB_RESET
             " (%2.2lf mb)\n",
             bundle.data, (double)size / 1024 / 1024);

  return 0;
};

int build_lib(struct command_arguments *args) {
  if (args->packages.len == 0)
    return clierror("No modules were requested. See `gab help build`"), 1;

  v_char bundle = {0};
  v_char_spush(&bundle, s_char_cstr("cgab-"));
  v_char_spush(&bundle, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&bundle, '-');
  v_char_spush(&bundle, s_char_cstr(platform));
  v_char_push(&bundle, '\0');

  struct gab_resource platform_file_resources[nnative_file_resources + 2];

  memset(platform_file_resources, 0, sizeof platform_file_resources);

  memcpy(platform_file_resources + 1, native_file_resources,
         sizeof(native_file_resources));

  v_char platform_dynlib_suffix = {0};
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(".cgab-"));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&platform_dynlib_suffix, '-');
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(platform));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(dynlib_fileending));
  v_char_push(&platform_dynlib_suffix, '\0');

  // Replace the native DYNLIBFILEENDING with the platform-specific one.
  // This is kinda manual and bug prone if we change resources.
  platform_file_resources[1].suffix = platform_dynlib_suffix.data;
  platform_file_resources[2].suffix = platform_dynlib_suffix.data;

  v_char platform_bundle_suffix = {0};
  v_char_spush(&platform_bundle_suffix, s_char_cstr("/cgab-"));
  v_char_spush(&platform_bundle_suffix, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&platform_bundle_suffix, '-');
  v_char_spush(&platform_bundle_suffix, s_char_cstr(platform));
  v_char_push(&platform_bundle_suffix, '\0');

  /* Add an additional kind of resource for builds such as these:
   * A BUNDLE loading resource.
   * cgab@0.0.5 -> gab-language/cgab/cgab-0.0.5-x86_64-linux-gnu
   */
  platform_file_resources[0] = (struct gab_resource){
      .prefix = "",
      .suffix = platform_bundle_suffix.data,
      .exister = file_exister,
      .loader = nullptr,
  };

  /*
   * We need our own custom roots here when building a bundled app.
   * This is because we can cross-compile our builds for os/architectures other
   * than our own.
   **/
  const char *platform_roots[] = {
      "./",
      install_location(platform, GAB_VERSION_TAG, nullptr),
      nullptr,
  };

  mz_zip_archive zip_o = {0};

  v_step steps = {0};

  v_step_push(&steps, (struct step){
                          kSTEP_ARCHIVE_OPEN,
                          .as.archive_open.path = bundle.data,
                          .as.archive_open.zip = &zip_o,
                      });

  for (int i = 0; i < args->packages.len; i++)
    v_step_push(&steps,
                (struct step){
                    kSTEP_ARCHIVE_ADD_PACKAGE,
                    .as.archive_add_package.zip = &zip_o,
                    .as.archive_add_package.roots = platform_roots,
                    .as.archive_add_package.resources = platform_file_resources,
                    .as.archive_add_package.package =
                        v_s_char_val_at(&args->packages, i),
                });

  long size = 0;

  v_step_push(&steps, (struct step){
                          kSTEP_ARCHIVE_FINALIZE,
                          .as.archive_finalize.zip = &zip_o,
                          .as.archive_finalize.size_out = &size,
                      });

  if (args->flags & FLAG_STEP_VERBOSE)
    logsteps(steps.len, steps.data);

  if (checksteps(args, steps.len, steps.data))
    return clierror("Installation cancelled.\n"), 1;

  if (execute_steps(steps.len, steps.data, args->flags & FLAG_STEP_VERBOSE))
    return clierror("Bundle creation failed.\n"), 1;

  clisuccess("Created bundled library " GAB_CYAN "%s" GAB_RESET
             " (%2.2lf mb)\n",
             bundle.data, (double)size / 1024 / 1024);

  return 0;
};

int touch(const char *path) {
  FILE *f = fopen(path, "w");

  if (f)
    fclose(f);

  return f != nullptr;
}

int init(struct command_arguments *args) {
  const char *package = nullptr, *module = nullptr;

  if (args->argc) {
    package = args->argv[0];
    args->argv++;
    args->argc--;

    char *colon = strchr(package, ':');
    if (colon) {
      *colon = '\0';
      module = colon + 1;
    }
  }

  if (args->argc) {
    if (module)
      return clierror("Module already specified: %s\n", module), 1;

    module = args->argv[0];
    args->argv++;
    args->argc--;
  }

  if (!package) {
    return clierror("No package specified\n"), 1;
  }

  // We may not have a module at this point.
  // In that case, just initialize the package.
  if (!module) {
    v_char path = {0};
    v_char_spush(&path, s_char_cstr(package));
    v_char_spush(&path, s_char_cstr("/mod/"));

    v_char_push(&path, '\0');
    gab_osmkdirp(path.data);

    v_char_pop(&path);
    v_char_spush(&path, s_char_cstr("/mod.gab"));

    if (!touch(path.data))
      return clierror("Failed to create file %s.", path.data), 1;

    clisuccess("Created package %s.", package);
    return 0;
  }

  v_char path = {0};
  v_char_spush(&path, s_char_cstr(package));
  v_char_spush(&path, s_char_cstr("/mod/"));
  v_char_spush(&path, s_char_cstr(module));

  v_char_push(&path, '\0');
  gab_osmkdirp(path.data);

  v_char_pop(&path);

  v_char_spush(&path, s_char_cstr("/mod.gab"));
  v_char_push(&path, '\0');

  if (!touch(path.data))
    return clierror("Failed to create file %s.", path.data), 1;

  clisuccess("Created package %s.", package);
  clisuccess("Created module %s.", module);
  return 0;
}

int build(struct command_arguments *args) {
  // If we detect that our stdin isn't a terminal (ie its a pipe or a file)
  // we read modules line-by-line from stdin.
  if (!gab_osfisatty(stdin)) {
    char line[MODULE_NAME_MAX];

    while (fgets(line, MODULE_NAME_MAX, stdin)) {
      int len = strlen(line);
      // TODO @cgab @cli: Skip whitespace before and after.

      // Skip empty lines
      if (!len)
        continue;

      // Skip comments
      if (line[0] == '#')
        continue;

      // Trim out that newline, if we have it.
      // In some cases, we might get an EOF before a newline.
      if (line[len - 1] == '\n')
        len--;

      // These are allocated, just leak em who cares.
      a_char *module = a_char_create(line, len);

      // Add the module to our module list.
      v_s_char_push(&args->packages, s_char_create(module->data, module->len));
    }
  }

  if (args->flags & FLAG_BUILD_TARGET)
    if (update_platform(args))
      return 1;

  v_s_char_push(&args->package_aliases, s_char_cstr(""));

  if (args->argc < 1)
    return build_lib(args);

  const char *module = args->argv[0];
  args->argv++;
  args->argc--;

  return build_exe(args, module);
}

bool check_not_gab(const char *name) {
  if (strlen(name) != 3)
    return true;

  return memcmp(name, "gab", 3);
}

bool check_valid_zip() {
  const char *path = gab_osexepath();

  mz_zip_zero_struct(&zip);

  assert(&zip);
  assert(path);
  if (!mz_zip_reader_init_file(&zip, path, 0)) {
    // TODO @cgab @cli: Report this error somehow
    // mz_zip_error e = mz_zip_get_last_error(&zip);
    return false;
  }

  size_t files = mz_zip_reader_get_num_files(&zip);

  return files;
}

int main(int argc, const char **argv) {
  /*register_printf_specifier('V', gab_val_printf_handler,*/
  /*                          gab_val_printf_arginfo);*/

  /**
   * Pull locale from ENV
   */
  setlocale(LC_ALL, "");

  /*
   * Populate roots list.
   */
  roots[0] = "./";
  roots[1] = install_location(GAB_TARGET_TRIPLE, GAB_VERSION_TAG, nullptr);
  roots[2] = nullptr;

  if (check_not_gab(argv[0]) && check_valid_zip())
    return run_bundle(argv[0]);

  if (argc < 2)
    goto fin;

  for (int i = 0; i < N_COMMANDS; i++) {
    struct command cmd = commands[i];
    assert(cmd.handler);

    if (!strcmp(argv[1], cmd.name)) {
      struct command_arguments o = parse_options(argc - 2, argv + 2, cmd);

      int res = cmd.handler(&o);
      v_s_char_destroy(&o.packages);
      return res;
    }
  }

fin:
  struct command cmd = DEFAULT_COMMAND;
  return cmd.handler(&(struct command_arguments){});
}
