#include "gab.h"

// Sqlite3 fails to compile with mremap for some reason
#define HAVE_MREMAP 0
// SQLITE3 Configuration
#define SQLITE_CONFIG_MULTITHREADED
#include "sqlite3.c"

/*
 * The strategy for multithreading SQLITE here is as follows:
 *
 * In Sqlite, Concurrent reads are fine. However, there is only a single write
 * allowed. (Readers don't block writers, because of the write-ahead-log)
 *
 * Enable 'multithreading' mode. In this mode, there are *no mutexes* compiled
 * into sqlite's code.
 *
 * It is *our* responsibility to ensure that no two threads access the same
 * connection or sqlite object (ie, prepared statement) at the same time.
 *
 * For connections, we accomplish this by lazily establishing a connection for
 * each Gab worker thread that wants to access the sqlite database.
 */

#define DBTYPE "db\\row"

struct gab_sqlite_conn {
  char path[1024];
  int len;
  sqlite3 *connections[];
};

gab_value make_conn(struct gab_triple gab) {
  gab_value conn =
      gab_box(gab, (struct gab_box_argt){
                       .size = sizeof(struct gab_sqlite_conn) +
                               gab_eglen(gab.eg) * sizeof(sqlite3 *),
                       .type = gab_string(gab, DBTYPE),
                   });

  struct gab_sqlite_conn *sql = gab_boxdata(conn);

  sql->len = gab_eglen(gab.eg);
  strncpy(sql->path, ":memory:", sizeof(sql->path));

  return conn;
}

int get_conn(struct gab_triple gab, gab_value conn, sqlite3 **out_conn) {
  struct gab_sqlite_conn *sql = gab_boxdata(conn);
  sqlite3 *sql_conn = sql->connections[gab.wkid];

  if (sql_conn != nullptr)
    return *out_conn = sql_conn, SQLITE_OK;

  int res =
      sqlite3_open_v2(sql->path, sql->connections + gab.wkid,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);

  return *out_conn = sql_conn, res;
}

GAB_DYNLIB_NATIVE_FN(row, open) {
  gab_value db = make_conn(gab);

  return gab_vmpush(gab_thisvm(gab), gab_ok, db), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(row, exec) {
  gab_value conn = gab_arg(0);

  sqlite3 *sqlite;
  int res = get_conn(gab, conn, &sqlite);

  if (res != SQLITE_OK)
    return gab_panicf(gab, "$", gab_string(gab, sqlite3_errmsg(sqlite)));

  gab_value stmt = gab_arg(1);

  if (gab_valkind(stmt) != kGAB_STRING)
    return gab_pktypemismatch(gab, stmt, kGAB_STRING);

  const char *sql = gab_strdata(&stmt);

  sqlite3_stmt *stmts = nullptr;
  res = sqlite3_prepare_v2(sqlite, sql, gab_strlen(stmt), &stmts, nullptr);

  if (res != SQLITE_OK) {
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Could not prepare statement"));
    return gab_union_cvalid(gab_nil);
  }

  /*
   * Rework this function significantly
   */

  while ((res = sqlite3_step(stmts)) != SQLITE_DONE) {
    switch (res) {
    case SQLITE_ROW:
      // Do some row accumulation
      int cols = sqlite3_column_count(stmts);
      // cols may be 0, if there is no data returned.

      for (int i = 0; i < cols; i++) {
        sqlite3_value *v = sqlite3_column_value(stmts, i);

        switch (sqlite3_column_type(stmts, i)) {
        case SQLITE_INTEGER:
        case SQLITE_FLOAT:
          gab_vmpush(gab_thisvm(gab), gab_number(sqlite3_value_double(v)));
          break;
        case SQLITE_TEXT:
          gab_vmpush(gab_thisvm(gab),
                     gab_string(gab, (const char *)sqlite3_value_text(v)));
          break;
        case SQLITE_BLOB:
          gab_vmpush(gab_thisvm(gab), gab_nbinary(gab, sqlite3_blob_bytes(v),
                                                  sqlite3_value_blob(v)));
          break;
        case SQLITE_NULL:
          gab_vmpush(gab_thisvm(gab), gab_nil);
          break;
        }
      }
      break;
    case SQLITE_DONE:
      break;
    default:
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, sqlite3_errmsg(sqlite))),
             gab_union_cvalid(gab_nil);
    }
  }

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "sqlite");
  gab_value type = gab_string(gab, DBTYPE);

  gab_def(gab,
          {
              gab_message(gab, "make"),
              mod,
              gab_snative(gab, "make", gab_mod_row_open),
          },
          {
              gab_message(gab, "exec"),
              type,
              gab_snative(gab, "exec", gab_mod_row_exec),
          }, );

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
