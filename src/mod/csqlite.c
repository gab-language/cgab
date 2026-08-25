/**
 *  MIT License
 *
 *  Copyright (c) 2023-2026 Teddy Randby
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "cgab.h"
#include <threads.h>

// Sqlite3 fails to compile with mremap for some reason
#define HAVE_MREMAP 0
// SQLITE3 Configuration
#define SQLITE_CONFIG_MULTITHREADED
#include "sqlite3.c"

/*
 * The strategy for multithreading SQLITE here is as follows:
 *
 * In Sqlite, parallel reads are fine. However, there is only a single write
 * allowed. (Readers don't block writers, because of the write-ahead-log)
 *
 * Enable 'multithreading' mode. In this mode, there are *no mutexes* compiled
 * into sqlite's code.
 *
 * It is *our* responsibility to ensure that no two threads access the same
 * connection or sqlite object (ie, prepared statement) at the same time.
 *
 * We accomplish this by lazily establishing a connection for
 * each Gab worker thread that wants to access the sqlite database.
 *
 * prepared statements don't ever survive beyond a single native function,
 * so there is no synchronization needed for them.
 */

#define cGAB_CSQLITE_TYPE "db\\row"
#define cGAB_CSQLITE_STMT_TYPE "db\\row\\stmt"

#define cGAB_CSQLITE_MAXPATH 2048

struct gab_sqlite_conn {
  char path[cGAB_CSQLITE_MAXPATH];
  int len;
  sqlite3 *connections[];
};

gab_value make_conn(struct gab_triple gab, const char *path) {
  gab_value conn =
      gab_box(gab, (struct gab_box_argt){
                       .size = sizeof(struct gab_sqlite_conn) +
                               gab_eglen(gab.eg) * sizeof(sqlite3 *),
                       .type = gab_string(gab, cGAB_CSQLITE_TYPE),
                   });

  struct gab_sqlite_conn *sql = gab_boxdata(conn);

  sql->len = gab_eglen(gab.eg);
  strncpy(sql->path, path, sizeof(sql->path));

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

struct gab_sqlite_stmt {
  sqlite3_stmt *stmts;
  mtx_t mtx;
};

void destroy_stmts(struct gab_triple, uint64_t len, char data[len]) {
  gab_assert(len == sizeof(struct gab_sqlite_stmt), "Wrongly sized stmt");

  struct gab_sqlite_stmt *stmt = (void *)(data);
  sqlite3_finalize(stmt->stmts);
  mtx_destroy(&stmt->mtx);
}

gab_value make_stmts(struct gab_triple gab, sqlite3 *sqlite, uint64_t len,
                     const char sql[len], uint64_t argc, gab_value *argv) {
  struct gab_sqlite_stmt stmts = {};
  int res = mtx_init(&stmts.mtx, mtx_plain);

  res = sqlite3_prepare_v2(sqlite, sql, len, &stmts.stmts, nullptr);

  if (res != SQLITE_OK)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, sqlite3_errmsg(sqlite))),
           gab_cinvalid;

  for (uint64_t i = 0; i < argc; i += 2) {
    gab_value sname = gab_arg(i);
    gab_value value = gab_arg(i + 1);

    gab_assert(gab_valkind(sname) == kGAB_STRING, "Invalid kind");

    const char *cname = gab_strdata(&sname);
    int idx = sqlite3_bind_parameter_index(stmts.stmts, cname);

    if (!idx)
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Invalid parameter name"), sname),
             gab_cinvalid;

    switch (gab_valkind(value)) {
    case kGAB_NUMBER: {
      if (sqlite3_bind_double(stmts.stmts, idx, gab_valtof(value)) != SQLITE_OK)
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, "Could not bind parameter"), sname,
                          value),
               gab_cinvalid;
      break;
    }
      // TODO @csqlite @qol: Better string lifetimes.
      // SQLITE_TRANSIENT Tells sql it should handle the lifetime of the string
      // data. This might not be necessary - we could probably make some
      // guarantees for sqlite about this. Might save time copying big strings.
    case kGAB_STRING: {
      if (sqlite3_bind_text(stmts.stmts, idx, gab_strdata(&value),
                            gab_strlen(value), SQLITE_TRANSIENT) != SQLITE_OK)
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, "Could not bind parameter"), sname,
                          value),
               gab_cinvalid;
      break;
    }
    case kGAB_BINARY: {
      if (sqlite3_bind_blob(stmts.stmts, idx, gab_strdata(&value),
                            gab_strlen(value), SQLITE_TRANSIENT) != SQLITE_OK)
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, "Could not bind parameter"), sname,
                          value),
               gab_cinvalid;
      break;
    }
    case kGAB_MESSAGE: {
      if (value == gab_nil) {
        if (sqlite3_bind_null(stmts.stmts, idx) != SQLITE_OK)
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Could not bind parameter"), sname,
                            value),
                 gab_cinvalid;

        break;
      }

      // TODO @csqlite @feat: Add true and false here?
      // Should they just bind to ints?

      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Invalid message value for binding")),
             gab_cinvalid;
    }
    default:
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Unhandled value type for binding"),
                        sname, value),
             gab_cinvalid;
    }
  }

  gab_value vstmts =
      gab_box(gab, (struct gab_box_argt){
                       .data = &stmts,
                       .size = sizeof(stmts),
                       .type = gab_string(gab, cGAB_CSQLITE_STMT_TYPE),
                       .destructor = destroy_stmts,
                   });

  return vstmts;
};

