#include "gab.h"
#include "platform.h"

GAB_DYNLIB_NATIVE_FN(channel, close) {
  gab_chnclose(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_arg(0));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(channel, is_closed) {
  bool closed = gab_chnisclosed(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(closed));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(channel, is_full) {
  bool full = gab_chnisfull(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(full));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(channel, is_empty) {
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
              gab_snative(gab, "close", gab_mod_channel_close),
          },
          {
              gab_message(gab, "is\\closed"),
              t,
              gab_snative(gab, "is\\closed", gab_mod_channel_is_closed),
          },
          {
              gab_message(gab, "is\\full"),
              t,
              gab_snative(gab, "is\\full", gab_mod_channel_is_full),
          },
          {
              gab_message(gab, "is\\empty"),
              t,
              gab_snative(gab, "is\\empty", gab_mod_channel_is_empty),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
