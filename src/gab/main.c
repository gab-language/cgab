#include "core.h"
#include "gab.h"
#include "miniz/amalgamation/miniz.c"
#include <locale.h>

#include "linenoise/include/linenoise.h"
#include "miniz/amalgamation/miniz.h"
#include "platform.h"

#define TOSTRING(x) #x
#define STR(x) TOSTRING(x)

#define MAIN_MODULE "gab\\main"

#ifdef GAB_PLATFORM_UNIX
#define GAB_SYMLINK_RECOMMENDATION "ln -sf %s/gab /usr/local/bin"
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

static const char *default_modules[] = {
    "Strings", "Binaries", "Shapes",  "Messages", "Numbers",
    "Blocks",  "Records",  "Fibers",  "Channels", "__core",
    "Ranges",  "IO",       "Streams",
};
static const size_t ndefault_modules = LEN_CARRAY(default_modules);

int run_repl(int flags, size_t nmodules, const char **modules) {
  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .flags = flags,
          .len = nmodules,
          .modules = modules,
          .roots =
              (const char *[]){
                  gab_osprefix(GAB_VERSION_TAG "." GAB_TARGET_TRIPLE),
                  "./",
                  nullptr, // List terminator.
              },
          .resources =
              (struct gab_resource[]){
                  {"mod/", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
                  {"", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
                  {"", "/mod.gab", gab_use_source, file_exister},
                  {"mod/", ".gab", gab_use_source, file_exister},
                  {"", ".gab", gab_use_source, file_exister},
                  {}, // List terminator.
              },
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  linenoiseSetMultiLine(true);
  gab_repl(gab, (struct gab_repl_argt){
                    .name = MAIN_MODULE,
                    .flags = flags,
                    .welcome_message = "Gab version " GAB_VERSION_TAG "",
                    .prompt_prefix = "> ",
                    .result_prefix = "=> ",
                    .readline = linenoise,
                    .add_hist = linenoiseHistoryAdd,
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

int run_string(const char *string, int flags, size_t jobs, size_t nmodules,
               const char **modules) {
  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .flags = flags,
          .jobs = jobs,
          .len = nmodules,
          .modules = modules,
          .roots =
              (const char *[]){
                  gab_osprefix(GAB_VERSION_TAG "." GAB_TARGET_TRIPLE),
                  "./",
                  nullptr, // List terminator.
              },
          .resources =
              (struct gab_resource[]){
                  {"mod/", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
                  {"", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
                  {"", "/mod.gab", gab_use_source, file_exister},
                  {"mod/", ".gab", gab_use_source, file_exister},
                  {"", ".gab", gab_use_source, file_exister},
                  {}, // List terminator.
              },
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

int run_app(const char *mod) {
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
          .len = ndefault_modules,
          .modules = default_modules,
          .roots =
              (const char *[]){
                  "",
                  nullptr,
              },
          .resources =
              (struct gab_resource[]){
                  {"mod/", GAB_DYNLIB_FILEENDING, gab_use_zip_dynlib,
                   zip_exister},
                  {"", GAB_DYNLIB_FILEENDING, gab_use_zip_dynlib, zip_exister},
                  {"", "/mod.gab", gab_use_zip_source, zip_exister},
                  {"mod/", ".gab", gab_use_zip_source, zip_exister},
                  {"", ".gab", gab_use_zip_source, zip_exister},
                  {},
              },
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

int run_file(const char *path, int flags, size_t jobs, size_t nmodules,
             const char **modules) {
  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .flags = flags,
          .jobs = jobs,
          .len = nmodules,
          .modules = modules,
          .roots =
              (const char *[]){
                  gab_osprefix(GAB_VERSION_TAG "." GAB_TARGET_TRIPLE),
                  "./",
                  nullptr, // List terminator.
              },
          .resources =
              (struct gab_resource[]){
                  {"mod/", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
                  {"", GAB_DYNLIB_FILEENDING, gab_use_dynlib, file_exister},
                  {"", "/mod.gab", gab_use_source, file_exister},
                  {"mod/", ".gab", gab_use_source, file_exister},
                  {"", ".gab", gab_use_source, file_exister},
                  {}, // List terminator.
              },
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  union gab_value_pair run_res = gab_use(gab, (struct gab_use_argt){
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
  int argc, flags;
  const char **argv;
  v_s_char modules;
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
  int (*handler)(struct command_arguments);
  struct option options[MAX_OPTIONS];
};

int get(struct command_arguments args);
int run(struct command_arguments args);
int exec(struct command_arguments args);
int repl(struct command_arguments args);
int help(struct command_arguments args);
int build(struct command_arguments args);

#define DEFAULT_COMMAND commands[0]

enum cliflag {
  FLAG_DUMP_AST = fGAB_AST_DUMP,
  FLAG_DUMP_BC = fGAB_BUILD_DUMP,
  FLAG_QUIET_ERR = fGAB_ERR_QUIET,
  FLAG_STRUCT_ERR = fGAB_ERR_STRUCTURED,
  FLAG_BUILD_TARGET = 1 << 5,
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
const struct option quiet_option = {
    "quiet",
    "Do not print errors to the engine's stderr",
    'q',
    .flag = FLAG_QUIET_ERR,
};
const struct option structured_err_option = {
    "sterr",
    "Instead of pretty-printing errors, use a structured output",
    's',
    .flag = FLAG_STRUCT_ERR,
};

bool module_handler(struct command_arguments *args) {
  const char *flag = *args->argv;
  args->argv++;
  args->argc--;

  if (args->argc <= 0) {
    printf("[gab] CLI Error: no argument to flag '%s'\n", flag);
    return false;
  }

  const char *mod = *args->argv;
  args->argv++;
  args->argc--;

  v_s_char_push(&args->modules, s_char_cstr(mod));

  return true;
}

const struct option modules_option = {
    "mods",
    "Change the modules loaded as the gab is initializing"
    "modules",
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
        "related to that subcommand.\n\n\tEG: gab help get",
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
        "package*,"
        "it refers to the local destination gab version.",
        .handler = get,
    },
    {
        "run",
        "Compile and run the module at path <args>",
        "Expects one argument, the name of the module to run. "
        "The module is invoked as if by '<arg>' .use.\n"
        "The search path for use: is as follows:"
        "\n\t- ./<arg>.gab"
        "\n\t- ./mod/<arg>.gab"
        "\n\t- ./<arg>/mod.gab"
        "\n\t- ./<arg>.[so | dll]"
        "\n\t- ./mod/<arg>.[so | dll]"
        "\n\t- <install_dir>/<arg>.gab"
        "\n\t- <install_dir>/mod/<arg>.gab"
        "\n\t- <install_dir>/<arg>/mod.gab"
        "\n\t- <install_dir>/<arg>.[so | dll]"
        "\n\t- <install_dir>/mod/<arg>.[so | dll]"
        "\nNote that relative locations are prioritized over installed ones.",
        .handler = run,
        {
            dumpast_option,
            dumpbytecode_option,
            quiet_option,
            structured_err_option,
            modules_option,
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
        "build",
        "Build a standalone executable for the module <arg>.",
        "Bundle the gab binary and source code for the module <arg> into a "
        "single executable.",
        .handler = build,
        {
            modules_option,
            {
                "target",
                "Set the os-target of build.",
                't',
                .flag = FLAG_BUILD_TARGET,
            },
        },
    },
    {
        "exec",
        "Compile and run the string <args>",
        "Compile the string <arg> as Gab code and execute it immediately.",
        .handler = exec,
        {
            dumpast_option,
            dumpbytecode_option,
            quiet_option,
            structured_err_option,
            modules_option,
        },
    },
    {
        "repl",
        "Enter the read-eval-print loop",
        "A read-eval-print-loop is a convenient tool for expiremtnation.\n"
        "Currently, Gab's repl is quite feature-poor. It is a priority to "
        "improve developer experience in this area.",
        .handler = repl,
        {
            dumpast_option,
            dumpbytecode_option,
            modules_option,
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

  v_s_char_create(&args.modules, 16);

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

      printf("[gab] CLI Error: unrecognized flag '%s'\n", arg);
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

      printf("[gab] CLI Error: unrecognized flag '%s'\n", arg);
      exit(1);
    }

  next:
  }

  return args;
}

#define GAB_RELEASE_DOWNLOAD_URL                                               \
  "https://github.com/gab-language/cgab/releases/download/"

const char *split_pkg(char *pkg) {
  char *cursor = pkg;

  while (*cursor != '@') {
    if (*cursor == '\0')
      return nullptr;

    cursor++;
  }

  *cursor = '\0';

  return ++cursor;
}

int download_gab(const char *pkg, const char *triple) {
  const size_t pkglen = strlen(pkg);
  char pkgbuf[pkglen + 4];
  strncpy(pkgbuf, pkg, pkglen);
  pkgbuf[pkglen] = '\0';

  const char *tag = split_pkg(pkgbuf);

  if (!tag) {
    printf("[gab] CLI Error: Could not resolve package and tag for '%s'\n",
           pkgbuf);
    return false;
  }

  if (!strlen(tag)) {
    printf("[gab] No tag specified. Defaulting to '" GAB_VERSION_TAG "'\n");
    tag = GAB_VERSION_TAG;
  }

  const size_t taglen = strlen(tag);
  char tagbuf[taglen + 1];

  strncpy(tagbuf, tag, taglen);
  tagbuf[taglen] = '\0';

  if (!strlen(pkgbuf)) {
    printf("[gab] No package specified. Defaulting to 'Gab'\n");
    strncpy(pkgbuf, "Gab", 4);
  }

  printf("[gab] Resolved package '%s'\n", pkgbuf);
  printf("[gab] Resolved tag '%s'\n", tagbuf);

  const char *gab_prefix = gab_osprefix("");

  if (gab_prefix == nullptr) {
    printf("[gab] CLI Error: could not determine installation prefix\n");
    return false;
  }

  if (!gab_osmkdirp(gab_prefix)) {
    printf("[gab] CLI Error: Failed to create directory at %s\n", gab_prefix);
    return false;
  };

  size_t triple_len = strlen(triple);
  char locbuf[taglen + triple_len + 2];
  strncpy(locbuf, tag, taglen);
  locbuf[taglen] = '.';
  strncpy(locbuf + taglen + 1, triple, triple_len);
  locbuf[taglen + triple_len + 1] = '\0';

  const char *location_prefix = gab_osprefix(locbuf);

  if (location_prefix == nullptr) {
    printf("[gab] CLI Error: could not determine installation prefix\n");
    return false;
  }

  printf("[gab] Resolved installation prefix: %s\n", location_prefix);

  if (!gab_osmkdirp(location_prefix)) {
    printf("[gab] CLI Error: Failed to create directory at %s\n",
           location_prefix);
    return false;
  };

  v_char location = {};
  v_char_spush(&location, s_char_cstr(location_prefix));
  v_char_spush(&location, s_char_cstr("/gab"));
  v_char_push(&location, '\0');

  if (!strcmp(pkgbuf, "Gab")) {
    v_char url = {};

    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tagbuf));
    v_char_spush(&url, s_char_cstr("/gab-release-"));
    v_char_spush(&url, s_char_cstr(triple));
    v_char_push(&url, '\0');

    // Fetch release binary
    int res = gab_osproc("curl", "-f", "-s", "-L", "-#", "-o", location.data,
                         url.data);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s for target %s.\n",
             tagbuf, triple);
      return false;
    }

#ifdef GAB_PLATFORM_UNIX
    res = gab_osproc("chmod", "+x", location.data);

    if (res) {
      printf("[gab] CLI Error: failed to change permissions of binary %s.",
             location.data);
      return false;
    }
#endif

    v_char_destroy(&location);
    v_char_destroy(&url);

    printf("[gab] Downloaded binary for release: %s.\n", tagbuf);

    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tagbuf));
    v_char_spush(&url, s_char_cstr("/gab-release-"));
    v_char_spush(&url, s_char_cstr(triple));
    v_char_spush(&url, s_char_cstr("-modules"));
    v_char_push(&url, '\0');

    v_char_spush(&location, s_char_cstr(location_prefix));
    v_char_spush(&location, s_char_cstr("/modules"));
    v_char_push(&location, '\0');

    // Fetch release modules
    res = gab_osproc("curl", "-L", "-#", "-o", location.data, url.data);
    printf("[gab] Downloaded modules for release: %s.\n", tagbuf);

    v_char_destroy(&location);
    v_char_destroy(&url);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s", tagbuf);
      return false;
    }

    v_char_spush(&location, s_char_cstr(location_prefix));
    v_char_spush(&location, s_char_cstr("/modules"));
    v_char_push(&location, '\0');

    v_char_spush(&url, s_char_cstr(location_prefix));
    v_char_push(&url, '/');
    v_char_push(&url, '\0');

    res = gab_osproc("tar", "xzf", location.data, "-C", url.data);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s", tagbuf);
      return false;
    }

    printf("[gab] Extracted modules.\n");

    v_char_destroy(&location);
    v_char_destroy(&url);

    // Fetch dev files (libcgab.a, headers)
    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tagbuf));
    v_char_spush(&url, s_char_cstr("/gab-release-"));
    v_char_spush(&url, s_char_cstr(triple));
    v_char_spush(&url, s_char_cstr("-dev"));
    v_char_push(&url, '\0');

    v_char_spush(&location, s_char_cstr(location_prefix));
    v_char_spush(&location, s_char_cstr("/dev"));
    v_char_push(&location, '\0');

    res = gab_osproc("curl", "-L", "-#", "-o", location.data, url.data);
    printf("[gab] Downloaded development files for release: %s.\n", tagbuf);

    v_char_destroy(&location);
    v_char_destroy(&url);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s", tagbuf);
      return false;
    }

    v_char_spush(&location, s_char_cstr(location_prefix));
    v_char_spush(&location, s_char_cstr("/dev"));
    v_char_push(&location, '\0');

    v_char_spush(&url, s_char_cstr(location_prefix));
    v_char_push(&url, '/');
    v_char_push(&url, '\0');

    res = gab_osproc("tar", "xzf", location.data, "-C", url.data);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s", tagbuf);
      return v_char_destroy(&location), v_char_destroy(&url), false;
    }

    printf("[gab] Extracted development files.\n");

    if (!strcmp(triple, GAB_TARGET_TRIPLE)) {
      printf(
          "\nCongratulations! %s@%s successfully installed.\n\n"
          "However, the binary is likely not available in your PATH yet.\n"
          "It is not recommended to add '%s' to PATH directly.\n\nInstead:\n "
          "\tOn systems that support symlinks, link the binary at %s/gab to "
          "some location in PATH already.\n\t\teg: " GAB_SYMLINK_RECOMMENDATION,
          pkgbuf, tagbuf, location_prefix, location_prefix, location_prefix);
    }

    return v_char_destroy(&location), v_char_destroy(&url), true;
  }

  return v_char_destroy(&location), false;
}

int get(struct command_arguments args) {

  const char *pkg = args.argc ? args.argv[0] : "@";

  if (download_gab(pkg, GAB_TARGET_TRIPLE))
    return 0;

  printf("[gab] CLI Error: Installing packages not yet supported\n");
  return 1;
}

int run(struct command_arguments args) {
  if (args.argc < 1) {
    printf("[gab] CLI Error: not enough arguments\n");
    return 1;
  }

  const char *path = args.argv[0];
  size_t jobs = 8;

  size_t nmodules = args.modules.len;
  assert(nmodules > 0);
  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args.modules, i)->data;

  return run_file(path, args.flags, jobs, nmodules, modules);
}

