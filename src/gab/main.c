#include "core.h"
#include "gab.h"
#include <locale.h>
#include <stddef.h>

#include "crossline/crossline.c"
#include "crossline/crossline.h"

#include "miniz/amalgamation/miniz.c"
#include "miniz/amalgamation/miniz.h"

#define SOKOL_IMPL
#include "sokol/sokol_fetch.h"
#include "sokol/sokol_log.h"

#include "colors.h"
#include "platform.h"

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

// TODO:
// Simply replace curl and tar with
// sokol_fetch and miniz implementations. Much better than subprocessing.

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

  fprintf(stderr, "[" GAB_CYAN "gab" GAB_RESET "] ");
  vfprintf(stderr, fmt, args);

  va_end(args);
}

void fetch_cb(const sfetch_response_t *res) {
  if (res->fetched) {
    FILE *dst = *(FILE **)res->user_data;
    cliinfo("Writing %lu bytes\n", res->data.size);
    size_t bytes = fwrite(res->data.ptr, res->data.size, 1, dst);
    if (bytes < res->data.size)
      clierror("Wrote too few bytes\n");
  }

  if (res->failed) {
    clierror("Response failed for %s: %i\n", res->path, res->error_code);
  }

  if (res->failed) {
    FILE *dst = *(FILE **)res->user_data;
    fflush(dst);
    fclose(dst);
  }
}

void nfetch(size_t n, const char *url[n], const char *file[n]) {
  sfetch_setup(&(sfetch_desc_t){
      .max_requests = 1024,
      .num_channels = 4,
      .num_lanes = 8,
      .logger.func = slog_func,
  });

  sfetch_handle_t handles[n];

  for (size_t i = 0; i < n; i++) {
    FILE *dst = fopen(file[i], "w");

    if (!dst) {
      clierror("Unable to open file: %s", file[i]);
      return;
    };

    cliinfo("REQUESTING %s\n", url[i]);

    void *buf = malloc(4096);
    sfetch_handle_t handle = sfetch_send(&(sfetch_request_t){
        .path = url[i],
        .callback = fetch_cb,
        .chunk_size = 4096,
        .user_data =
            {
                .ptr = &dst,
                .size = sizeof(FILE *),
            },
        .buffer =
            {
                .ptr = buf,
                .size = 4096,
            },
    });
    handles[i] = handle;

    if (!sfetch_handle_valid(handle)) {
      clierror("Error creating sfetch request\n");
    }
  }

  for (;;) {
    bool workdone = true;

    // When all handles return false, our work is done.
    for (size_t i = 0; i < n; i++) {
      if (sfetch_handle_valid(handles[i])) {
        cliinfo("Handle %i is still going\n", i);
        workdone = false;
        break;
      } else {
        cliinfo("Handle %i is done\n", i);
      }
    }

    cliinfo("Work done? %b\n", workdone);
    if (workdone)
      break;

    sfetch_dowork();
  }

  clisuccess("Downloaded %lu resources.\n", n);

  sfetch_shutdown();
}

void print_err(struct gab_triple gab, gab_value err) {
  const char *str = gab_errtocs(gab, err);
  fprintf(stderr, "%s\n", str);
}

void pop_and_printerr(struct gab_triple gab) {
  gab_value *errors = gab_egerrs(gab.eg);
  assert(errors != nullptr);

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
    }
    return false;
  }

  if (res->aresult->data[0] != gab_ok) {
    const char *errstr = gab_errtocs(gab, res->aresult->data[1]);
    assert(errstr != nullptr);
    fputs(errstr, stderr);
    return a_gab_value_destroy(res->aresult), false;
  }

  return true;
}

typedef union gab_value_pair (*module_f)(struct gab_triple);

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

  module_f mod = (module_f)gab_oslibfind(lib, GAB_DYNLIB_MAIN);

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

  v_char dst = {};

