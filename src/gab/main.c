#include "core.h"
#include "engine.h"
#include "gab.h"
#include <locale.h>

#define TOSTRING(x) #x
#define STR(x) TOSTRING(x)

#define MAIN_MODULE "gab\\main"

#ifdef GAB_PLATFORM_UNIX
#define GAB_SYMLINK_RECOMMENDATION "ln -sf %s/gab /usr/local/bin"
#else
#define GAB_SYMLINK_RECOMMENDATION                                             \
  "New-Item -ItemType SymbolicLink -Path %s\gab -Target "                      \
  "\"Some\\Directory\\In\\Path\""
#endif

struct gab_triple gab;

/*
 * OS Signal handler for when SIGINT is caught
 */
void propagate_term(int) { gab_sigterm(gab); }

void print_err(struct gab_triple gab, gab_value err) {
  const char *str = gab_errtocs(gab, err);
  fprintf(stderr, "%s\n", str);
}

bool check_and_printerr(union gab_value_pair res) {
  if (res.status != gab_cvalid) {
    const char *errstr = gab_errtocs(gab, res.vresult);
    puts(errstr);
    return false;
  }

  if (res.aresult->data[0] != gab_ok) {
    const char *errstr = gab_errtocs(gab, res.aresult->data[1]);
    puts(errstr);
    a_gab_value_destroy(res.aresult);
    return false;
  }

  return true;
}

static const char *default_modules[] = {
    "Strings", "Binaries", "Messages", "Numbers",  "Blocks",
    "Records", "Shapes",   "Fibers",   "Channels", "__core",
    "Ranges",  "Streams",  "IO",
};
static const size_t ndefault_modules = LEN_CARRAY(default_modules);

int run_repl(int flags, size_t nmodules, const char **modules) {
  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .flags = flags,
          .joberr_handler = print_err,
          .len = nmodules,
          .modules = modules,
      },
      &gab);

  if (!check_and_printerr(res))
    return gab_destroy(gab), 1;

  gab_repl(gab, (struct gab_repl_argt){
                    .name = MAIN_MODULE,
                    .flags = flags,
                    .welcome_message = "Gab version " GAB_VERSION_TAG "",
                    .prompt_prefix = " > ",
                    .len = nmodules,
                    .sargv = modules,
                    .argv = res.aresult->data + 1, // Skip initial ok:
                });

  a_gab_value_destroy(res.aresult);

  return gab_destroy(gab), 0;
}

int run_string(const char *string, int flags, size_t jobs, size_t nmodules,
               const char **modules) {
  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .flags = flags,
          .joberr_handler = print_err,
          .jobs = jobs,
          .len = nmodules,
          .modules = modules,
      },
      &gab);

  if (!check_and_printerr(res))
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

  if (!check_and_printerr(run_res))
    return gab_destroy(gab), 1;

  return a_gab_value_destroy(run_res.aresult), gab_destroy(gab), 0;
}

