#include "gab.h"

a_gab_value *gab_fmtlib_printf(struct gab_triple gab, uint64_t argc,
                               gab_value argv[argc]) {
  gab_value fmtstr = gab_arg(0);

  if (gab_valkind(fmtstr) != kGAB_STRING)
    return gab_pktypemismatch(gab, fmtstr, kGAB_STRING);

  const char *fmt = gab_strdata(&fmtstr);

  if (gab_nfprintf(gab.eg->sout, fmt, argc - 1, argv + 1) < 0)
    return gab_fpanic(gab,
                      "Wrong number of format arguments to printf (expected $)",
                      gab_number(argc - 1));

  return nullptr;
}

a_gab_value *gab_fmtlib_sprintf(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
  gab_value fmtstr = gab_arg(0);

  const char *fmt = gab_strdata(&fmtstr);

  char buf[1000];
  int len = gab_nsprintf(buf, sizeof(buf), fmt, argc - 1, argv + 1);

  if (len < 0)
    return gab_fpanic(
        gab, "Wrong number of format arguments to sprintf (expected $)",
        gab_number(argc - 1));

  gab_vmpush(gab_thisvm(gab), gab_string(gab, buf));

  return nullptr;
}

a_gab_value *gab_fmtlib_println(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {

  for (uint64_t i = 0; i < argc; i++) {
    gab_value v = gab_arg(i);
    gab_fvalinspect(gab.eg->sout, v, -1);
    if (i + 1 < argc)
      fputc(' ', gab.eg->sout);
  }
  fputc('\n', gab.eg->sout);

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
          },
          {
              gab_message(gab, "sprintf"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "sprintf", gab_fmtlib_sprintf),
          });

  return a_gab_value_one(gab_ok);
}
