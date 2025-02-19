#include "gab.h"

a_gab_value *gab_timelib_now(struct gab_triple gab, uint64_t argc,
                             gab_value argv[argc]) {
  if (argc != 1) {
    return gab_fpanic(gab, "Invalid call to gab_lib_clock");
  }

  clock_t t = clock();

  gab_value res = gab_number((double)t / CLOCKS_PER_SEC);

  gab_vmpush(gab_thisvm(gab), res);
  return nullptr;
};

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "time");

  gab_def(gab, {
                   gab_message(gab, "now"),
                   mod,
                   gab_snative(gab, "now", gab_timelib_now),
               });

  return a_gab_value_one(gab_ok);
}
