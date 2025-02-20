#include "gab.h"

a_gab_value *gab_fmtlib_printf(struct gab_triple *gab, uint64_t argc,
                               gab_value argv[argc]) {
  gab_value fmtstr = gab_arg(0);

  if (gab_valkind(fmtstr) != kGAB_STRING)
    return gab_pktypemismatch(*gab, fmtstr, kGAB_STRING);

  const char *fmt = gab_strdata(&fmtstr);

  if (gab_nfprintf(stdout, fmt, argc - 1, argv + 1) < 0)
    return gab_fpanic(*gab,
                      "Wrong number of format arguments to printf (expected $)",
                      gab_number(argc - 1));

  return nullptr;
}

a_gab_value *gab_fmtlib_println(struct gab_triple *gab, uint64_t argc,
                                gab_value argv[argc]) {

  for (uint64_t i = 0; i < argc; i++) {
    gab_value v = gab_arg(i);
    gab_fvalinspect(stdout, v, -1);
    if (i + 1 < argc)
      fputc(' ', stdout);
  }
  fputc('\n', stdout);

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab,
          {
              gab_message(gab, "printf"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "printf", gab_fmtlib_printf),
          },
          {
              gab_message(gab, "println"),
              gab_undefined,
              gab_snative(gab, "println", gab_fmtlib_println),
          });

  return a_gab_value_one(gab_ok);
}
