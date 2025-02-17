#include "gab.h"

a_gab_value *gab_chnlib_close(struct gab_triple gab, uint64_t argc,
                              gab_value argv[argc]) {
  gab_chnclose(gab_arg(0));

  gab_vmpush(gab_vm(gab), gab_arg(0));

  return nullptr;
}

a_gab_value *gab_chnlib_is_closed(struct gab_triple gab, uint64_t argc,
                                  gab_value argv[argc]) {
  bool closed = gab_chnisclosed(gab_arg(0));

  gab_vmpush(gab_vm(gab), gab_bool(closed));

  return nullptr;
}

a_gab_value *gab_chnlib_is_full(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
  bool full = gab_chnisfull(gab_arg(0));

  gab_vmpush(gab_vm(gab), gab_bool(full));

  return nullptr;
}

a_gab_value *gab_chnlib_is_empty(struct gab_triple gab, uint64_t argc,
                                 gab_value argv[argc]) {
  bool empty = gab_chnisempty(gab_arg(0));

  gab_vmpush(gab_vm(gab), gab_bool(empty));

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_CHANNEL);

  gab_def(gab,
          {
              gab_message(gab, "close!"),
              t,
              gab_snative(gab, "close!", gab_chnlib_close),
          },
          {
              gab_message(gab, "closed?"),
              t,
              gab_snative(gab, "closed?", gab_chnlib_is_closed),
          },
          {
              gab_message(gab, "full?"),
              t,
              gab_snative(gab, "full?", gab_chnlib_is_full),
          },
          {
              gab_message(gab, "empty?"),
              t,
              gab_snative(gab, "empty?", gab_chnlib_is_empty),
          });

  return a_gab_value_one(gab_ok);
}
