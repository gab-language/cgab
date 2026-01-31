#include "core.h"
#include "miniz/amalgamation/miniz.c"
#include "miniz/amalgamation/miniz.h"

#include "gab.h"
#include <locale.h>
#include <stddef.h>

#include "crossline/crossline.c"
#include "crossline/crossline.h"

#include "platform.h"

#include <stdio.h>

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
  gab_value *errors = gab_egerrs(gab.eg);

  if (!errors)
    return;

  for (gab_value *err = errors; *err != gab_nil; err++) {
    assert(gab_valkind(*err) == kGAB_RECORD);
    const char *errstr = gab_errtocs(gab, *err);
    assert(errstr != nullptr);
    fputs(errstr, stderr);
  };

  free(errors);
}

bool check_and_printerr(union gab_value_pair *res) {
  if (res->status == gab_ctimeout)
    *res = gab_fibawait(gab, res->vresult);

  pop_and_printerr(gab);

  if (res->status != gab_cvalid) {
    if (res->status == gab_cinvalid && res->vresult) {
      const char *errstr = gab_errtocs(gab, res->vresult);
      assert(errstr != nullptr);
      fputs(errstr, stderr);
      fflush(stderr);
    }
    return false;
  }

  // if (res->aresult->data[0] != gab_ok) {
  //   const char *errstr = gab_errtocs(gab, res->aresult->data[1]);
  //   assert(errstr != nullptr);
  //   fputs(errstr, stderr);
  //   fflush(stderr);
  //   return a_gab_value_destroy(res->aresult), false;
  // }

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

  auto mod = (union gab_value_pair (*)(struct gab_triple))gab_oslibfind(
      lib, GAB_DYNLIB_MAIN);

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
   * TODO: This should really be organized per-process, as multiple gab-apps
   * can be opened at the same time, and we don't want them to stomp over each
   * other. *maybe* this is fixed by checking if the file exists first.
   *
   * TODO: Properly create directories that are nested.
   * The filename here can be something like 'mod/other_lib/sub/example'
   * We need to walk down this path, creating directories in /tmp/gab
   * *maybe mz takes care of this?*
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

  union gab_value_pair fiber = gab_aexec(gab, (struct gab_exec_argt){
                                                  .name = path,
                                                  .source_len = sz,
                                                  .source = (const char *)src,
                                                  .flags = gab.flags,
                                                  .len = len,
                                                  .sargv = sargs,
                                                  .argv = vargs,
                                              });

  a_char_destroy(src);

  if (fiber.status != gab_cvalid)
    return fiber;

  return gab_tfibawait(gab, fiber.vresult, cGAB_WORKER_IDLE_TRIES);
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

static const char *default_modules_deps[] = {
    "cstrings", "cshapes", "cmessages", "cnumbers",
    "crecords", "cfibers", "cchannels", "cio",
};
static const size_t ndefault_modules_deps = LEN_CARRAY(default_modules_deps);

static const char *default_modules[] = {
    "Strings", "Binaries", "Shapes",  "Messages", "Numbers",
    "Blocks",  "Records",  "Fibers",  "Channels", "__core",
    "Ranges",  "IO",       "Streams", nullptr,
};
static const size_t ndefault_modules = LEN_CARRAY(default_modules) - 1;

#define GAB_NATIVE_MODULE_SUFFIX                                               \
  ".cgab-" GAB_VERSION_TAG "-" GAB_TARGET_TRIPLE GAB_DYNLIB_FILEENDING

static const struct gab_resource native_file_resources[] = {
    {"mod/", GAB_NATIVE_MODULE_SUFFIX, gab_use_dynlib, file_exister},
    {"", GAB_NATIVE_MODULE_SUFFIX, gab_use_dynlib, file_exister},
    {"", "/mod.gab", gab_use_source, file_exister},
    {"mod/", ".gab", gab_use_source, file_exister},
    {"", ".gab", gab_use_source, file_exister},
    {}, // List terminator.
};

static const size_t nnative_file_resources =
    LEN_CARRAY(native_file_resources) - 1;

static const struct gab_resource native_zip_resources[] = {
    {"mod/", GAB_NATIVE_MODULE_SUFFIX, gab_use_zip_dynlib, zip_exister},
    {"", GAB_NATIVE_MODULE_SUFFIX, gab_use_zip_dynlib, zip_exister},
    {"", "/mod.gab", gab_use_zip_source, zip_exister},
    {"mod/", ".gab", gab_use_zip_source, zip_exister},
    {"", ".gab", gab_use_zip_source, zip_exister},
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

int run_repl(int flags, uint32_t wait, size_t nmodules, const char **modules) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .wait = wait ? wait : 50000,
          .modules = modules,
          .roots = roots,
          .resources = native_file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  gab_repl(gab, (struct gab_repl_argt){
                    .name = MAIN_MODULE,
                    .flags = flags,
                    .welcome_message = welcome_message,
                    .prompt_prefix = ">>> ",
                    .promptmore_prefix = "|   ",
                    .result_prefix = "",
                    .readline = readline,
                    .len = nmodules,
                    .sargv = modules,
                    .argv = res.aresult->data + 1, // Skip initial ok:
                });

  // The user may have left some fibers running unterminated.
  // This will confusingly hang after gab_repl has returned,
  // until the user sends SIG_INT.
  // We just manually terimate any running fibers here.
  gab_sigterm(gab);

  a_gab_value_destroy(res.aresult);

  return gab_destroy(gab), 0;
}

int run_string(const char *string, int flags, uint32_t wait, size_t jobs,
               size_t nmodules, const char **modules) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .wait = wait,
          .jobs = jobs,
          .modules = modules,
          .roots = roots,
          .resources = native_file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 0;

  // This is a weird case where we actually want to include the null terminator
  s_char src = s_char_create(string, strlen(string) + 1);

  union gab_value_pair run_res =
      gab_exec(gab, (struct gab_exec_argt){
                        .name = MAIN_MODULE,
                        .source = (char *)src.data,
                        .flags = flags,
                        .len = nmodules,
                        .sargv = modules,
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

  if (len > 4 && !memcmp(mod + len - 4, ".exe", 4))
    len -= 4;

  size_t modlen = 0;
  for (size_t i = len - 1; i > 0; i--) {
    modlen++;
    if (mod[i - 1] == '\\' || mod[i - 1] == '/') {
      mod = mod + i;
      break;
    }
  }

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .modules = default_modules,
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

  union gab_value_pair run_res =
      gab_use(gab, (struct gab_use_argt){
                       .vname = gab_nstring(gab, modlen, mod),
                       .len = ndefault_modules,
                       .sargv = default_modules,
                       .argv = res.aresult->data + 1,
                   });

  a_gab_value_destroy(res.aresult);

  if (!check_and_printerr(&run_res))
    return gab_destroy(gab), 1;

  return a_gab_value_destroy(run_res.aresult), gab_destroy(gab), 0;
}

int run_file(const char *path, int flags, uint32_t wait, size_t jobs,
             size_t nmodules, const char **modules) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .wait = wait,
          .jobs = jobs,
          .modules = modules,
          .roots = roots,
          .resources = native_file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  union gab_value_pair run_res = gab_use(gab, (struct gab_use_argt){
                                                  .flags = flags,
                                                  .sname = path,
                                                  .len = nmodules,
                                                  .sargv = modules,
                                                  .argv = res.aresult->data + 1,
                                              });

  a_gab_value_destroy(res.aresult);

  if (!check_and_printerr(&run_res))
    return gab_destroy(gab), 1;

  return a_gab_value_destroy(run_res.aresult), gab_destroy(gab), 0;
}

bool add_module(mz_zip_archive *zip_o, const char **roots,
                const struct gab_resource *resources, s_char module,
                const char **prefix_out, const char **suffix_out) {

  char cstr_module[module.len + 1];
  memcpy(cstr_module, module.data, module.len);
  cstr_module[module.len] = '\0';

  const char *path =
      gab_mresolve(roots, resources, cstr_module, prefix_out, suffix_out);

  // If the path ends in *mod.gab*, we should treat the whole directory as a
  // module and add it.

  if (!path)
    return false;

  const char *prefix = *prefix_out;
  const char *suffix = *suffix_out;

  size_t lenprefix = strlen(prefix);
  size_t lensuffix = strlen(suffix);
  size_t lenpath = strlen(cstr_module);
  char modulename[lenprefix + lenpath + lensuffix + 1];

  memcpy(modulename, prefix, lenprefix);
  memcpy(modulename + lenprefix, cstr_module, lenpath);
  memcpy(modulename + lenprefix + lenpath, suffix, lensuffix);
  modulename[lenprefix + lenpath + lensuffix] = '\0';

  /*
   * It is unclear whether it is more important to prioritize speed
   * (which affects startup/load time)
   * or compression
   * (which affects bundle size).
   *
   * Perhaps leave this up to the user?
   */
  return mz_zip_writer_add_file(zip_o, modulename, path, nullptr, 0,
                                MZ_BEST_SPEED);
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
    kSTEP_ARCHIVE_ADD_MODULE,
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
      s_char module;
      const char **prefix_out;
      const char **suffix_out;
    } archive_add_module;

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

      v_char filename = {0};
      v_char_spush(&filename, s_char_cstr(step->as.unzip.dst));
      v_char_push(&filename, '/');
      v_char_spush(&filename, s_char_cstr(stat.m_filename));
      v_char_push(&filename, '\0');

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
  case kSTEP_ARCHIVE_ADD_MODULE:
    return !add_module(step->as.archive_add_module.zip,
                       step->as.archive_add_module.roots,
                       step->as.archive_add_module.resources,
                       step->as.archive_add_module.module,
                       step->as.archive_add_module.prefix_out,
                       step->as.archive_add_module.suffix_out);
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
  case kSTEP_ARCHIVE_ADD_MODULE:
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
    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_FETCH:
    if (res == 22)
      return clierror("Step %i failed: Resource %s not found.\n", i,
                      step->as.fetch.url);

    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_EXTRACT:
    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_UNZIP:
    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_RM:
    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_ARCHIVE_OPEN:
    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_ARCHIVE_ADD_MODULE:
    if (!*step->as.archive_add_module.prefix_out ||
        !*step->as.archive_add_module.suffix_out)
      return clierror("Step %i failed: Failed to resolve module %.*s.\n", i,
                      step->as.archive_add_module.module.len,
                      step->as.archive_add_module.module.data);

    return clierror("Step %i failed: %i.\n", i, res);
  case kSTEP_ARCHIVE_FINALIZE:
    return clierror("Step %i failed: %i.\n", i, res);
  }
}

