#include "core.h"
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
  "\"Some\Directory\In\Path\""
#endif

struct gab_triple gab;

void propagate_term(int) {
  gab_sigterm(gab);
  gab_destroy(gab);
}

void run_repl(int flags) {
  gab = gab_create((struct gab_create_argt){
      .flags = flags,
  });

  gab_repl(gab, (struct gab_repl_argt){
                    .name = MAIN_MODULE,
                    .flags = flags,
                    .welcome_message = "Gab version " GAB_VERSION_TAG "",
                    .prompt_prefix = " > ",
                });

  gab_destroy(gab);
}

int run_string(const char *string, int flags, size_t jobs) {
  gab = gab_create((struct gab_create_argt){
      .flags = flags,
      .jobs = jobs,
  });

  // This is a weird case where we actually want to include the null terminator
  s_char src = s_char_create(string, strlen(string) + 1);

  a_gab_value *result = gab_exec(gab, (struct gab_exec_argt){
                                          .name = MAIN_MODULE,
                                          .source = (char *)src.data,
                                          .flags = flags,
                                      });

  int exit_code = 1;

  if (result) {

    if (result->len)
      if (result->data[0] == gab_ok)
        exit_code = 0;

    free(result);
  }

  gab_destroy(gab);

  return exit_code;
}

int run_file(const char *path, int flags, size_t jobs) {
  gab = gab_create((struct gab_create_argt){
      .flags = flags,
      .jobs = jobs,
  });

  a_gab_value *result = gab_suse(gab, path);

  int exit_code = 1;

  if (result) {

    if (result->len) {
      if (result->data[0] == gab_ok)
        exit_code = 0;
    }

    free(result);
  } else {
    gab_fpanic(gab, "Module '$' not found.", gab_string(gab, path));
  }

  gab_destroy(gab);

  return exit_code;
}

struct option {
  const char *name;
  const char *desc;
  char shorthand;
  bool takes_argument;
  int flag;
};

#define MAX_OPTIONS 7

struct command {
  const char *name;
  const char *desc;
  const char *long_desc;
  int (*handler)(int, const char **, int);
  struct option options[MAX_OPTIONS];
};

int get(int argc, const char **argv, int flags);
int run(int argc, const char **argv, int flags);
int exec(int argc, const char **argv, int flags);
int repl(int argc, const char **argv, int flags);
int help(int argc, const char **argv, int flags);

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
const struct option empty_env_option = {
    "eenv",
    "Don't use gab's core module - start with a mostly empty "
    "environment",
    'e',
    .flag = fGAB_ENV_EMPTY,
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
            empty_env_option,
            {
                "jobs",
                "Specify the number of os threads which should serve as "
                "workers for running fibers. Default is " STR(
                    cGAB_DEFAULT_NJOBS),
                'j',
                .flag = fGAB_JOB_RUNNERS,
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
            empty_env_option,
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
            empty_env_option,
        },
    },
};

#define N_COMMANDS (LEN_CARRAY(commands))

struct parse_options_result {
  int remaining;
  int flags;
};

