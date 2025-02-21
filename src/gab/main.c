#include "core.h"
#include "gab.h"
#include <locale.h>

#define TOSTRING(x) #x
#define STR(x) TOSTRING(x)

#define MAIN_MODULE "gab\\main"

#ifdef GAB_PLATFORM_UNIX
#define GAB_SYMLINK_RECOMMENDATION "ln -s %s/gab /usr/local/bin"
#else
#define GAB_SYMLINK_RECOMMENDATION                                             \
  "New-Item -ItemType SymbolicLink -Path %s\gab -Target "                      \
  "\"Some\Directory\In\Path\""
#endif

void run_repl(int flags) {
  struct gab_triple gab = gab_create((struct gab_create_argt){
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
  struct gab_triple gab = gab_create((struct gab_create_argt){
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
  struct gab_triple gab = gab_create((struct gab_create_argt){
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
        "Print this help message",
        .handler = help,
    },
    {
        "get",
        "Install Gab and/or packages given by <args>",
        .handler = get,
    },
    {
        "run",
        "Compile and run the module at path <args>",
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

int help(int argc, const char **argv, int flags) {
  for (int i = 0; i < N_COMMANDS; i++) {
    struct command cmd = commands[i];
    printf("\ngab %4s [opts] <args>\t%s\n", cmd.name, cmd.desc);

    for (int j = 0; j < MAX_OPTIONS; j++) {
      struct option opt = cmd.options[j];

      if (!opt.name)
        break;

      printf("\t--%-5s\t-%c\t%s\n", opt.name, opt.shorthand, opt.desc);
    }
  };
  return 0;
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