void slogstep(struct step *step, int i) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return;
  case kSTEP_MKDIRP:
    return clisuccess(" %i Created  %s.\n", i, step->as.mkdirp.path);
  case kSTEP_FETCH:
    return clisuccess(" %i Fetched  %s.\n", i, step->as.fetch.url);
  case kSTEP_EXTRACT:
    return clisuccess(" %i Extracted %s.\n", i, step->as.extract.src);
  case kSTEP_UNZIP:
    return clisuccess(" %i Unzipped %s.\n", i, step->as.unzip.src);
  case kSTEP_RM:
    return clisuccess(" %i Removed %s.\n", i, step->as.rm.path);
  case kSTEP_ARCHIVE_OPEN:
    return clisuccess(" %i Opened bundle %s.\n", i, step->as.archive_open.path);
  case kSTEP_ARCHIVE_ADD_MODULE:
    return clisuccess(" %i Added module %.*s.\n\t%s%.*s%s\n", i,
                      step->as.archive_add_module.module.len,
                      step->as.archive_add_module.module.data,
                      *step->as.archive_add_module.prefix_out,
                      step->as.archive_add_module.module.len,
                      step->as.archive_add_module.module.data,
                      *step->as.archive_add_module.suffix_out);
  case kSTEP_ARCHIVE_FINALIZE:
    return clisuccess(" %i Finalized bundle.\n", i);
  }
}