int run_file(const char *path, int flags, size_t jobs, size_t nmodules,
             const char **modules) {
  union gab_value_pair res = gab_create(
      (struct gab_create_argt){
          .flags = flags,
          .joberr_handler = print_err,
          .jobs = jobs,
          .len = nmodules,
          .modules = modules,
      },
      &gab);

  if (!check_and_printerr(res))
    return gab_destroy(gab), 1;

  union gab_value_pair run_res = gab_use(gab, (struct gab_use_argt){
                                                  .sname = path,
                                                  .len = nmodules,
                                                  .sargv = modules,
                                                  .argv = res.aresult->data + 1,
                                              });

  a_gab_value_destroy(res.aresult);

  if (!check_and_printerr(run_res))
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

#define DEFAULT_COMMAND commands[0]

const struct option dumpast_option = {
    "dast",
    "Dump compiled ast to stdout",
    'a',
    .flag = fGAB_AST_DUMP,
};
const struct option dumpbytecode_option = {
    "dbc",
    "Dump compiled bytecode to stdout",
    'd',
    .flag = fGAB_BUILD_DUMP,
};
const struct option quiet_option = {
    "quiet",
    "Do not print errors to the engine's stderr",
    'q',
    .flag = fGAB_ERR_QUIET,
};
const struct option structured_err_option = {
    "sterr",
    "Instead of pretty-printing errors, use a structured output",
    's',
    .flag = fGAB_ERR_STRUCTURED,
};
const struct option check_option = {
    "check",
    "Compile the file without running it",
    'c',
    .flag = fGAB_BUILD_CHECK,
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
            check_option,
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
        "exec",
        "Compile and run the string <args>",
        "Compile the string <arg> as Gab code and execute it immediately.",
        .handler = exec,
        {
            dumpast_option,
            dumpbytecode_option,
            quiet_option,
            check_option,
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

int get(struct command_arguments args) {
  if (args.argc < 1)
    printf("[gab] No package or tag found. Defaulting to '@'\n");

  const char *pkg = args.argc ? args.argv[0] : "@";

  const size_t pkglen = strlen(pkg);
  char pkgbuf[pkglen + 4];
  strncpy(pkgbuf, pkg, pkglen);
  pkgbuf[pkglen] = '\0';

  const char *tag = split_pkg(pkgbuf);

  if (!tag) {
    printf("[gab] CLI Error: Could not resolve package and tag for '%s'\n",
           pkgbuf);
    return 1;
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
    return 1;
  }

  if (!gab_osmkdirp(gab_prefix)) {
    printf("[gab] CLI Error: Failed to create directory at %s\n", gab_prefix);
    return 1;
  };

  const char *location_prefix = gab_osprefix(tagbuf);

  if (location_prefix == nullptr) {
    printf("[gab] CLI Error: could not determine installation prefix\n");
    return 1;
  }

  printf("[gab] Resolved installation prefix: %s\n", location_prefix);

  if (!gab_osmkdirp(location_prefix)) {
    printf("[gab] CLI Error: Failed to create directory at %s\n",
           location_prefix);
    return 1;
  };

  v_char location = {};
  v_char_spush(&location, s_char_cstr(location_prefix));
  v_char_spush(&location, s_char_cstr("/gab"));
  v_char_push(&location, '\0');

  if (!strcmp(pkgbuf, "Gab")) {
    v_char url = {};

    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tagbuf));
    v_char_spush(&url, s_char_cstr("/gab-release-" GAB_TARGET_TRIPLE));
    v_char_push(&url, '\0');

    // Fetch release binary
    int res = gab_osproc("curl", "-L", "-#", "-o", location.data, url.data);

#ifdef GAB_PLATFORM_UNIX
    res = gab_osproc("chmod", "+x", location.data);

    if (res) {
      printf("[gab] CLI Error: failed to change permissions of binary %s",
             location.data);
      return 1;
    }
#endif

    v_char_destroy(&location);
    v_char_destroy(&url);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s", tagbuf);
      return 1;
    }

    printf("[gab] Downloaded binary for release: %s.\n", tagbuf);

    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tagbuf));
    v_char_spush(&url,
                 s_char_cstr("/gab-release-" GAB_TARGET_TRIPLE "-modules"));
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
      return 1;
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
      return v_char_destroy(&location), v_char_destroy(&url), 1;
    }

    printf("[gab] Extracted modules.\n");
    printf(
        "\nCongratulations! %s@%s successfully installed.\n\n"
        "However, the binary is likely not available in your PATH yet.\n"
        "It is not recommended to add '%s' to PATH directly.\n\nInstead:\n "
        "\tOn systems that support symlinks, link the binary at %s/gab to "
        "some location in PATH already.\n\t\teg: " GAB_SYMLINK_RECOMMENDATION,
        pkgbuf, tagbuf, location_prefix, location_prefix, location_prefix);

    return v_char_destroy(&location), v_char_destroy(&url), 0;
  }

  printf("[gab] CLI Error: Installing packages not yet supported\n");
  return v_char_destroy(&location), 1;
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
  // return run_string(args.argv[0], args.flags, 8, 0, nullptr);
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

int main(int argc, const char **argv) {
  /*register_printf_specifier('V', gab_val_printf_handler,*/
  /*                          gab_val_printf_arginfo);*/

  /**
   * Pull locale from ENV
   */
  setlocale(LC_ALL, "");

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