struct parse_options_result parse_options(int argc, const char **argv,
                                          struct command command) {
  int flags = 0;

  for (int i = 0; i < argc; i++) {
    if (argv[i][0] != '-')
      return (struct parse_options_result){argc - i, flags};

    if (argv[i][1] == '-') {
      for (int j = 0; j < MAX_OPTIONS; j++) {
        struct option opt = command.options[j];

        if (opt.name && !strcmp(argv[i] + 2, opt.name)) {
          flags |= opt.flag;
          goto next;
        }
      }

      printf("[gab] CLI Error: unrecognized flag '%s'\n", argv[i]);
      exit(1);
    } else {
      for (int j = 0; j < MAX_OPTIONS; j++) {
        struct option opt = command.options[j];

        if (opt.name && argv[i][1] == opt.shorthand) {
          flags |= opt.flag;
          goto next;
        }
      }

      printf("[gab] CLI Error: unrecognized flag '%s'\n", argv[i]);
      exit(1);
    }

  next:
  }

  return (struct parse_options_result){0, flags};
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

int get(int argc, const char **argv, int flags) {
  if (argc < 1)
    printf("[gab] No package or tag found. Defaulting to '@'\n");

  const char *pkg = argc ? argv[0] : "@";

  const size_t len = strlen(pkg);
  char buf[len + 4];
  strncpy(buf, pkg, len);
  buf[len] = '\0';

  const char *tag = split_pkg(buf);

  if (!tag) {
    printf("[gab] CLI Error: Could not resolve package and tag for '%s'\n",
           buf);
    return 1;
  }

  if (!strlen(tag)) {
    printf("[gab] No tag specified. Defaulting to '" GAB_VERSION_TAG "'\n");
    tag = GAB_VERSION_TAG;
  }

  if (!strlen(buf)) {
    printf("[gab] No package specified. Defaulting to 'Gab'\n");
    strncpy(buf, "Gab", 4);
  }

  printf("[gab] Resolved tag '%s'\n", tag);
  printf("[gab] Resolved package '%s'\n", buf);

  const char *gab_prefix = gab_osprefix("");

  if (gab_prefix == nullptr) {
    printf("[gab] CLI Error: could not determine installation prefix\n");
    return 1;
  }

  if (!gab_osmkdirp(gab_prefix)) {
    printf("[gab] CLI Error: Failed to create directory at %s\n", gab_prefix);
    return 1;
  };

  const char *location_prefix = gab_osprefix(tag);

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

  if (!strcmp(buf, "Gab")) {
    v_char url = {};

    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tag));
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
      printf("[gab] CLI Error: failed to download release %s", tag);
      return 1;
    }

    printf("[gab] Downloaded binary for release: %s.\n", tag);

    v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
    v_char_spush(&url, s_char_cstr(tag));
    v_char_spush(&url,
                 s_char_cstr("/gab-release-" GAB_TARGET_TRIPLE "-modules"));
    v_char_push(&url, '\0');

    v_char_spush(&location, s_char_cstr(location_prefix));
    v_char_spush(&location, s_char_cstr("/modules"));
    v_char_push(&location, '\0');

    // Fetch release modules
    res = gab_osproc("curl", "-L", "-#", "-o", location.data, url.data);
    printf("[gab] Downloaded modules for release: %s.\n", tag);

    v_char_destroy(&location);
    v_char_destroy(&url);

    if (res) {
      printf("[gab] CLI Error: failed to download release %s", tag);
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
      printf("[gab] CLI Error: failed to download release %s", tag);
      return 1;
    }

    printf("[gab] Extracted modules.\n");
    printf(
        "\nCongratulations! %s@%s successfully installed.\n\n"
        "However, the binary is likely not available in your PATH yet.\n"
        "It is not recommended to add '%s' to PATH directly.\n\nInstead:\n "
        "\tOn systems that support symlinks, link the binary at %s/gab to "
        "some location in PATH already.\n\t\teg: " GAB_SYMLINK_RECOMMENDATION,
        buf, tag, location_prefix, location_prefix, location_prefix);

    return 0;
  }

  printf("[gab] CLI Error: Installing packages not yet supported\n");
  return 1;
}

int run(int argc, const char **argv, int flags) {
  if (argc < 1) {
    printf("[gab] CLI Error: not enough arguments\n");
    return 1;
  }

  const char *path = argv[0];
  size_t jobs = 8;

  if (flags & fGAB_JOB_RUNNERS) {
    const char *njobs = argv[0];

    if (argc < 2) {
      printf("[gab] CLI Error: not enough arguments\n");
      return 1;
    }

    jobs = atoi(njobs);
    path = argv[1];
  }

  return run_file(path, flags, jobs);
}

int exec(int argc, const char **argv, int flags) {
  if (argc < 1) {
    printf("[gab] CLI Error: not enough arguments\n");
    return 1;
  }

  return run_string(argv[0], flags, 8);
}

int repl(int argc, const char **argv, int flags) {
  run_repl(flags);
  return 0;
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

int help(int argc, const char **argv, int flags) {
  if (argc < 1) {
    // Print command summaries
    for (int i = 0; i < N_COMMANDS; i++)
      cmd_summary(i);

    return 0;
  }

  const char *subcommand = argv[0];

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

  signal(SIGINT, propagate_term);

  for (int i = 0; i < N_COMMANDS; i++) {
    struct command cmd = commands[i];
    assert(cmd.handler);

    if (!strcmp(argv[1], cmd.name)) {
      struct parse_options_result o = parse_options(argc - 2, argv + 2, cmd);

      return cmd.handler(o.remaining, argv + (argc - o.remaining), o.flags);
    }
  }

  printf("[gab] CLI Error: unrecognized subcommand '%s'\n", argv[1]);

fin:
  struct command cmd = DEFAULT_COMMAND;
  return cmd.handler(0, argv, 0);
}
