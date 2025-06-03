#include "gab.h"

union gab_value_pair gab_msglib_message(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[static argc]) {
  gab_value name = gab_arg(1);

  if (gab_valkind(name) != kGAB_STRING)
    return gab_pktypemismatch(gab, name, kGAB_STRING);

  gab_vmpush(gab_thisvm(gab), gab_strtomsg(name));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_to_string(struct gab_triple gab, uint64_t argc,
                                          gab_value argv[static argc]) {
  gab_value msg = gab_arg(0);

  gab_vmpush(gab_thisvm(gab), gab_msgtostr(msg));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_gen(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[static argc]) {
  static _Atomic int64_t n;
  int64_t this_n = atomic_fetch_add(&n, 1);

  gab_value prefix = gab_arg(1);
  if (prefix == gab_nil)
    prefix = gab_string(gab, "G__");

  char buf[4096];
  int len = gab_sprintf(buf, sizeof buf, "$$", prefix, gab_number(this_n));

  if (len < 0)
    return gab_panicf(gab, "sprintf buffer too small", gab_number(argc - 1));

  gab_value msg = gab_nmessage(gab, len, buf);
  gab_vmpush(gab_thisvm(gab), msg);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_specs(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[static argc]) {
  if (argc == 1) {
    gab_value rec = gab_thisfibmsg(gab);
    gab_vmpush(gab_thisvm(gab), rec);
    return gab_union_cvalid(gab_nil);
  }

  gab_value msg = gab_arg(1);

  gab_value rec = gab_thisfibmsg(gab);
  rec = rec == gab_cundefined ? rec : gab_recat(rec, msg);

  if (rec == gab_cundefined)
    gab_vmpush(gab_thisvm(gab), gab_nil);
  else
    gab_vmpush(gab_thisvm(gab), rec);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_has(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[static argc]) {
  switch (argc) {
  case 2: {

    struct gab_impl_rest res = gab_impl(gab, argv[0], argv[1]);

    gab_vmpush(gab_thisvm(gab), gab_bool(res.status));
    return gab_union_cvalid(gab_nil);
  }
  default:
    return gab_union_cvalid(gab_nil);
  }
}

union gab_value_pair gab_msglib_at(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[static argc]) {
  gab_value m = gab_arg(0);
  gab_value k = gab_arg(1);

  struct gab_impl_rest res = gab_impl(gab, m, k);

  gab_value values[] = {gab_none, gab_nil};

  if (res.status) {
    values[0] = gab_ok;
    values[1] = res.as.spec;
  }

  gab_nvmpush(gab_thisvm(gab), 2, values);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_string_into(struct gab_triple gab,
                                            uint64_t argc,
                                            gab_value argv[static argc]) {
  gab_value m = gab_arg(0);

  gab_vmpush(gab_thisvm(gab), gab_msgtostr(m));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_put(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[static argc]) {
  gab_value msg = gab_arg(0);
  gab_value rec = gab_arg(1);
  gab_value spec = gab_arg(2);

  if (gab_valkind(spec) != kGAB_PRIMITIVE && gab_valkind(spec) != kGAB_BLOCK &&
      gab_valkind(spec) != kGAB_NATIVE)
    return gab_pktypemismatch(gab, spec, kGAB_BLOCK);

  if (!gab_def(gab, {msg, rec, spec}))
    return gab_panicf(gab, "$ already specializes for type $", msg, rec);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_def(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[static argc]) {
  gab_value msg = gab_arg(0);
  gab_value spec = gab_arg(argc - 1);

  if (gab_valkind(msg) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, msg, kGAB_MESSAGE);

  uint64_t len = argc - 2;

  if (argc < 2)
    len = 0;

  if (len == 0) {
    gab_value t = gab_cundefined;

    if (!gab_def(gab, {msg, t, spec}))
      return gab_panicf(gab, "$ already specializes for type $", msg, t);

    return gab_union_cvalid(gab_nil);
  }

  for (uint64_t i = 0; i < len; i++) {
    gab_value t = gab_arg(i + 1);

    if (!gab_def(gab, {msg, t, spec}))
      return gab_panicf(gab, "$ already specializes for type $", msg, t);
  }

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_case(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[static argc]) {
  gab_value msg = gab_arg(0);
  gab_value cases = gab_arg(1);

  if (gab_valkind(msg) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, msg, kGAB_MESSAGE);

  if (gab_valkind(cases) != kGAB_RECORD)
    return gab_pktypemismatch(gab, cases, kGAB_RECORD);

  for (int i = 0; i < gab_reclen(cases); i++) {
    gab_value type = gab_ukrecat(cases, i);
    gab_value spec = gab_uvrecat(cases, i);

    if (!gab_def(gab, {msg, type, spec}))
      return gab_panicf(gab, "$ already specializes for type $", msg, type);
  }

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_module(struct gab_triple gab, uint64_t argc,
                                       gab_value argv[static argc]) {
  gab_value cases = gab_arg(0);
  gab_value messages = gab_arg(1);

  if (gab_valkind(cases) != kGAB_RECORD)
    return gab_pktypemismatch(gab, cases, kGAB_RECORD);

  if (gab_valkind(messages) != kGAB_RECORD)
    return gab_pktypemismatch(gab, messages, kGAB_RECORD);

  if (gab_reclen(cases) == 0) {
    gab_value type = gab_cundefined;

    for (uint64_t i = 0; i < gab_reclen(messages); i++) {
      gab_value spec = gab_uvrecat(messages, i);
      gab_value msg = gab_ukrecat(messages, i);

      if (gab_valkind(msg) != kGAB_MESSAGE)
        return gab_pktypemismatch(gab, msg, kGAB_MESSAGE);

      if (!gab_def(gab, {msg, type, spec}))
        return gab_panicf(gab, "$ already specializes for type $", msg, type);
    }

    return gab_union_cvalid(gab_nil);
  }

  for (int j = 0; j < gab_reclen(cases); j++) {
    gab_value type = gab_uvrecat(cases, j);

    for (int i = 0; i < gab_reclen(messages); i++) {
      gab_value spec = gab_uvrecat(messages, i);
      gab_value msg = gab_ukrecat(messages, i);

      if (gab_valkind(msg) != kGAB_MESSAGE)
        return gab_pktypemismatch(gab, msg, kGAB_MESSAGE);

      if (!gab_def(gab, {msg, type, spec}))
        return gab_panicf(gab, "$ already specializes for type $", msg, type);
    }
  }

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_MESSAGE);
  gab_value mod = gab_strtomsg(t);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(gab_type(gab, kGAB_BLOCK)),
              gab_type(gab, kGAB_BLOCK),
          },
          {
              gab_message(gab, "t"),
              mod,
              t,
          },
          {
              gab_message(gab, "gen"),
              mod,
              gab_snative(gab, "gen", gab_msglib_gen),
          },
          {
              gab_message(gab, "specializations"),
              mod,
              gab_snative(gab, "specializations", gab_msglib_specs),
          },
          {
              gab_message(gab, "def"),
              t,
              gab_snative(gab, "def", gab_msglib_def),
          },
          {
              gab_message(gab, "defcase"),
              t,
              gab_snative(gab, "defcase", gab_msglib_case),
          },
          {
              gab_message(gab, "defmodule"),
              gab_type(gab, kGAB_RECORD),
              gab_snative(gab, "defmodule", gab_msglib_module),
          },
          {
              gab_message(gab, "to\\s"),
              t,
              gab_snative(gab, "to\\s", gab_msglib_to_string),
          },
          {
              gab_message(gab, "has?"),
              t,
              gab_snative(gab, "has?", gab_msglib_has),
          },
          {
              gab_message(gab, "at"),
              t,
              gab_snative(gab, "at", gab_msglib_at),
          });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
