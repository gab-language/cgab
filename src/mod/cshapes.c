#include "gab.h"

GAB_DYNLIB_NATIVE_FN(shp, at) {
  gab_value shp = gab_arg(0);
  gab_value key = gab_arg(1);

  if (!gab_valisshp(shp))
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  if (!gab_valisnum(key))
    return gab_pktypemismatch(gab, shp, kGAB_NUMBER);

  gab_uint idx = gab_valtou(key);

  gab_value val = gab_shpat(shp, idx);

  if (val == gab_cundefined)
    gab_vmpush(gab_thisvm(gab), gab_none);
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, val);

  return gab_union_cvalid(gab_nil);
}

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)
#define CLAMP(a, b) (MAX(0, MIN(a, b)))

GAB_DYNLIB_NATIVE_FN(shp, slice) {
  gab_value shp = gab_arg(0);
  uint64_t len = gab_shplen(argv[0]);
  uint64_t start = 0, end = len;

  switch (argc) {
  case 2: {
    if (gab_valkind(argv[1]) != kGAB_NUMBER) {
      return gab_panicf(gab, "&:slice expects a number as the second argument");
    }

    gab_uint a = gab_valtou(argv[1]);
    end = MIN(a, len);
    break;
  }

  case 3:
    if (gab_valkind(argv[1]) == kGAB_NUMBER) {
      start = MIN(gab_valtou(argv[1]), len);
    } else if (argv[1] == gab_nil) {
      return gab_panicf(gab, "&:slice expects a number as the second argument");
    }

    if (gab_valkind(argv[2]) == kGAB_NUMBER) {
      end = MIN(gab_valtou(argv[2]), len);
    } else if (argv[2] == gab_nil) {
      return gab_panicf(gab, "&:slice expects a number as the third argument");
    }
    break;

  default:
    return gab_panicf(gab, "&:slice expects 2 or 3 arguments");
  }

  if (start > end) {
    return gab_panicf(gab, "&:slice expects the start to be before the end");
  }

  uint64_t size = end - start;

  if (size == 0) {
    gab_vmpush(gab_thisvm(gab), gab_listof(gab));
    return gab_union_cvalid(gab_nil);
  }

  gab_value vs[size];
  for (uint64_t i = 0; i < size; i++) {
    vs[i] = gab_ushpat(shp, start + i);
  }

  gab_vmpush(gab_thisvm(gab), gab_list(gab, size, vs));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, cat) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  gab_value oth = gab_arg(1);

  if (gab_valkind(oth) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, oth, kGAB_SHAPE);

  gab_value res = gab_lstcat(gab, shp, oth);

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, push) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  shp = gab_nlstpush(gab, shp, argc - 1, argv + 1);

  gab_vmpush(gab_thisvm(gab), shp);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, pop) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  gab_value res;

  gab_vmpush(gab_thisvm(gab), gab_lstpop(gab, shp, &res));
  gab_vmpush(gab_thisvm(gab), res);

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, is_empty) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  uint64_t len = gab_shplen(shp);

  gab_vmpush(gab_thisvm(gab), gab_bool(len == 0));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, is_list) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  gab_vmpush(gab_thisvm(gab), gab_bool(gab_shpisl(shp)));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, len) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  gab_vmpush(gab_thisvm(gab), gab_number(gab_shplen(shp)));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, seq_init) {
  gab_value shp = gab_arg(0);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  uint64_t len = gab_shplen(shp);

  if (len == 0) {
    gab_vmpush(gab_thisvm(gab), gab_none);
    return gab_union_cvalid(gab_nil);
  }

  gab_value key = gab_number(0);
  gab_value val = gab_ushpat(shp, 0);

  gab_vmpush(gab_thisvm(gab), gab_ok, key, val, key);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(shp, seq_next) {
  gab_value shp = gab_arg(0);
  gab_value old_key = gab_arg(1);

  if (gab_valkind(shp) != kGAB_SHAPE)
    return gab_pktypemismatch(gab, shp, kGAB_SHAPE);

  if (gab_valisnum(old_key))
    return gab_pktypemismatch(gab, old_key, kGAB_NUMBER);

  uint64_t len = gab_shplen(shp);

  if (len == 0)
    goto fin;

  gab_uint i = gab_valtou(old_key);

  if (i == -1 || i + 1 == len)
    goto fin;

  gab_value key = i + 1;
  gab_value val = gab_ushpat(shp, i + 1);

  gab_vmpush(gab_thisvm(gab), gab_ok, key, val, key);
  return gab_union_cvalid(gab_nil);

fin:
  gab_vmpush(gab_thisvm(gab), gab_none);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_SHAPE);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "slice"),
              t,
              gab_snative(gab, "slice", gab_mod_shp_slice),
          },
          {
              gab_message(gab, "push"),
              t,
              gab_snative(gab, "push", gab_mod_shp_push),
          },
          {
              gab_message(gab, "+"),
              t,
              gab_snative(gab, "+", gab_mod_shp_cat),
          },
          {
              gab_message(gab, "is\\empty"),
              t,
              gab_snative(gab, "is\\empty", gab_mod_shp_is_empty),
          },
          {
              gab_message(gab, "is\\list"),
              t,
              gab_snative(gab, "is\\list", gab_mod_shp_is_list),
          },
          {
              gab_message(gab, "pop"),
              t,
              gab_snative(gab, "pop", gab_mod_shp_pop),
          },
          {
              gab_message(gab, "at"),
              t,
              gab_snative(gab, "at", gab_mod_shp_at),
          },
          {
              gab_message(gab, "len"),
              t,
              gab_snative(gab, "len", gab_mod_shp_len),
          },
          {
              gab_message(gab, "seq\\next"),
              t,
              gab_snative(gab, "seq\\next", gab_mod_shp_seq_next),
          },
          {
              gab_message(gab, "seq\\init"),
              t,
              gab_snative(gab, "seq\\init", gab_mod_shp_seq_init),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