void logstep(struct step *step, int i) {
  switch (step->k) {
  case kSTEP_NONE:
    assert(false);
    return;
  case kSTEP_MKDIRP:
    return cliinfo(" %2i Ensure a directory exists at " GAB_MAGENTA
                   "%s" GAB_RESET ".\n",
                   i, step->as.mkdirp.path);
  case kSTEP_FETCH:
    return cliinfo(" %2i Via curl, download " GAB_MAGENTA "%s" GAB_RESET
                   " to " GAB_MAGENTA "%s" GAB_RESET ".\n",
                   i, step->as.fetch.url, step->as.fetch.dst);
  case kSTEP_EXTRACT:
    return cliinfo(" %2i Via tar, extract " GAB_MAGENTA "%s" GAB_RESET " to " GAB_MAGENTA
                   "%s" GAB_RESET ".\n",
                   i, step->as.extract.src, step->as.extract.dst);
  case kSTEP_UNZIP:
    return cliinfo(" %2i Unzip " GAB_MAGENTA "%s" GAB_RESET " to " GAB_MAGENTA
                   "%s" GAB_RESET ".\n",
                   i, step->as.unzip.src, step->as.unzip.dst);
  case kSTEP_RM:
    return cliinfo(" %2i Remove " GAB_MAGENTA "%s" GAB_RESET " if it exists.\n",
                   i, step->as.rm.path);
  case kSTEP_ARCHIVE_OPEN:
    return cliinfo(" %2i Open bundle %s.\n", i, step->as.archive_open.path);
  case kSTEP_ARCHIVE_ADD_MODULE:
    return cliinfo(" %2i Resolve and append module %.*s.\n", i,
                   step->as.archive_add_module.module.len,
                   step->as.archive_add_module.module.data);
  case kSTEP_ARCHIVE_FINALIZE:
    return cliinfo(" %2i Finalize bundle.\n", i);
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
  uint32_t argc, flags, wait;
  const char **argv;
  v_s_char modules;
  const char *platform;
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

bool busywait_handler(struct command_arguments *args) {
  const char *flag = *args->argv;
  args->argv++;
  args->argc--;

  if (args->argc <= 0) {
    clierror("No argument to flag '%s'.\n", flag);
    return false;
  }

  const char *wait = *args->argv;
  args->argv++;
  args->argc--;

  uint32_t nwait = 0;

  if (!strcmp(wait, "none"))
    return true;

  if (!strcmp(wait, "no"))
    return true;

  nwait = atoll(wait);
  if (nwait == 0) {
    clierror("Specify a busy-wait greater than 0, or use none|no.");
    return false;
  }

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
        v_s_char_push(&args->modules, s_char_create(mod + begin, len));

      len = 0;
      begin = i + 1;
      continue;
    }

    len++;
  }

  if (len)
    v_s_char_push(&args->modules, s_char_create(mod + begin, len));

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

static struct command commands[] = {
    {
        "welcome",
        "Print the welcome message.",
        "Print the welcome message.",
        .example =
            {
                "gab",
            },
        .handler = welcome,
    },
    {
        "help",
        "Print this message, or describe the subcommand given by <arg>",
        "With no arguments, prints a general help message summarizing all "
        "available subcommands and their flags.\n"
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
        "<arg> should have the shape <package>@<tag>."
        "\n\n<package> should correspond to a valid package, or the reserved "
        "'gab' package."
        "\n\n<tag> should be a valid tag of the aforementioned package."
        "\n\nWhen the <package> argument is the 'gab' package, gab "
        "*itself* is installed for the version <tag>."
        "\nThis installation includes the `github.com/gab-language/cgab` "
        "package, among other binary and development files.\n"
        "\nOtherwise, <package> is downloaded at <tag>, and installed "
        "among the modules for gab@" GAB_VERSION_TAG ".\n\n"
        "To download a package, gab needs:\n\n"
        "\t1. A host for the repository. This is found in the package name "
        "itself.\n"
        "\t2. A tag, corresponding to a release.\n"
        "\t3. A supported gab platform.\n"
        "\t4. A supported gab version.\n\n"
        "Using the last two items, gab constructs a bundle name like so:\n\n"
        "\tcgab-<gab version>-<gab platform>\n\tcgab-" GAB_VERSION_TAG
        "-" GAB_TARGET_TRIPLE "\n\n"
        "Using the first two items and the bundle name, gab constructs a url "
        "like so:\n\n"
        "\thttp://<pkg>/releases/download/<tag>/<bundle name>\n\t"
        "http://github.com/gab-language/cgab/releases/download/0.0.5/"
        "cgab-0.0.5-x86_64-linux-gnu\n\n"
        "Gab downloads this artifact, and unzips it into the packages <install "
        "location>.\n"
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
        "build",
        "Build a standalone executable for the module <arg>.",
        "Bundle the module <arg> and any modules given with -m into a "
        "single executable.\nWhen stdin is a file or a pipe, modules "
        "will be read line-by-line from stdin.\n\n"
        "Multiple platforms are supported:\n"
        "\tx86_64-linux-gnu    (Linux Intel)\n"
        "\taarch64-linux-gnu   (Linux ARM)\n"
        // "\tx86_64-windows-gnu  (Windows Intel)\n"
        // "\taarch64-windows-gnu (Windows ARM)\n"
        "\tx86_64-macos-none   (MacOS Intel)\n"
        "\taarch64-macos-none  (MacOS ARM)\n\n"
        "The executable produced will be named <arg>.exe. When invoked, will "
        "behave as if the user typed `gab use <arg>`.\n"
        "You may remove the .exe extension, but the filename is used to "
        "determine the entrypoint.\n"
        "The executable itself is distributable as a stand-alone binary. "
        "Users need not install anything, or even know anything about gab.\n\n"
        "If no entrypoint <arg> is supplied, then gab will build the modules "
        "into a library-bundle instead.\n"
        "These bundles are named for the gab version and platform they are "
        "built for.\n"
        "They look like this:\n\n"
        "\tcgab-0.0.5-x86_64-linux-gnu\n\n"
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
        "Expects one argument, the name of the module to run. "
        "The module is invoked as if by '<arg>'.use.\n\n"
        "The search path begins at the first root. Roots and resources are "
        "checked in descending order.\n"
        "Each resource is checked at each root before moving on to the next.\n"
        "\nROOTS:"
        "\n\t./"
        "\n\t<install_dir>"
        "\n\t<install_dir>/github.com/gab-language/cgab@" GAB_VERSION_TAG "\n"
        "\nRESOURCES:"
        "\n\t<arg>.gab"
        "\n\tmod/<arg>.gab"
        "\n\t<arg>/mod.gab"
        "\n\t<arg>.[so | dylib | dll]"
        "\n\tmod/<arg>.[so | dylib | dll]",
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
            {
                "jobs",
                "Specify the maximum number of threads which Gab may spawn in "
                "parallel." STR(cGAB_DEFAULT_NJOBS),
                'j',
            },
        },
    },
    {
        "exec",
        "Compile and run the string <args>",
        "Compile the string <arg> as Gab code and execute it immediately.",
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
        },
    },
    {
        "repl",
        "Enter the REPL",
        "A REPL is a convenient tool for experimentation.",
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
    cliinfo("This plan will download resources from the internet. Use the -n "
            "or --noisy flag to view the plan. Be sure these are sources you "
            "trust!\nExecute this plan? (y,n) ");

    int ch = getc(stdin);

    if (ch != 'y' && ch != 'Y')
      return 1;
  }

  return 0;
}