#ifdef GAB_PLATFORM_UNIX
  // TODO: This should really be organized per-process, as multiple gab-apps
  // can be opened at the same time, and we don't want them to stomp over each
  // other.
  // TODO: Properly create directories that are nested.
  // The filename here can be something like 'mod/other_lib/sub/example'
  // We need to walk down this path, creating directories in /tmp/gab
  if (!gab_osmkdirp("/tmp/gab"))
    return gab_panicf(gab, "Failed to create temporary file folder.");

  if (!gab_osmkdirp("/tmp/gab/mod"))
    return gab_panicf(gab, "Failed to create temporary file folder.");

  v_char_spush(&dst, s_char_cstr("/tmp/gab/"));
  v_char_spush(&dst, s_char_cstr(stat.m_filename));
  v_char_push(&dst, '\0');
#elifdef GAB_PLATFORM_WIN
#error WINDOWS NOT IMPLEMENTED YET
#else
#error UNKNOWN PLATFORM
#endif

  if (!mz_zip_reader_extract_file_to_file(&zip, stat.m_filename, dst.data, 0)) {
    mz_zip_error e = mz_zip_get_last_error(&zip);
    const char *estr = mz_zip_get_error_string(e);
    return gab_panicf(gab, "Failed to load zipped module: $",
                      gab_string(gab, estr));
  }

  union gab_value_pair res = gab_use_dynlib(gab, dst.data, len, sargs, vargs);

  v_char_destroy(&dst);

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
      gab_aexec(gab, (struct gab_exec_argt){
                         .name = path,
                         .source = (const char *)src->data,
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

static const struct gab_resource file_resources[] = {
    {"mod/", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
    {"", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
    {"", "/mod.gab", gab_use_source, file_exister},
    {"mod/", ".gab", gab_use_source, file_exister},
    {"", ".gab", gab_use_source, file_exister},
    {}, // List terminator.
};

static const struct gab_resource zip_resources[] = {
    {"mod/", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
    {"", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
    {"", "/mod.gab", gab_use_source, file_exister},
    {"mod/", ".gab", gab_use_source, file_exister},
    {"", ".gab", gab_use_source, file_exister},
    {}, // List terminator.
};

static const char *roots[3] = {};

static char prompt_buffer[4096];
char *readline(const char *prompt) {
  return crossline_readline(prompt, prompt_buffer, sizeof(prompt_buffer));
}

int run_repl(int flags, uint32_t wait, size_t nmodules, const char **modules) {
  gab_ossignal(SIGINT, propagate_term);

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .wait = wait ? wait : 50000,
          .modules = modules,
          .roots = roots,
          .resources = file_resources,
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  gab_repl(gab, (struct gab_repl_argt){
                    .name = MAIN_MODULE,
                    .flags = flags,
                    .welcome_message =
                        "Gab version " GAB_VERSION_TAG "\tPress ^C to exit.",
                    .prompt_prefix = ">>> ",
                    .promptmore_prefix = ">   ",
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
          .resources = file_resources,
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
          .resources = zip_resources,
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
          .resources = file_resources,
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

#define MAX_OPTIONS 7

struct command {
  const char *name;
  const char *desc;
  const char *long_desc;
  const char *example;
  int (*handler)(struct command_arguments *);
  struct option options[MAX_OPTIONS];
};

int get(struct command_arguments *args);
int run(struct command_arguments *args);
int exec(struct command_arguments *args);
int repl(struct command_arguments *args);
int help(struct command_arguments *args);
int build(struct command_arguments *args);

#define DEFAULT_COMMAND commands[0]

enum cliflag {
  FLAG_DUMP_AST = fGAB_AST_DUMP,
  FLAG_DUMP_BC = fGAB_BUILD_DUMP,
  FLAG_STRUCT_ERR = fGAB_ERR_STRUCTURED,
  FLAG_BUILD_TARGET = 1 << 4,
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

bool platform_handler(struct command_arguments *args) {
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

static struct command commands[] = {
    {
        "help",
        "Print this message, or learn more about the subcommand given by "
        "<args>",
        "With no arguments, prints a general help message summarizing all "
        "available subcommands and their flags.\n"
        "With a subcommand given by <arg>, print more specific information "
        "related to that subcommand.",
        .example = "gab help get",
        .handler = help,
    },
    {
        "get",
        "Install Gab and/or packages given by <args>",
        "With no arguments, installs Gab's builtin modules for the release "
        "corresponding to this binary's version.\n"
        "Accepts arguments in the shape <package>@<tag>, where:"
        "\n\t- <package> corresponds to a valid gab package"
        "\n\t- <tag> corresponds to a valid, local gab version"
        "\n\nNote that the tag does not correspond to a version *of the "
        "package*, "
        "it refers to the local destination gab version.",
        .example = "gab get @0.0.5",
        .handler = get,
    },
    {
        "build",
        "Build a standalone executable for the module <arg>.",
        "Bundle the gab binary and source code for the module <arg> into a "
        "single executable.",
        .example = "gab build -m IO,Strings my_app",
        .handler = build,
        {
            modules_option,
            {
                "plat",
                "Set the platform of the build",
                'p',
                .flag = FLAG_BUILD_TARGET,
                .handler_f = platform_handler,
            },
        },
    },
    {
        "run",
        "Compile and run the module at path <args>",
        "Expects one argument, the name of the module to run. "
        "The module is invoked as if by '<arg>'.use.\n\n"
        "The search path begins at the first root. Roots and resources are checked in descending order.\n"
        "Each resource is checked at that root before moving on to the next.\n"
        "\nROOTS:"
        "\n\t./"
        "\n\t<install_dir>\n"
        "\nRESOURCES:"
        "\n\t<arg>.gab"
        "\n\tmod/<arg>.gab"
        "\n\t<arg>/mod.gab"
        "\n\t<arg>.[so | dylib | dll]"
        "\n\tmod/<arg>.[so | dylib | dll]",
        .example = "gab run -m Json,http -j 16 my_project",
        .handler = run,
        {
            dumpast_option,
            dumpbytecode_option,
            structured_err_option,
            modules_option,
            busywait_option,
            {
                "jobs",
                "Specify the number of os threads which should serve as "
                "workers for running fibers. Default is " STR(
                    cGAB_DEFAULT_NJOBS),
                'j',
            },
        },
    },
    {
        "exec",
        "Compile and run the string <args>",
        "Compile the string <arg> as Gab code and execute it immediately.",
        .example = "gab exec -a -d \"'hello'.println\"",
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
        "Enter the read-eval-print loop",
        "A read-eval-print-loop is a convenient tool for expiremtnation.",
        .example = "gab repl -m Json",
        .handler = repl,
        {
            dumpast_option,
            dumpbytecode_option,
            modules_option,
            busywait_option,
        },
    },
};

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
  char *cursor = pkg;

  while (*cursor != '@') {
    if (*cursor == '\0')
      return cursor;

    cursor++;
  }

  *cursor = '\0';

  return ++cursor;
}

int download_gab(const char *pkg, const char *tag, const char *triple) {

  int taglen = strlen(tag);

  size_t triple_len = strlen(triple);
  char locbuf[taglen + triple_len + 2];
  strncpy(locbuf, tag, taglen);
  locbuf[taglen] = '.';
  strncpy(locbuf + taglen + 1, triple, triple_len);
  locbuf[taglen + triple_len + 1] = '\0';

  const char *location_prefix = gab_osprefix(locbuf);

  if (location_prefix == nullptr) {
    clierror("Could not determine installation prefix.\n");
    return false;
  }

  cliinfo("Resolved installation prefix: " GAB_MAGENTA "%s" GAB_RESET ".\n",
          location_prefix);

  v_char binary_location = {};
  v_char_spush(&binary_location, s_char_cstr(location_prefix));
  v_char_spush(&binary_location, s_char_cstr("/gab"));
  v_char_push(&binary_location, '\0');

  v_char binary_url = {};
  v_char_spush(&binary_url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&binary_url, s_char_cstr(tag));
  v_char_spush(&binary_url, s_char_cstr("/gab-release-"));
  v_char_spush(&binary_url, s_char_cstr(triple));
  v_char_push(&binary_url, '\0');

  v_char modules_location = {};
  v_char_spush(&modules_location, s_char_cstr(location_prefix));
  v_char_spush(&modules_location, s_char_cstr("modules"));
  v_char_push(&modules_location, '\0');

  v_char modules_url = {};
  v_char_spush(&modules_url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&modules_url, s_char_cstr(tag));
  v_char_spush(&modules_url, s_char_cstr("/gab-release-"));
  v_char_spush(&modules_url, s_char_cstr(triple));
  v_char_spush(&modules_url, s_char_cstr("-modules"));
  v_char_push(&modules_url, '\0');

  v_char modules_extract_location = {};
  v_char_spush(&modules_extract_location, s_char_cstr(location_prefix));
  v_char_push(&modules_extract_location, '\0');

  // Fetch dev files (libcgab.a, headers)
  v_char dev_url = {};
  v_char_spush(&dev_url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&dev_url, s_char_cstr(tag));
  v_char_spush(&dev_url, s_char_cstr("/gab-release-"));
  v_char_spush(&dev_url, s_char_cstr(triple));
  v_char_spush(&dev_url, s_char_cstr("-dev"));
  v_char_push(&dev_url, '\0');

  v_char dev_location = {};
  v_char_spush(&dev_location, s_char_cstr(location_prefix));
  v_char_spush(&dev_location, s_char_cstr("dev"));
  v_char_push(&dev_location, '\0');

  v_char dev_extract_location = {};
  v_char_spush(&dev_extract_location, s_char_cstr(location_prefix));
  v_char_push(&dev_extract_location, '\0');

  cliinfo("The following steps will be taken:\n");
  cliinfo("(1) Create the " GAB_MAGENTA "%s" GAB_RESET
          " directory, if it does not exist.\n",
          location_prefix);
  cliinfo("(2) Download (via curl) the gab binary: " GAB_MAGENTA "%s" GAB_RESET
          "\n",
          binary_url.data);
  cliinfo("(3) Download (via curl) gab's builtin modules: " GAB_MAGENTA
          "%s" GAB_RESET "\n",
          modules_url.data);
  cliinfo("(4) Download (via curl) gab's development files: " GAB_MAGENTA
          "%s" GAB_RESET "\n",
          dev_url.data);
  cliinfo("(5) Extract (via tar) the downloaded modules: " GAB_MAGENTA
          "%s" GAB_RESET " => " GAB_MAGENTA "%s" GAB_RESET "\n",
          modules_location.data, modules_extract_location.data);
  cliinfo("(6) Extract (via tar) the downloaded development files: " GAB_MAGENTA
          "%s" GAB_RESET " => " GAB_MAGENTA "%s" GAB_RESET "\n",
          dev_location.data, dev_extract_location.data);

  cliinfo("Begin installation process? (y,n) ");
  int ch = getc(stdin);
  if (ch != 'y' && ch != 'Y') {
    clierror("Installation cancelled.\n");
    return false;
  }

  /// Ensure that the ~/gab folder exists.
  const char *gab_prefix = gab_osprefix("");

  if (gab_prefix == nullptr) {
    clierror("(1) Could not determine installation prefix.\n");
    return false;
  }

  if (!gab_osmkdirp(gab_prefix)) {
    clierror("(1) Failed to create directory at " GAB_MAGENTA "%s" GAB_RESET
             ".\n",
             gab_prefix);
    return false;
  };

  if (!gab_osmkdirp(location_prefix)) {
    clierror("(1) Failed to create directory at " GAB_MAGENTA "%s" GAB_RESET
             ".\n",
             location_prefix);
    return false;
  };

  clisuccess("(1) Validated installation location.\n");

  nfetch(3,
         (const char *[]){
             binary_url.data,
             modules_url.data,
             dev_url.data,
         },
         (const char *[]){
             binary_location.data,
             modules_location.data,
             dev_location.data,
         }

  );

  return 1;

  // Fetch release binary
  int res = gab_osproc("curl", "-f", "-s", "-L", "-o", binary_location.data,
                       binary_url.data);

  if (res) {
    clierror("(2) Failed to download release " GAB_YELLOW "%s" GAB_RESET
             " for target " GAB_YELLOW "%s" GAB_RESET ".\n",
             tag, triple);
    return false;
  }

  clisuccess("(2) Downloaded binary for release: " GAB_YELLOW "%s" GAB_RESET
             ".\n",
             tag);

  // Fetch release modules
  res = gab_osproc("curl", "-f", "-s", "-L", "-o", modules_location.data,
                   modules_url.data);
  clisuccess("(3) Downloaded modules for release: " GAB_YELLOW "%s" GAB_RESET
             ".\n",
             tag);

  if (res) {
    clierror("(3) Failed to download modules for release " GAB_YELLOW
             "%s" GAB_RESET ".",
             tag);
    return false;
  }

  res = gab_osproc("curl", "-f", "-s", "-L", "-o", dev_location.data,
                   dev_url.data);
  clisuccess("(4) Downloaded development files for release: " GAB_YELLOW
             "%s" GAB_RESET ".\n",
             tag);

  if (res) {
    clierror("(4) Failed to download development files for release %s", tag);
    return false;
  }

  res = gab_osproc("(5) tar", "xzf", modules_location.data, "-C",
                   modules_extract_location.data);

  if (res) {
    clierror("(5) Failed to extract module files for release " GAB_YELLOW
             "%s" GAB_RESET ".",
             tag);
    return false;
  }

  clisuccess("(5) Extracted modules.\n");

  res = gab_osproc("tar", "xzf", dev_location.data, "-C",
                   dev_extract_location.data);

  if (res) {
    clierror("(6) Failed to extract development files for release " GAB_YELLOW
             "%s" GAB_RESET "",
             tag);
    return false;
  }

  clisuccess("(6) Extracted development files.\n");

  if (!strcmp(triple, GAB_TARGET_TRIPLE)) {
    clisuccess(
        "Congratulations! " GAB_GREEN "%s" GAB_RESET "@" GAB_YELLOW
        "%s" GAB_RESET " successfully installed.\n\n"
        "However, the binary is likely not available in your PATH yet.\n"
        "It is not recommended to add " GAB_MAGENTA "%s" GAB_RESET
        " to PATH directly.\n\nInstead:\n "
        "\tOn systems that support symlinks, link the binary at %sgab to "
        "some location in PATH already.\n\t\teg: " GAB_SYMLINK_RECOMMENDATION,
        pkg, tag, location_prefix, location_prefix, location_prefix);
  }

  return true;
}

int get(struct command_arguments *args) {

  const char *pkg = args->argc ? args->argv[0] : "@";

  /// Ensure that the ~/gab folder exists.
  const char *gab_prefix = gab_osprefix("");

  if (gab_prefix == nullptr) {
    clierror("Could not determine installation prefix.\n");
    return false;
  }

  if (!gab_osmkdirp(gab_prefix)) {
    clierror("Failed to create directory at " GAB_MAGENTA "%s" GAB_RESET ".\n",
             gab_prefix);
    return false;
  };

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
  if (!strlen(pkgbuf)) {
    cliinfo("No package specified. Defaulting to " GAB_GREEN "'Gab'" GAB_RESET
            ".\n");
    strncpy(pkgbuf, "Gab", 4);
  }

  cliinfo("Resolved package " GAB_GREEN "%s" GAB_RESET ".\n", pkgbuf);

  /*
   * If we didn't find a tag in the package, then that *might* be an
   * unrecoverable error. If the user meant to download the builtin Gab package,
   * then we have a sane default. Otherwise, we error.
   */
  if (!taglen) {
    if (!strcmp(pkgbuf, "Gab")) {
      cliinfo("No tag specified. Defaulting to " GAB_YELLOW
                  GAB_VERSION_TAG GAB_RESET ".\n");
      strncpy(tagbuf, GAB_VERSION_TAG, 10 + taglen);
    } else {
      clierror("A tag must be specfied. Try " GAB_GREEN "%s" GAB_RESET
               "@" GAB_YELLOW "<some tag>" GAB_RESET ".\n",
               pkgbuf);
      return 1;
    }
  }

  cliinfo("Resolved tag " GAB_YELLOW "%s" GAB_RESET ".\n", tagbuf);

  /*
   * Now we have resolved a valid tag and package.
   */

  // If we match the special Gab package, then defer to that helper.
  if (!strcmp(pkgbuf, "Gab"))
    if (download_gab(pkgbuf, tagbuf, GAB_TARGET_TRIPLE))
      return 0;

  clierror("The 'get' subcommand does not support downloading other "
           "packages yet.\n");
  return 1;
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
  printf("\ngab %4s [opts] <args>\t%s\n", cmd.name, cmd.desc);

  for (int j = 0; j < MAX_OPTIONS; j++) {
    struct option opt = cmd.options[j];

    if (!opt.name)
      break;

    printf("\t--%-5s\t-%c\t%s.\n", opt.name, opt.shorthand, opt.desc);
  }
}

void cmd_details(int i) {
  struct command cmd = commands[i];
  printf("USAGE:\n\tgab %4s [opts] <args>\n\n%s\n\nEXAMPLE:\n\t%s", cmd.name, cmd.long_desc, cmd.example);

  if (cmd.options[0].name == nullptr)
    return;

  printf("\n\nFLAGS:\n");
  for (int j = 0; j < MAX_OPTIONS; j++) {
    struct option opt = cmd.options[j];

    if (!opt.name)
      break;

    printf("\t--%-5s\t-%c\t%s.\n", opt.name, opt.shorthand, opt.desc);
  }
}

int help(struct command_arguments *args) {
  if (args->argc < 1) {
    printf("gab\\cli version %s.\n", GAB_VERSION_TAG);

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

bool add_module(mz_zip_archive *zip_o, const char **roots,
                const struct gab_resource *resources, s_char module) {

  char cstr_module[module.len + 1];
  memcpy(cstr_module, module.data, module.len);
  cstr_module[module.len] = '\0';

  const char *prefix, *suffix;
  const char *path =
      gab_mresolve(roots, resources, cstr_module, &prefix, &suffix);

  // If the path ends in *mod.gab*, we should treat the whole directory as a
  // module and add it.

  if (!path) {
    clierror("Could not resolve module " GAB_GREEN "%s" GAB_RESET ".\n",
             cstr_module);
    return false;
  }

  size_t lenprefix = strlen(prefix);
  size_t lensuffix = strlen(suffix);
  size_t lenpath = strlen(cstr_module);
  char modulename[lenprefix + lenpath + lensuffix + 1];

  memcpy(modulename, prefix, lenprefix);
  memcpy(modulename + lenprefix, cstr_module, lenpath);
  memcpy(modulename + lenprefix + lenpath, suffix, lensuffix);
  modulename[lenprefix + lenpath + lensuffix] = '\0';

  cliinfo("Resolved module " GAB_GREEN "%s" GAB_GREEN "\n\t" GAB_MAGENTA
          "%s" GAB_RESET " " GAB_GREEN "=>" GAB_RESET " " GAB_YELLOW
          "<archive>/%s" GAB_RESET "\n",
          cstr_module, path, modulename);

  /*
   * It is unclear whether it is more important to prioritize speed
   * (which affects startup/load time)
   * or compression
   * (which affects bundle size).
   *
   * Perhaps leave this up to the user?
   */
  if (!mz_zip_writer_add_file(zip_o, modulename, path, nullptr, 0,
                              MZ_BEST_SPEED)) {
    mz_zip_error e = mz_zip_get_last_error(zip_o);
    const char *estr = mz_zip_get_error_string(e);
    clierror("Failed to add file to archive '%s' - %s.\n", path, estr);
    mz_zip_writer_end(zip_o);
    return false;
  }

  return true;
}

const char *platform = GAB_TARGET_TRIPLE;
const char *dynlib_fileending = GAB_DYNLIB_FILEENDING;

int build(struct command_arguments *args) {
  if (args->flags & FLAG_BUILD_TARGET) {
    platform = args->platform;

    if (!strcmp(platform, "x86_64-linux-gnu")) {
      dynlib_fileending = ".so";
    } else if (!strcmp(platform, "x86_64-macos-none")) {
      dynlib_fileending = ".dylib";
    } else if (!strcmp(platform, "x86_64-windows-gnu")) {
      dynlib_fileending = ".dll";
    } else if (!strcmp(platform, "aarch64-linux-gnu")) {
      dynlib_fileending = ".so";
    } else if (!strcmp(platform, "aarch64-macos-none")) {
      dynlib_fileending = ".dylib";
    } else if (!strcmp(platform, "aarch64-windows-gnu")) {
      dynlib_fileending = ".dll";
    } else {
      clierror("Unrecognized platform '%s'.\n", platform);
      return 1;
    }

    cliinfo("Build platform is %s.\n", platform, dynlib_fileending);
    if (!download_gab("Gab", GAB_VERSION_TAG, platform)) {
      clierror("Continuing. Core modules may be missing.\n");
    }
  }

  /* We need our own custom roots here when building a bundled app.
   * This is because we can cross-compile our builds for os/architectures other
   * than our own.
   **/

  v_char location = {};
  v_char_spush(&location, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&location, '.');
  v_char_spush(&location, s_char_cstr(platform));
  v_char_push(&location, '\0');

  const char *roots[] = {
      "./",
      gab_osprefix(location.data),
      nullptr,
  };

  if (args->argc < 1) {
    clierror("Missing bundle argument to build subcommand.\n");
    return 1;
  }

  const char *module = args->argv[0];
  size_t module_len = strlen(module);
  char bundle_buf[module_len + 5];
  memcpy(bundle_buf, module, module_len);
  memcpy(bundle_buf + module_len, ".exe", 5);
  const char *bundle = bundle_buf;

  args->argv++;
  args->argc--;

  v_char exepath = {};
  v_char_spush(&exepath, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&exepath, '.');
  v_char_spush(&exepath, s_char_cstr(platform));
  v_char_push(&exepath, '\0');
  const char *path = gab_osprefix(exepath.data);

  v_char_destroy(&exepath);
  v_char_spush(&exepath, s_char_cstr(path));
  v_char_spush(&exepath, s_char_cstr("gab"));
  v_char_push(&exepath, '\0');

  FILE *exe = fopen(exepath.data, "r");
  if (!exe) {
    clierror("Failed to open gab executable at '%s'.\n", exepath.data);
    clierror("Falling back to this binary, Gab@" GAB_VERSION_TAG "\n");
    const char *path = gab_osexepath();
    exe = fopen(path, "r");
    if (!exe) {
      clierror("Failed to open gab executable at '%s'.\n", path);
      return 1;
    }
  }
  v_char_destroy(&exepath);

  FILE *bundle_f = fopen(bundle, "w");
  if (!bundle_f) {
    clierror("Failed to open bundle file '%s' to write.\n", bundle);
    return 1;
  }

  copy_file(exe, bundle_f);

  mz_zip_archive zip_o = {0};

  if (!mz_zip_writer_init_cfile(&zip_o, bundle_f, 0)) {
    mz_zip_error e = mz_zip_get_last_error(&zip_o);
    const char *estr = mz_zip_get_error_string(e);
    clierror("Failed to initialize zip archive: %s.\n", estr);
    return 1;
  }

  /* The default imported modules require some native modules. Add these. */
  for (int i = 0; i < ndefault_modules_deps; i++)
    v_s_char_push(&args->modules, s_char_cstr(default_modules_deps[i]));

  v_s_char_push(&args->modules, s_char_cstr(module));

  for (int i = 0; i < args->modules.len; i++)
    if (!add_module(&zip_o, roots, file_resources,
                    v_s_char_val_at(&args->modules, i)))
      return 1;

  if (!mz_zip_writer_finalize_archive(&zip_o)) {
    mz_zip_error e = mz_zip_get_last_error(&zip_o);
    const char *estr = mz_zip_get_error_string(e);
    clierror("Failed to finalize zip archive: %s.\n", estr);
    mz_zip_writer_end(&zip_o);
    return 1;
  }

  if (!mz_zip_writer_end(&zip_o)) {
    mz_zip_error e = mz_zip_get_last_error(&zip_o);
    const char *estr = mz_zip_get_error_string(e);
    clierror("Failed to cleanup zip archive: %s.\n", estr);
    return 1;
  }

  long size = ftell(bundle_f);

  fclose(bundle_f);
  fclose(exe);

#if GAB_PLATFORM_UNIX
  if (chmod(bundle, 0755) != 0) {
    perror("chmod");
    return -1;
  }
#endif

  clisuccess("Created bundled module " GAB_CYAN "%s" GAB_RESET " (%2.2lf mb)\n",
             bundle, (double)size / 1024 / 1024);
  return 0;
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
  roots[1] = gab_osprefix(GAB_VERSION_TAG "." GAB_TARGET_TRIPLE);
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
      v_s_char_destroy(&o.modules);
      return res;
    }
  }

fin:
  struct command cmd = DEFAULT_COMMAND;
  return cmd.handler(&(struct command_arguments){});
}
