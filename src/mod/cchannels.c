#include "gab.h"

union gab_value_pair gab_chnlib_close(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_chnclose(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_arg(0));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_chnlib_is_closed(struct gab_triple gab, uint64_t argc,
                                          gab_value argv[argc]) {
  bool closed = gab_chnisclosed(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(closed));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_chnlib_is_full(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[argc]) {
  bool full = gab_chnisfull(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(full));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_chnlib_is_empty(struct gab_triple gab, uint64_t argc,
                                         gab_value argv[argc]) {
  bool empty = gab_chnisempty(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(empty));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_CHANNEL);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "close"),
              t,
              gab_snative(gab, "close", gab_chnlib_close),
          },
          {
              gab_message(gab, "is\\closed"),
              t,
              gab_snative(gab, "is\\closed", gab_chnlib_is_closed),
          },
          {
              gab_message(gab, "is\\full"),
              t,
              gab_snative(gab, "is\\full", gab_chnlib_is_full),
          },
          {
              gab_message(gab, "is\\empty"),
              t,
              gab_snative(gab, "is\\empty", gab_chnlib_is_empty),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