union gab_value_pair step_stmt(struct gab_triple gab,
                               struct gab_sqlite_stmt *stmt) {
  int res = mtx_trylock(&stmt->mtx);
  if (res == thrd_busy)
    return gab_union_ctimeout(gab_nil);

  gab_assert(res == thrd_success, "Must not fail if not busy");

  if (!stmt->stmts)
    goto done;

  res = sqlite3_step(stmt->stmts);

  switch (res) {
  case SQLITE_ROW:
    gab_push(gab, gab_ok);

    // Do some row accumulation
    int cols = sqlite3_column_count(stmt->stmts);
    // cols may be 0, if there is no data returned.

    for (int i = 0; i < cols; i++) {
      sqlite3_value *v = sqlite3_column_value(stmt->stmts, i);

      switch (sqlite3_column_type(stmt->stmts, i)) {
      case SQLITE_INTEGER:
      case SQLITE_FLOAT:
        gab_push(gab, gab_number(sqlite3_value_double(v)));
        break;
      case SQLITE_TEXT:
        gab_push(gab, gab_string(gab, (const char *)sqlite3_value_text(v)));
        break;
      case SQLITE_BLOB:
        gab_push(gab, gab_nbinary(gab, sqlite3_value_bytes(v),
                                  sqlite3_value_blob(v)));
        break;
      case SQLITE_NULL:
        gab_push(gab, gab_nil);
        break;
      }
    }
    return mtx_unlock(&stmt->mtx), gab_union_cvalid(gab_nil);
  case SQLITE_DONE:
    sqlite3_finalize(stmt->stmts);
    stmt->stmts = nullptr;
  done:
    gab_push(gab, gab_none);
    return mtx_unlock(&stmt->mtx), gab_union_cvalid(gab_nil);
  case SQLITE_BUSY:
    return mtx_unlock(&stmt->mtx), gab_union_ctimeout(gab_nil);
  default:
    return mtx_unlock(&stmt->mtx),
           gab_panicf(gab, "sqlite status @: @", gab_number(res),
                      gab_string(gab, sqlite3_errstr(res)));
    break;
  }
}

GAB_DYNLIB_NATIVE_FN(row, open) {
  gab_value path = gab_arg(1);

  gab_value conn = gab_nil;

  // TODO @bug: This seems like kind of a bad way to check for errors.

  // Create a connection with the given path.
  if (path == gab_nil)
    conn = make_conn(gab, ":memory:");
  else if (gab_valkind(path) == kGAB_STRING)
    conn = make_conn(gab, gab_strdata(&path));
  else
    return gab_pktypemismatch(gab, path, kGAB_STRING);

  // Test the connection by eagerly spawning one
  // for the current worker.
  sqlite3 *sqlite;
  int res = get_conn(gab, conn, &sqlite);

  // This can fail.
  if (res != SQLITE_OK)
    return gab_push(gab, gab_err, gab_string(gab, sqlite3_errmsg(sqlite))),
           gab_union_cvalid(gab_nil);

  return gab_push(gab, gab_ok, conn), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(row, query) {
  gab_value conn = gab_arg(0);

  sqlite3 *sqlite;
  int res = get_conn(gab, conn, &sqlite);

  if (res != SQLITE_OK)
    return gab_panicf(gab, "$", gab_string(gab, sqlite3_errmsg(sqlite)));

  gab_value stmt = gab_arg(1);

  if (gab_valkind(stmt) != kGAB_STRING)
    return gab_pktypemismatch(gab, stmt, kGAB_STRING);

  for (uint64_t i = 2; i < argc; i += 2) {
    gab_value sname = gab_arg(i);
    if (gab_valkind(sname) != kGAB_STRING)
      return gab_pktypemismatch(gab, sname, kGAB_STRING);
  }

  gab_value box = make_stmts(gab, sqlite, gab_strlen(stmt), gab_strdata(&stmt),
                             argc - 2, argv + 2);

  if (box == gab_cinvalid)
    return gab_union_cvalid(gab_nil);
  else
    return gab_push(gab, gab_ok, box), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(row, seq_init) {
  gab_value vstmt = gab_arg(0);

  struct gab_sqlite_stmt *stmt = gab_boxdata(vstmt);

  return step_stmt(gab, stmt);
}

GAB_DYNLIB_NATIVE_FN(row, seq_next) {
  gab_value vstmt = gab_arg(0);

  struct gab_sqlite_stmt *stmt = gab_boxdata(vstmt);

  return step_stmt(gab, stmt);
}

GAB_DYNLIB_MAIN_FN {
  gab_value conn_type = gab_string(gab, cGAB_CSQLITE_TYPE);
  gab_value conn_mod = gab_strtomsg(conn_type);

  gab_value stmt_type = gab_string(gab, cGAB_CSQLITE_STMT_TYPE);
  gab_value stmt_mod = gab_strtomsg(stmt_type);

  gab_def(gab,
          {
              gab_message(gab, "make"),
              conn_mod,
              gab_snative(gab, "make", gab_mod_row_open),
          },
          {
              gab_message(gab, "t"),
              conn_mod,
              conn_type,
          },
          {
              gab_message(gab, "Query"),
              conn_mod,
              stmt_mod,
          },
          {
              gab_message(gab, "t"),
              stmt_mod,
              stmt_type,
          },
          {
              gab_message(gab, "query"),
              conn_type,
              gab_snative(gab, "query", gab_mod_row_query),
          },
          {
              gab_message(gab, "seq\\init"),
              stmt_type,
              gab_snative(gab, "seq\\init", gab_mod_row_seq_init),
          },
          {
              gab_message(gab, "seq\\next"),
              stmt_type,
              gab_snative(gab, "seq\\next", gab_mod_row_seq_next),
          }, );

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = gab_valarray(gab_ok, conn_mod),
  };
}
