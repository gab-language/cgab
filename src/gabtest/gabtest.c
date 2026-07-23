#include <stdlib.h>

#include "cgab.h"

// Disable timing so that we can cross-compile this binary
#define MUNIT_DISABLE_TIMING
#include "munit/munit.h"

extern const MunitSuite record_suite;
extern const MunitSuite shape_suite;
extern const MunitSuite string_suite;
extern const MunitSuite binary_suite;
extern const MunitSuite channel_suite;
extern const MunitSuite exec_suite;

struct gab_triple gab;

int main(int argc, char *argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
  union gab_value_pair result = gab_create(
      (struct gab_create_argt){
          .jobs = 4,
          .wait = 0,
      },
      &gab);

  if (result.status != gab_cvalid && result.aresult->data[0] != gab_ok)
    return EXIT_FAILURE;

  MunitSuite suites[] = {
      record_suite,
      shape_suite,
      string_suite,
      binary_suite,
      channel_suite,
      exec_suite,
      {},
  };

  MunitSuite gab_suite = {
      "libcgab", NULL, suites, 1, MUNIT_SUITE_OPTION_NONE,
  };

  return munit_suite_main(&gab_suite, nullptr, argc, argv);
}

// Include munit implementation
#include "munit/munit.c"