int exec(struct command_arguments args) {
  if (args.argc < 1) {
    printf("[gab] CLI Error: not enough arguments\n");
    return 1;
  }

  size_t nmodules = args.modules.len;
  assert(nmodules > 0);
  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args.modules, i)->data;

  return run_string(args.argv[0], args.flags, 8, nmodules, modules);
}

int repl(struct command_arguments args) {
  size_t nmodules = args.modules.len;
  assert(nmodules > 0);
  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args.modules, i)->data;

  return run_repl(args.flags, nmodules, modules);
}

void cmd_summary(int i) {
  struct command cmd = commands[i];
  printf("\ngab %4s [opts] <args>\t%s\n", cmd.name, cmd.desc);

  for (int j = 0; j < MAX_OPTIONS; j++) {
    struct option opt = cmd.options[j];

    if (!opt.name)
      break;

    printf("\t--%-5s\t-%c\t%s\n", opt.name, opt.shorthand, opt.desc);
  }
}

void cmd_details(int i) {
  struct command cmd = commands[i];
  printf("USAGE:\n\tgab %4s [opts] <args>\n\n%s", cmd.name, cmd.long_desc);

  if (cmd.options[0].name == nullptr)
    return;

  printf("\n\nFLAGS:\n");
  for (int j = 0; j < MAX_OPTIONS; j++) {
    struct option opt = cmd.options[j];

    if (!opt.name)
      break;

    printf("\t--%-5s\t-%c\t%s\n", opt.name, opt.shorthand, opt.desc);
  }
}

