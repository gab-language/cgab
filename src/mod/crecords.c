#include "gab.h"

a_gab_value *gab_reclib_at(struct gab_triple gab, uint64_t argc,
                           gab_value argv[argc]) {
  gab_value rec = gab_arg(0);
  gab_value key = gab_arg(1);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_value val = gab_recat(rec, key);

  if (val == gab_invalid)
    gab_vmpush(gab_thisvm(gab), gab_none);
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, val);

  return nullptr;
}

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)
#define CLAMP(a, b) (MAX(0, MIN(a, b)))

a_gab_value *gab_reclib_slice(struct gab_triple gab, uint64_t argc,
                              gab_value argv[argc]) {
  gab_value rec = gab_arg(0);
  uint64_t len = gab_reclen(argv[0]);
  uint64_t start = 0, end = len;

  switch (argc) {
  case 2: {
    if (gab_valkind(argv[1]) != kGAB_NUMBER) {
      return gab_fpanic(gab, "&:slice expects a number as the second argument");
    }

    double a = gab_valtof(argv[1]);
    end = MIN(a, len);
    break;
  }

  case 3:
    if (gab_valkind(argv[1]) == kGAB_NUMBER) {
      start = MIN(gab_valtou(argv[1]), len);
    } else if (argv[1] == gab_nil) {
      return gab_fpanic(gab, "&:slice expects a number as the second argument");
    }

    if (gab_valkind(argv[2]) == kGAB_NUMBER) {
      end = MIN(gab_valtou(argv[2]), len);
    } else if (argv[2] == gab_nil) {
      return gab_fpanic(gab, "&:slice expects a number as the third argument");
    }
    break;

  default:
    return gab_fpanic(gab, "&:slice expects 2 or 3 arguments");
  }

  if (start > end) {
    return gab_fpanic(gab, "&:slice expects the start to be before the end");
  }

  uint64_t size = end - start;

  if (size == 0) {
    gab_vmpush(gab_thisvm(gab), gab_listof(gab));
    return nullptr;
  }

  gab_value vs[size];
  for (uint64_t i = 0; i < size; i++) {
    vs[i] = gab_uvrecat(rec, start + i);
  }

  gab_vmpush(gab_thisvm(gab), gab_list(gab, size, vs));

  return nullptr;
}

a_gab_value *gab_reclib_push(struct gab_triple gab, uint64_t argc,
                             gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  rec = gab_nlstpush(gab, rec, argc - 1, argv + 1);

  gab_vmpush(gab_thisvm(gab), rec);

  return nullptr;
}

a_gab_value *gab_reclib_pop(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_value res;

  gab_vmpush(gab_thisvm(gab), gab_lstpop(gab, rec, &res));
  gab_vmpush(gab_thisvm(gab), res);

  return nullptr;
}

a_gab_value *gab_reclib_put(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  gab_value rec = gab_arg(0);
  gab_value key = gab_arg(1);
  gab_value val = gab_arg(2);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_vmpush(gab_thisvm(gab), gab_recput(gab, rec, key, val));

  return nullptr;
}

a_gab_value *gab_reclib_take(struct gab_triple gab, uint64_t argc,
                             gab_value argv[argc]) {
  gab_value rec = gab_arg(0);
  gab_value key = gab_arg(1);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_value v = gab_nil;

  gab_vmpush(gab_thisvm(gab), gab_rectake(gab, rec, key, &v));

  gab_vmpush(gab_thisvm(gab), v);

  return nullptr;
}

a_gab_value *gab_reclib_is_empty(struct gab_triple gab, uint64_t argc,
                                 gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_value shp = gab_recshp(rec);
  uint64_t len = gab_shplen(shp);

  gab_vmpush(gab_thisvm(gab), gab_bool(len == 0));
  return nullptr;
}

a_gab_value *gab_reclib_is_list(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_value shp = gab_recshp(rec);

  gab_vmpush(gab_thisvm(gab), gab_bool(gab_shpislist(shp)));

  return nullptr;
}

gab_value doputvia(struct gab_triple gab, gab_value rec, gab_value val,
                   uint64_t len, gab_value path[len]) {
  gab_value key = path[0];

  if (len == 1)
    return gab_recput(gab, rec, key, val);

  gab_value subrec = gab_recat(rec, key);

  if (subrec == gab_invalid)
    subrec = gab_record(gab, 0, 0, path, path);

  if (gab_valkind(subrec) != kGAB_RECORD)
    return gab_invalid;

  gab_value subval = doputvia(gab, subrec, val, len - 1, path + 1);

  if (subval == gab_invalid)
    return gab_invalid;

  return gab_recput(gab, rec, key, subval);
}

