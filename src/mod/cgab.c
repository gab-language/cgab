#include "gab.h"

const char *modules[] = {
    "Strings", "Binaries", "Messages", "Numbers", "Blocks", "Records", "Shapes",
    "Fibers",  "Channels", "__core",   "Ranges",  "IO",     "Streams",
};
const size_t nmodules = sizeof(modules) / sizeof(modules[0]);

GAB_DYNLIB_NATIVE_FN(gab, build) {
  gab_value source = gab_arg(0);
  const char *src = gab_strdata(&source);

  union gab_value_pair mod = gab_build(gab, (struct gab_parse_argt){
                                                .source = src,
                                                .name = src,
                                                .len = nmodules,
                                                .argv = modules,
                                            });

  if (mod.status != gab_cinvalid) {
    gab_vmpush(gab_thisvm(gab), gab_ok, mod.vresult);
    return gab_union_cvalid(gab_nil);
  }

  gab_value rec = mod.vresult;

  gab_vmpush(gab_thisvm(gab), gab_err, rec);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(gab, parse) {
  gab_value source = gab_arg(0);
  const char *src = gab_strdata(&source);

  union gab_value_pair mod = gab_parse(gab, (struct gab_parse_argt){
                                                .source = src,
                                                .name = src,
                                            });

  if (mod.status != gab_cinvalid) {
    gab_vmpush(gab_thisvm(gab), gab_ok, mod.vresult);
    return gab_union_cvalid(gab_nil);
  }

  gab_value rec = gab_recordof(gab);
  // gab, gab_message(gab, "status"), gab_string(gab, err.status_name),
  // gab_message(gab, "row"), gab_number(err.row),
  // gab_message(gab, "col\\begin"), gab_number(err.col_begin),
  // gab_message(gab, "col\\end"), gab_number(err.col_end),
  // gab_message(gab, "byte\\begin"), gab_number(err.byte_begin),
  // gab_message(gab, "byte\\end"), gab_number(err.byte_end),
  // );

  gab_vmpush(gab_thisvm(gab), gab_err, rec);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(gab, aeval) {
  gab_value source = gab_arg(0);
  gab_value env = gab_arg(1);

  if (env != gab_nil && gab_valkind(env) != kGAB_RECORD)
    return gab_pktypemismatch(gab, env, kGAB_RECORD);

  const char *src = gab_strdata(&source);

  gab_value fib;

  if (env == gab_nil) {
    union gab_value_pair res = gab_aexec(gab, (struct gab_exec_argt){
                                                  .source = src,
                                                  .name = src,
                                              });
    if (res.status == gab_cinvalid)
      return gab_vmpush(gab_thisvm(gab), gab_err, res.vresult),
             gab_union_cvalid(gab_nil);

    assert(res.status == gab_cvalid);
    fib = res.vresult;

  } else {
    size_t len = gab_reclen(env) - 1;
    const char *keys[len];
    gab_value keyvals[len];
    gab_value vals[len];

    for (size_t i = 0; i < len; i++) {
      size_t index = i + 1;
      keyvals[i] = gab_valintos(gab, gab_ukrecat(env, index));
      vals[i] = gab_uvrecat(env, index);
      keys[i] = gab_strdata(keyvals + i);
    }

    union gab_value_pair res = gab_aexec(gab, (struct gab_exec_argt){
                                                  .source = src,
                                                  .name = src,
                                                  .len = len,
                                                  .sargv = keys,
                                                  .argv = vals,
                                              });
    if (res.status == gab_cinvalid)
      return gab_vmpush(gab_thisvm(gab), gab_err, res.vresult),
             gab_union_cvalid(gab_nil);

    assert(res.status == gab_cvalid);
    fib = res.vresult;
  }

  if (fib == gab_cinvalid) {
    gab_vmpush(gab_thisvm(gab), gab_err);
    return gab_union_cvalid(gab_nil);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, fib);

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab,
          {
              gab_message(gab, "as\\gab"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\gab", gab_mod_gab_aeval),
          },
          {
              gab_message(gab, "as\\gab\\block"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\gab\\block", gab_mod_gab_build),
          },
          {
              gab_message(gab, "as\\gab\\ast"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\gab\\ast", gab_mod_gab_parse),
          });

  gab_value res[] = {gab_ok};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