#define N_COMMANDS (LEN_CARRAY(commands))

struct command_arguments parse_options(int argc, const char **argv,
                                       struct command command) {
  struct command_arguments args = {
      .argc = argc,
      .argv = argv,
  };

  v_s_char_create(&args.modules, 32);

  for (int i = 0; i < ndefault_modules; i++)
    v_s_char_push(&args.modules, s_char_cstr(default_modules[i]));

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

int get_package(v_step *steps, struct command_arguments *args,
                const char *package, const char *gab_target,
                const char *gab_tag) {

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
  v_char_spush(&bundle, s_char_cstr("cgab-"));
  v_char_spush(&bundle, s_char_cstr(gab_tag));
  v_char_push(&bundle, '-');
  v_char_spush(&bundle, s_char_cstr(gab_target));
  v_char_push(&bundle, '\0');

  v_char url = {};
  v_char_spush(&url, s_char_cstr("http://"));
  v_char_spush(&url, s_char_cstr(pkg));
  v_char_spush(&url, s_char_cstr("/releases/download/"));
  v_char_spush(&url, s_char_cstr(tag));
  v_char_push(&url, '/');
  v_char_spush(&url, s_char_cstr(bundle.data));
  v_char_push(&url, '\0');

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
                         .as.fetch.url = url.data,
                         .as.fetch.dst = bundle_dst.data,
                     });

  v_step_push(steps, (struct step){
                         kSTEP_UNZIP,
                         .as.unzip.src = bundle_dst.data,
                         .as.unzip.dst = pkg_dst.data,
                     });

  v_step_push(steps, (struct step){
                         kSTEP_RM,
                         .as.rm.path = bundle_dst.data,
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

  get_package(steps, args, package.data, gab_target, gab_tag);

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

  v_step steps = {0};
  int res = 0;

  // If we match the special Gab package, then defer to that helper.
  if (!strcmp(pkgbuf, "gab"))
    res = download_gab(&steps, args, platform, tagbuf);
  else
    res = get_package(&steps, args, pkg, platform, GAB_VERSION_TAG);

  if (res)
    return res;

  if (args->flags & FLAG_STEP_VERBOSE)
    logsteps(steps.len, steps.data);

  if (checksteps(args, steps.len, steps.data))
    return clierror("Installation cancelled.\n"), 1;

  if (execute_steps(steps.len, steps.data, args->flags & FLAG_STEP_VERBOSE))
    return clierror("Installation failed.\n"), 1;

  clisuccess("Installation complete.\n");
  return 0;
}

int run(struct command_arguments *args) {
  if (args->argc < 1) {
    clierror("Missing module argument to subcommand 'run'.\n");
    return 1;
  }

  const char *path = args->argv[0];
  size_t jobs = 8;

  /// Push a terminator module to the list
  v_s_char_push(&args->modules, s_char_create(nullptr, 0));

  size_t nmodules = args->modules.len;
  assert(nmodules > 0);

  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args->modules, i)->data;

  return run_file(path, args->flags, args->wait, jobs, nmodules - 1, modules);
}