int help(struct command_arguments args) {
  if (args.argc < 1) {
    printf("gab\\cli version %s\n", GAB_VERSION_TAG);

    // Print command summaries
    for (int i = 0; i < N_COMMANDS; i++)
      cmd_summary(i);

    return 0;
  }

  const char *subcommand = args.argv[0];

  for (int i = 0; i < N_COMMANDS; i++) {
    struct command cmd = commands[i];
    if (!strcmp(subcommand, cmd.name)) {
      cmd_details(i);
      return 0;
    }
  }

  printf("[gab] CLI Error: unrecognized subcommand '%s'\n", subcommand);
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

bool add_module(mz_zip_archive *zip_o, const char *module) {
  const char *prefix, *suffix;
  const char *path = gab_resolve(gab, module, &prefix, &suffix);

  if (!path) {
    printf("[gab] cli error: Could not resolve module %s\n", module);
    return false;
  }

  size_t lenprefix = strlen(prefix);
  size_t lensuffix = strlen(suffix);
  size_t lenpath = strlen(module);
  char modulename[lenprefix + lenpath + lensuffix + 1];

  memcpy(modulename, prefix, lenprefix);
  memcpy(modulename + lenprefix, module, lenpath);
  memcpy(modulename + lenprefix + lenpath, suffix, lensuffix);
  modulename[lenprefix + lenpath + lensuffix] = '\0';

  printf("[gab] Resolve module %s to %s. Storing in archive as %s.\n", module,
         path, modulename);

  if (!mz_zip_writer_add_file(zip_o, modulename, path, nullptr, 0,
                              MZ_BEST_COMPRESSION)) {
    mz_zip_error e = mz_zip_get_last_error(zip_o);
    const char *estr = mz_zip_get_error_string(e);
    printf("[gab] mz error zipping %s: %s", path, estr);
    mz_zip_writer_end(zip_o);
    return false;
  }

  return true;
}

const char *target = GAB_TARGET_TRIPLE;
const char *dynlib_fileending = GAB_DYNLIB_FILEENDING;

int build(struct command_arguments args) {
  if (args.flags & FLAG_BUILD_TARGET) {
    if (args.argc == 0) {
      printf("[gab] CLI ERROR: Expected argument\n");
      return 1;
    }

    target = args.argv[0];

    if (!strcmp(target, "x86_64-linux-gnu")) {
      dynlib_fileending = ".so";
    } else if (!strcmp(target, "x86_64-macos-none")) {
      dynlib_fileending = ".dylib";
    } else if (!strcmp(target, "x86_64-windows-gnu")) {
      dynlib_fileending = ".dll";
    } else if (!strcmp(target, "aarch64-linux-gnu")) {
      dynlib_fileending = ".so";
    } else if (!strcmp(target, "aarch64-macos-none")) {
      dynlib_fileending = ".dylib";
    } else if (!strcmp(target, "aarch64-windows-gnu")) {
      dynlib_fileending = ".dll";
    } else {
      printf("[gab] CLI ERROR: Unrecognized target %s\n", target);
      return 1;
    }

    args.argv++;
    args.argc--;

    printf("[gab] Build target is %s: %s\n", target, dynlib_fileending);
    if (!download_gab("@", target))
      return 1;
  }

  size_t nmodules = args.modules.len;
  assert(nmodules > 0);
  const char *modules[nmodules];
  for (int i = 0; i < nmodules; i++)
    modules[i] = v_s_char_ref_at(&args.modules, i)->data;

  v_char location = {};
  v_char_spush(&location, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&location, '.');
  v_char_spush(&location, s_char_cstr(target));
  v_char_push(&location, '\0');

  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .len = nmodules,
          .modules = modules,
          .roots =
              (const char *[]){
                "./",
                  gab_osprefix(location.data),
                  nullptr,
              },
          .resources =
              (struct gab_resource[]){
                  {"mod/", dynlib_fileending, gab_use_dynlib, file_exister},
                  {"", dynlib_fileending, gab_use_dynlib, file_exister},
                  {"", "/mod.gab", gab_use_source, file_exister},
                  {"mod/", ".gab", gab_use_source, file_exister},
                  {"", ".gab", gab_use_source, file_exister},
                  {},
              },
      },
      &gab);

  if (!check_and_printerr(&res))
    return gab_destroy(gab), 1;

  if (args.argc < 1) {
    printf("[gab] CLI ERROR: Missing bundle arguments to build subcommand");
    return 1;
  }

  const char *bundle = args.argv[0];
  size_t bundle_len = strlen(bundle);
  char bundle_buf[bundle_len + 5];
  memcpy(bundle_buf, bundle, bundle_len);
  memcpy(bundle_buf + bundle_len, ".exe", 5);
  bundle = bundle_buf;

  args.argv++;
  args.argc--;

  v_char exepath = {};
  v_char_spush(&exepath, s_char_cstr(GAB_VERSION_TAG));
  v_char_push(&exepath, '.');
  v_char_spush(&exepath, s_char_cstr(target));
  v_char_push(&exepath, '\0');
  const char *path = gab_osprefix(exepath.data);

  v_char_destroy(&exepath);
  v_char_spush(&exepath, s_char_cstr(path));
  v_char_spush(&exepath, s_char_cstr("gab"));
  v_char_push(&exepath, '\0');

  // TODO: Use correct loaders for the given triple here.

  FILE *exe = fopen(exepath.data, "r");
  if (!exe) {
    printf("[gab] CLI Error: Could not open exe file at: %s\n", exepath.data);
    return 1;
  }
  v_char_destroy(&exepath);

  FILE *bundle_f = fopen(bundle, "w");
  if (!bundle_f) {
    printf("[gab] CLI Error: Could not open bundle file: %s\n", bundle);
    return 1;
  }

  copy_file(exe, bundle_f);

  mz_zip_archive zip_o = {0};

  if (!mz_zip_writer_init_cfile(&zip_o, bundle_f, 0)) {
    mz_zip_error e = mz_zip_get_last_error(&zip_o);
    const char *estr = mz_zip_get_error_string(e);
    printf("[gab] mz error: %s", estr);
    return 1;
  }

  for (int i = 0; i < ndefault_modules; i++) {
    const char *module = default_modules[i];
    if (!add_module(&zip_o, module))
      return 1;
  }

  for (int i = 0; i < args.argc; i++) {
    const char *module = args.argv[i];
    if (!add_module(&zip_o, module))
      return 1;
  }

  if (!mz_zip_writer_finalize_archive(&zip_o)) {
    mz_zip_error e = mz_zip_get_last_error(&zip_o);
    const char *estr = mz_zip_get_error_string(e);
    printf("[gab] mz Error: %s", estr);
    mz_zip_writer_end(&zip_o);
    return 1;
  }

  mz_zip_writer_end(&zip_o);

  fclose(bundle_f);
  fclose(exe);
  gab_destroy(gab);

#if GAB_PLATFORM_UNIX
  if (chmod(bundle, 0755) != 0) {
    perror("chmod");
    return -1;
  }
#endif

  printf("[gab] Success! Bundled module can be found at: %s\n", bundle);
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
    mz_zip_error e = mz_zip_get_last_error(&zip);
    printf("ZIPERROR: %s\n", mz_zip_get_error_string(e));
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

  if (check_not_gab(argv[0])) {
    if (check_valid_zip()) {
      return run_app(argv[0]);
    }
  }

  if (argc < 2)
    goto fin;

  gab_ossignal(SIGINT, propagate_term);

  for (int i = 0; i < N_COMMANDS; i++) {
    struct command cmd = commands[i];
    assert(cmd.handler);

    if (!strcmp(argv[1], cmd.name)) {
      struct command_arguments o = parse_options(argc - 2, argv + 2, cmd);

      int res = cmd.handler(o);
      v_s_char_destroy(&o.modules);
      return res;
    }
  }

fin:
  struct command cmd = DEFAULT_COMMAND;
  return cmd.handler((struct command_arguments){});
}
