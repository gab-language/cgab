#include "core.h"
#include "gab.h"
#include <locale.h>

#define TOSTRING(x) #x
#define STR(x) TOSTRING(x)

#define MAIN_MODULE "gab\\main"

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
        exit_code = 1;

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

    if (result->len)
      if (result->data[0] == gab_ok)
        exit_code = 1;

    free(result);
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

int install(int argc, const char **argv, int flags);
int run(int argc, const char **argv, int flags);
int exec(int argc, const char **argv, int flags);
int repl(int argc, const char **argv, int flags);
int help(int argc, const char **argv, int flags);

#define DEFAULT_COMMAND commands[0]

static struct command commands[] = {
    {
        "help",
        "Print this help message",
        .handler = help,
    },
    {
        "install",
        "Install different versions of gab",
        .handler = install,
    },
    {
        "run",
        "Compile and run the module at path <args>",
        .handler = run,
        {
            {
                "dump",
                "Dump compiled ast to stdout",
                'a',
                .flag = fGAB_AST_DUMP,
            },
            {
                "dump",
                "Dump compiled bytecode to stdout",
                'd',
                .flag = fGAB_BUILD_DUMP,
            },
            {
                "quiet",
                "Do not print errors to the engine's stderr",
                'q',
                .flag = fGAB_ERR_QUIET,
            },
            {
                "sterr",
                "Instead of pretty-printing errors, use a structured output.",
                's',
                .flag = fGAB_ERR_STRUCTURED,
            },
            {
                "check",
                "Compile the file without running it.",
                'c',
                .flag = fGAB_BUILD_CHECK,
            },
            {
                "eenv",
                "Don't use gab's core module - start with a mostly-empty "
                "environment.",
                'e',
                .flag = fGAB_ENV_EMPTY,
            },
            {
                "jobs",
                "Specify the number of os threads which should serve as "
                "workers for running gab fibers. Default is " STR(
                    cGAB_DEFAULT_NJOBS) ".",
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
            {
                "dump",
                "Dump compiled ast to stdout",
                'a',
                .flag = fGAB_AST_DUMP,
            },
            {
                "dump",
                "Dump compiled bytecode to stdout",
                'd',
                .flag = fGAB_BUILD_DUMP,
            },
            {
                "quiet",
                "Do not print errors to the engine's stderr",
                'q',
                .flag = fGAB_ERR_QUIET,
            },
            {
                "sterr",
                "Instead of pretty-printing errors, use a structured output.",
                's',
                .flag = fGAB_ERR_STRUCTURED,
            },
            {
                "check",
                "Compile the file without running it.",
                'c',
                .flag = fGAB_BUILD_CHECK,
            },
            {
                "eenv",
                "Don't use gab's core module - start with a mostly-empty "
                "environment.",
                'e',
                .flag = fGAB_ENV_EMPTY,
            },
        },
    },
    {
        "repl",
        "Enter the read-eval-print loop.",
        .handler = repl,
        {
            {
                "dump",
                "Dump compiled bytecode to stdout",
                'd',
                .flag = fGAB_BUILD_DUMP,
            },
            {
                "eenv",
                "Don't use gab's core module - start with a mostly-empty "
                "environment.",
                'e',
                .flag = fGAB_ENV_EMPTY,
            },
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
          break;
        }
      }

      printf("UNRECOGNIZED FLAG: %s\n", argv[i]);
      exit(1);
      continue;
    } else {
      for (int j = 0; j < MAX_OPTIONS; j++) {
        struct option opt = command.options[j];

        if (opt.name && argv[i][1] == opt.shorthand) {
          flags |= opt.flag;
          break;
        }
      }

      continue;

      printf("UNRECOGNIZED FLAG: %s\n", argv[i]);
      exit(1);
    }
  }

  return (struct parse_options_result){0, flags};
}

#define GAB_RELEASE_DOWNLOAD_URL                                               \
  "https://github.com/gab-language/cgab/releases/download/"

int install(int argc, const char **argv, int flags) {
  const char *tag = argc ? argv[0] : GAB_VERSION_TAG;

  v_char url = {};

  v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&url, s_char_cstr(tag));
  v_char_spush(&url, s_char_cstr("/gab-release-" GAB_TARGET_TRIPLE));
  v_char_push(&url, '\0');

  const char *location_prefix = gab_osprefix();

  if (location_prefix == nullptr) {
    printf("[ERROR]: Could not determine install prefix.\n");
    return 1;
  }

  printf("[gab]: resolved installation prefix: %s.\n", location_prefix);

  v_char location = {};
  v_char_spush(&location, s_char_cstr(location_prefix));
  v_char_spush(&location, s_char_cstr("/gab"));
  v_char_push(&location, '\0');

  // Fetch release binary
  int res = gab_osproc("curl", "-L", "-#", "-o", location.data, url.data);

  v_char_destroy(&location);
  v_char_destroy(&url);

  if (res) {
    printf("ERROR: Failed to download release %s", tag);
    return 1;
  }

  v_char_spush(&url, s_char_cstr(GAB_RELEASE_DOWNLOAD_URL));
  v_char_spush(&url, s_char_cstr(tag));
  v_char_spush(&url, s_char_cstr("/gab-release-" GAB_TARGET_TRIPLE "-modules"));
  v_char_push(&url, '\0');

  v_char_spush(&location, s_char_cstr(location_prefix));
  v_char_spush(&location, s_char_cstr("/modules"));
  v_char_push(&location, '\0');

  // Fetch release modules
  res = gab_osproc("curl", "-L", "-#", "-o", location.data, url.data);

  v_char_destroy(&location);
  v_char_destroy(&url);

  if (res) {
    printf("ERROR: Failed to download release %s", tag);
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
    printf("ERROR: Failed to download release %s", tag);
    return 1;
  }

  return 0;
}

int run(int argc, const char **argv, int flags) {
  if (argc < 1) {
    printf("ERROR: Not enough arguments\n");
    return 1;
  }

  const char *path = argv[0];
  size_t jobs = 8;

  if (flags & fGAB_JOB_RUNNERS) {
    const char *njobs = argv[0];

    if (argc < 2) {
      printf("ERROR: Not enough arguments\n");
      return 1;
    }

    jobs = atoi(njobs);
    path = argv[1];
  }

  return run_file(path, flags, jobs);
}

int exec(int argc, const char **argv, int flags) {
  assert(argc > 0);

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

fin: {
  struct command cmd = DEFAULT_COMMAND;
  return cmd.handler(0, argv, 0);
}
}
