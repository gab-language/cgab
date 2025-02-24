#include "gab.h"

a_gab_value *gab_fiblib_await(struct gab_triple gab, uint64_t argc,
                              gab_value argv[argc]) {
  gab_value fib = gab_arg(0);

  a_gab_value *res = gab_fibawait(gab, fib);

  gab_nvmpush(gab_thisvm(gab), res->len, res->data);

  return nullptr;
}

a_gab_value *gab_fiblib_is_done(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
  gab_value fib = gab_arg(0);

  bool is_done = gab_fibisdone(fib);

  gab_vmpush(gab_thisvm(gab), gab_bool(is_done));

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_FIBER);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "await!"),
              t,
              gab_snative(gab, "await!", gab_fiblib_await),
          },
          {
              gab_message(gab, "done?"),
              t,
              gab_snative(gab, "done?", gab_fiblib_is_done),
          });

  return a_gab_value_one(gab_ok);
}