int exec(struct command_arguments *args) {
  if (args->argc < 1) {
    clierror("Missing code argument to subcommand 'exec'.\n");
    return 1;
  }

  /// Push a terminator module to the list
  v_s_char_push(&args->modules, s_char_create(nullptr, 0));

  size_t nmodules = args->modules.len;
  assert(nmodules > 0);

  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args->modules, i)->data;

  return run_string(args->argv[0], args->flags, args->wait, 8, nmodules - 1,
                    modules);
}

int repl(struct command_arguments *args) {
  /// Push a terminator module to the list
  v_s_char_push(&args->modules, s_char_create(nullptr, 0));

  size_t nmodules = args->modules.len;
  assert(nmodules > 0);

  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args->modules, i)->data;

  return run_repl(args->flags, args->wait, nmodules - 1, modules);
}

void cmd_summary(int i) {
  struct command cmd = commands[i];
  printf("\n\tgab %-8s [opts] <args>\t%s", cmd.name, cmd.desc);
}

void cmd_details(int i) {
  struct command cmd = commands[i];
  printf("USAGE:\n\tgab %4s [opts] <args>\n\n%s\n\nEXAMPLES:", cmd.name,
         cmd.long_desc);

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
  puts(welcome_message);
  puts("\nTo get started, run `gab help` for a list of commands."
       "\n\nIf you've just downloaded gab, welcome! Run `gab get` to complete "
       "your installation.\n");
  return 0;
}