a_gab_value *gab_reclib_putvia(struct gab_triple gab, uint64_t argc,
                               gab_value argv[argc]) {
  gab_value rec = gab_arg(0);
  gab_value val = gab_arg(1);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  if (argc == 2) {
    gab_vmpush(gab_thisvm(gab), rec);
    return nullptr;
  }

  gab_value result = doputvia(gab, rec, val, argc - 2, argv + 2);

  if (result == gab_invalid)
    return gab_fpanic(gab, "Invalid path for $ on $",
                      gab_message(gab, "putvia"), rec);

  gab_vmpush(gab_thisvm(gab), result);

  return nullptr;
}

a_gab_value *gab_reclib_len(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  gab_vmpush(gab_thisvm(gab), gab_number(gab_reclen(rec)));

  return nullptr;
}

a_gab_value *gab_reclib_strings_into(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  FILE *tmp = tmpfile();

  if (tmp == nullptr)
    return gab_fpanic(gab, "Failed: $", gab_string(gab, strerror(errno)));

  int bytes = gab_fvalinspect(tmp, rec, 3);

  rewind(tmp);

  uint8_t b[bytes];
  if (fread(b, sizeof(uint8_t), bytes, tmp) != bytes)
    return gab_fpanic(gab, "Failed: $", gab_string(gab, strerror(errno)));

  if (fclose(tmp))
    return gab_fpanic(gab, "Failed: $", gab_string(gab, strerror(errno)));

  gab_value str = gab_nstring(gab, bytes, (char *)b);

  gab_vmpush(gab_thisvm(gab), str);

  return nullptr;
}

a_gab_value *gab_reclib_seqinit(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
  gab_value rec = gab_arg(0);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  uint64_t len = gab_reclen(rec);

  if (len == 0) {
    gab_vmpush(gab_thisvm(gab), gab_none);
    return nullptr;
  }

  gab_value key = gab_ukrecat(rec, 0);
  gab_value val = gab_uvrecat(rec, 0);

  gab_vmpush(gab_thisvm(gab), gab_ok, key, val, key);
  return nullptr;
}

a_gab_value *gab_reclib_seqnext(struct gab_triple gab, uint64_t argc,
                                gab_value argv[argc]) {
  gab_value rec = gab_arg(0);
  gab_value old_key = gab_arg(1);

  if (gab_valkind(rec) != kGAB_RECORD)
    return gab_pktypemismatch(gab, rec, kGAB_RECORD);

  uint64_t len = gab_reclen(rec);

  if (len == 0)
    goto fin;

  uint64_t i = gab_recfind(rec, old_key);

  if (i == -1 || i + 1 == len)
    goto fin;

  gab_value key = gab_ukrecat(rec, i + 1);
  gab_value val = gab_uvrecat(rec, i + 1);

  gab_vmpush(gab_thisvm(gab), gab_ok, key, val, key);
  return nullptr;

fin:
  gab_vmpush(gab_thisvm(gab), gab_none);
  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_RECORD);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "slice"),
              t,
              gab_snative(gab, "slice", gab_reclib_slice),
          },
          {
              gab_message(gab, "put_via"),
              t,
              gab_snative(gab, "put_via", gab_reclib_putvia),
          },
          {
              gab_message(gab, "push"),
              t,
              gab_snative(gab, "push", gab_reclib_push),
          },
          {
              gab_message(gab, "empty?"),
              t,
              gab_snative(gab, "empty?", gab_reclib_is_empty),
          },
          {
              gab_message(gab, "list?"),
              t,
              gab_snative(gab, "list?", gab_reclib_is_list),
          },
          {
              gab_message(gab, "take"),
              t,
              gab_snative(gab, "take", gab_reclib_take),
          },
          {
              gab_message(gab, "pop"),
              t,
              gab_snative(gab, "pop", gab_reclib_pop),
          },
          {
              gab_message(gab, "put"),
              t,
              gab_snative(gab, "put", gab_reclib_put),
          },
          {
              gab_message(gab, "at"),
              t,
              gab_snative(gab, "at", gab_reclib_at),
          },
          {
              gab_message(gab, "len"),
              t,
              gab_snative(gab, "len", gab_reclib_len),
          },
          {
              gab_message(gab, "seq\\next"),
              t,
              gab_snative(gab, "seq\\next", gab_reclib_seqnext),
          },
          {
              gab_message(gab, "seq\\init"),
              t,
              gab_snative(gab, "seq\\init", gab_reclib_seqinit),
          });

  return a_gab_value_one(gab_ok);
}
