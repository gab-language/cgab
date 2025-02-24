#include "core.h"
#include "gab.h"

a_gab_value *gab_gablib_eval(struct gab_triple gab, uint64_t argc,
                             gab_value argv[static argc]) {
  gab_value source = gab_arg(0);

  const char *src = gab_strdata(&source);

  // These flags aren't passed to the fiber
  // That eventually runs this code.
  // That is the issue we encounter here.
  a_gab_value *res =
      gab_exec(gab, (struct gab_exec_argt){
                        .source = src,
                        .name = src,
                        .flags = fGAB_ERR_QUIET | fGAB_RUN_INCLUDEDEFAULTARGS,
                    });

  if (res == nullptr)
    return a_gab_value_one(gab_err);

  gab_nvmpush(gab_thisvm(gab), res->len, res->data);

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "gab");

  gab_def(gab, {
                   gab_message(gab, "gab\\eval"),
                   gab_type(gab, kGAB_STRING),
                   gab_snative(gab, "gab\\eval", gab_gablib_eval),
               });

  gab_value results[] = {gab_ok, mod};

  return a_gab_value_create(results, sizeof(results) / sizeof(gab_value));
}
