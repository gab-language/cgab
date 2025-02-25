#include "core.h"
#include "gab.h"

a_gab_value *gab_gablib_aeval(struct gab_triple gab, uint64_t argc,
                             gab_value argv[static argc]) {
  gab_value source = gab_arg(0);
  gab_value env = gab_arg(1);

  if (env != gab_nil && gab_valkind(env) != kGAB_RECORD)
    return gab_pktypemismatch(gab, env, kGAB_RECORD);

  const char *src = gab_strdata(&source);

  gab_value fib;

  if (env == gab_nil) {
    // These flags aren't passed to the fiber
    // That eventually runs this code.
    // That is the issue we encounter here.
    fib = gab_aexec(gab,
                    (struct gab_exec_argt){
                        .source = src,
                        .name = src,
                        .flags = fGAB_ERR_QUIET | fGAB_RUN_INCLUDEDEFAULTARGS,
                    });
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

    fib = gab_aexec(gab, (struct gab_exec_argt){
                             .source = src,
                             .name = src,
                             .flags = fGAB_ERR_QUIET,
                             .len = len,
                             .sargv = keys,
                             .argv = vals,
                         });
  }

  if (fib == gab_undefined) {
    gab_vmpush(gab_thisvm(gab), gab_err);
    return nullptr;
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, fib);

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "gab");

  gab_def(gab, {
                   gab_message(gab, "gab\\eval"),
                   gab_type(gab, kGAB_STRING),
                   gab_snative(gab, "gab\\eval", gab_gablib_aeval),
               });

  gab_value results[] = {gab_ok, mod};

  return a_gab_value_create(results, sizeof(results) / sizeof(gab_value));
}
