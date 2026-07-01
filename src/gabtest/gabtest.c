#include <stdlib.h>

#include "gab.h"
#include "munit/munit.h"

extern const MunitSuite record_suite;

struct gab_triple gab;

int main(int argc, char *argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
  union gab_value_pair result = gab_create(
      (struct gab_create_argt){
          .jobs = 4,
          .wait = cGAB_DEFAULT_WAIT_NS,
      },
      &gab);

  if (result.status != gab_cvalid && result.aresult->data[0] != gab_ok)
    return EXIT_FAILURE;

  MunitSuite suites[] = {
      record_suite,
      {},
  };

  MunitSuite gab_suite = {
      "libcgab", NULL, suites, 1, MUNIT_SUITE_OPTION_NONE,
  };

  /* Finally, we'll actually run our test suite!  That second argument
   * is the user_data parameter which will be passed either to the
   * test or (if provided) the fixture setup function. */
  return munit_suite_main(&gab_suite, (void *)"gab", argc, argv);
}

// Include munit implementation
#include "munit/munit.c"
