#include "gab.h"

GAB_DYNLIB_NATIVE_FN(fib, await) {
  gab_value fib = gab_arg(0);

  union gab_value_pair res = gab_tfibawait(gab, fib, 0);

  if (res.status == gab_cvalid) {
    gab_value env = gab_fibawaite(gab, fib);
    gab_nvmpush(gab_thisvm(gab), res.aresult->len, res.aresult->data);
    gab_vmpush(gab_thisvm(gab), env);
    return gab_union_cvalid(gab_nil);
  }

  return res;
}

GAB_DYNLIB_NATIVE_FN(fib, is_done) {
  gab_value fib = gab_arg(0);

  bool is_done = gab_fibisdone(fib);

  gab_vmpush(gab_thisvm(gab), gab_bool(is_done));

  return gab_union_cvalid(gab_nil);
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
              gab_message(gab, "await"),
              t,
              gab_snative(gab, "await", gab_mod_fib_await),
          },
          {
              gab_message(gab, "is\\done"),
              t,
              gab_snative(gab, "is\\done", gab_mod_fib_is_done),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