int help(struct command_arguments *args) {
  if (args->argc < 1) {
    printf("To see more details about each command, "
           "run:\n\n\tgab help <cmd>\n\nCOMMANDS:");

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

  size_t modulelen = strlen(module);
  char bundle_buf[modulelen + 5];
  memcpy(bundle_buf, module, modulelen);
  memcpy(bundle_buf + modulelen, ".exe", 5);
  const char *bundle = bundle_buf;

  v_char exepath = {};
  v_char_spush(&exepath, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&exepath, '.');
  v_char_spush(&exepath, s_char_cstr(platform));
  v_char_push(&exepath, '/');
  v_char_push(&exepath, '\0');
  const char *path = gab_osprefix_install(exepath.data);

  v_char_destroy(&exepath);
  v_char_spush(&exepath, s_char_cstr(path));
  v_char_spush(&exepath, s_char_cstr("gab"));
  v_char_push(&exepath, '\0');

  /* The default imported modules require some native modules. Add these. */
  for (int i = 0; i < ndefault_modules_deps; i++)
    v_s_char_push(&args->modules, s_char_cstr(default_modules_deps[i]));

  v_s_char_push(&args->modules, s_char_cstr(module));

  struct gab_resource platform_file_resources[nnative_file_resources + 1];

  memcpy(platform_file_resources, native_file_resources,
         sizeof(native_file_resources));

  v_char platform_dynlib_suffix = {0};
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(".cgab-"));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&platform_dynlib_suffix, '-');
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(platform));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(dynlib_fileending));
  v_char_push(&platform_dynlib_suffix, '\0');

  // Replace the native DYNLIBFILEENDING witht the platform-specific one.
  // This is kinda manual and bug prone if we change resources.
  platform_file_resources[0].suffix = platform_dynlib_suffix.data;
  platform_file_resources[1].suffix = platform_dynlib_suffix.data;

  /*
   * We need our own custom roots here when building a bundled app.
   * This is because we can cross-compile our builds for os/architectures other
   * than our own.
   **/
  const char *platform_roots[] = {
      "./",
      install_location(platform, GAB_VERSION_TAG, nullptr),
      install_location(platform, GAB_VERSION_TAG,
                       "github.com/gab-language/cgab@" GAB_VERSION_TAG),
      nullptr,
  };

  mz_zip_archive zip_o = {0};

  v_step steps = {0};

  v_step_push(&steps,
              (struct step){
                  kSTEP_ARCHIVE_OPEN,
                  .as.archive_open.path = bundle,
                  .as.archive_open.zip = &zip_o,
                  .as.archive_open.initial_data_path = exepath.data,
                  .as.archive_open.initial_data_fallback_path = gab_osexepath(),
              });

  const char *prefixes[args->modules.len];
  const char *suffixes[args->modules.len];

  for (int i = 0; i < args->modules.len; i++)
    v_step_push(
        &steps,
        (struct step){
            kSTEP_ARCHIVE_ADD_MODULE,
            .as.archive_add_module.zip = &zip_o,
            .as.archive_add_module.roots = platform_roots,
            .as.archive_add_module.resources = platform_file_resources,
            .as.archive_add_module.module = v_s_char_val_at(&args->modules, i),
            .as.archive_add_module.prefix_out = prefixes + i,
            .as.archive_add_module.suffix_out = suffixes + i,

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
  if (chmod(bundle, 0755) != 0) {
    perror("chmod");
    return -1;
  }
#endif

  clisuccess("Created bundled executable " GAB_CYAN "%s" GAB_RESET
             " (%2.2lf mb)\n",
             bundle, (double)size / 1024 / 1024);

  return 0;
};

int build_lib(struct command_arguments *args) {
  if (args->modules.len == ndefault_modules)
    return clierror("No modules were requested. See `gab help build`"), 1;

  v_char bundle = {0};
  v_char_spush(&bundle, s_char_cstr("cgab-"));
  v_char_spush(&bundle, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&bundle, '-');
  v_char_spush(&bundle, s_char_cstr(platform));
  v_char_push(&bundle, '\0');

  struct gab_resource platform_file_resources[nnative_file_resources + 1];

  memcpy(platform_file_resources, native_file_resources,
         sizeof(native_file_resources));

  v_char platform_dynlib_suffix = {0};
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(".cgab-"));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&platform_dynlib_suffix, '-');
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(platform));
  v_char_spush(&platform_dynlib_suffix, s_char_cstr(dynlib_fileending));
  v_char_push(&platform_dynlib_suffix, '\0');

  // Replace the native DYNLIBFILEENDING witht the platform-specific one.
  // This is kinda manual and bug prone if we change resources.
  platform_file_resources[0].suffix = platform_dynlib_suffix.data;
  platform_file_resources[1].suffix = platform_dynlib_suffix.data;

  /*
   * We need our own custom roots here when building a bundled app.
   * This is because we can cross-compile our builds for os/architectures other
   * than our own.
   **/
  const char *platform_roots[] = {
      "./",
      install_location(platform, GAB_VERSION_TAG, nullptr),
      install_location(platform, GAB_VERSION_TAG,
                       "github.com/gab-language/cgab@" GAB_VERSION_TAG),
      nullptr,
  };

  mz_zip_archive zip_o = {0};

  v_step steps = {0};

  v_step_push(&steps, (struct step){
                          kSTEP_ARCHIVE_OPEN,
                          .as.archive_open.path = bundle.data,
                          .as.archive_open.zip = &zip_o,
                      });

  const char *prefixes[args->modules.len];
  const char *suffixes[args->modules.len];

  // We skip over the default modules in this case.
  for (int i = ndefault_modules; i < args->modules.len; i++)
    v_step_push(
        &steps,
        (struct step){
            kSTEP_ARCHIVE_ADD_MODULE,
            .as.archive_add_module.zip = &zip_o,
            .as.archive_add_module.roots = platform_roots,
            .as.archive_add_module.resources = platform_file_resources,
            .as.archive_add_module.module = v_s_char_val_at(&args->modules, i),
            .as.archive_add_module.prefix_out = prefixes + i,
            .as.archive_add_module.suffix_out = suffixes + i,

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

/*
 * Maybe we should allow building a python-wheel-like zip?
 *
 * If you don't include a module entrypoint, create a bundle with name:
 *  cgab_GAB_VERSION_TAG_GAB_TARGET
 *
 *  ex: cgab_0.0.5_x86_64-linux-gnu
 *
 *  This should include every module requested in -m or stdin.
 *
 *  When building cross platform modules, the best we can do
 *
 */
int build(struct command_arguments *args) {
  // If we detect that our stdin isn't a terminal (ie its a pipe or a file)
  // we read modules line-by-line from stdin.
  if (!gab_osfisatty(stdin)) {
    char line[MODULE_NAME_MAX];

    while (fgets(line, MODULE_NAME_MAX, stdin)) {
      int len = strlen(line);
      // TODO: Skip whitespace before and after.

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
      a_char *module = a_char_create(line, len + 1);

      // Add the module to our module list.
      v_s_char_push(&args->modules, s_char_cstr(module->data));
    }
  }

  if (args->flags & FLAG_BUILD_TARGET)
    if (update_platform(args))
      return 1;

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
    // TODO: Report this error somehow
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
  roots[2] = install_location(GAB_TARGET_TRIPLE, GAB_VERSION_TAG,
                              "github.com/gab-language/cgab@" GAB_VERSION_TAG);
  roots[3] = nullptr;

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
      v_s_char_destroy(&o.modules);
      return res;
    }
  }

fin:
  struct command cmd = DEFAULT_COMMAND;
  return cmd.handler(&(struct command_arguments){});
}
