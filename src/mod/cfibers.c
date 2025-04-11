#include "gab.h"

union gab_value_pair gab_fiblib_sleep(struct gab_triple gab, uint64_t argc,
                              gab_value argv[argc]) {

  gab_value n = gab_arg(1);

  if (gab_valkind(n) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, n, kGAB_NUMBER);

  if (thrd_sleep(&(struct timespec){.tv_nsec = gab_valtou(n)}, NULL) < 0)
    return gab_panicf(gab, "Error occurred during sleep");

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_fiblib_await(struct gab_triple gab, uint64_t argc,
                              gab_value argv[argc]) {
  gab_value fib = gab_arg(0);

  union gab_value_pair res = gab_fibawait(gab, fib);
  gab_value env = gab_fibawaite(gab, fib);

  if (res.status == gab_cvalid) {
    gab_nvmpush(gab_thisvm(gab), res.aresult->len, res.aresult->data);
    gab_vmpush(gab_thisvm(gab), env);
  }

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_fiblib_is_done(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
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
              gab_snative(gab, "await", gab_fiblib_await),
          },
          {
              gab_message(gab, "sleep"),
              gab_strtomsg(t),
              gab_snative(gab, "sleep", gab_fiblib_sleep),
          },
          {
              gab_message(gab, "is\\done"),
              t,
              gab_snative(gab, "is\\done", gab_fiblib_is_done),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
