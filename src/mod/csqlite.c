#include "gab.h"

// Sqlite3 fails to compile with mremap for some reason
#define HAVE_MREMAP 0
#include "sqlite3.c"

#define DBTYPE "db\\row"

GAB_DYNLIB_NATIVE_FN(row, open) {
  gab_value db = gab_box(gab, (struct gab_box_argt){
                                  .size = sizeof(sqlite3 *),
                                  .type = gab_string(gab, DBTYPE),
                              });

  int res = sqlite3_open_v2(":memory:", gab_boxdata(db), 0, nullptr);

  if (res != SQLITE_OK) {
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Could not open database"));
    return gab_union_cvalid(gab_nil);
  }

  return gab_union_cvalid(gab_nil);
  gab_vmpush(gab_thisvm(gab), gab_ok, db);
}

GAB_DYNLIB_NATIVE_FN(row, exec) {
  gab_value db = gab_arg(0);

  sqlite3 *sqlite = *(sqlite3 **)gab_boxdata(db);

  gab_value stmt = gab_arg(1);

  if (gab_valkind(stmt) != kGAB_STRING)
    return gab_pktypemismatch(gab, stmt, kGAB_STRING);

  const char *sql = gab_strdata(&stmt);

  sqlite3_stmt *stmts = nullptr;
  int res = sqlite3_prepare_v2(sqlite, sql, gab_strlen(stmt), &stmts, nullptr);

  if (res != SQLITE_OK) {
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Could not prepare statement"));
    return gab_union_cvalid(gab_nil);
  }

  while ((res = sqlite3_step(stmts)) != SQLITE_DONE) {
    switch (res) {
    case SQLITE_ROW:
        // Do some row accumulation
      break;
    case SQLITE_OK:
      break;
    default:
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Error in statement")),
             gab_union_cvalid(gab_nil);
    }
  }
}

GAB_DYNLIB_MAIN_FN { return gab_union_cvalid(gab_ok); }
