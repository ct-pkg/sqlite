/*
 * Sqlite.shim.h - handles, adaptation, and the two idempotent closers.
 *
 * The handle is opaque to CT: `SqliteDB *` / `SqliteStmt *` are the
 * only spellings callers see. Wrapping the C pointer (instead of hosting
 * methods on `sqlite3 *` / `sqlite3_stmt *`) buys two things:
 *
 *   1. `close()` / `finalize()` can clear the inner pointer, so a second call
 *      is a no-op success - the idempotence the old `db_close` / `stmt_finalize`
 *      offered;
 *   2. the method hosts are package-private types, so they cannot collide with
 *      another package's `close` on a shared C type.
 *
 * The wrapper itself is deliberately never freed, so a second close never
 * touches released memory (the same trade-off as std::io's `ct_io_file`).
 * That is the only cost: one small allocation per open / prepare.
 *
 * The rest of this header is the adaptation surface - shapes a map recipe
 * cannot spell (out-parameters, the `const unsigned char *` -> `const char *`
 * narrowing, NULL-safe forwarding). Every helper is `static inline`, so there
 * is no `.shim.c`. The `*` counts below must match `Sqlite.m.ct` verbatim.
 *
 * License: MIT (see LICENSE).
 */
#ifndef SQLITE_SHIM_H
#define SQLITE_SHIM_H

#include <sqlite3.h>
#include <stdlib.h>

/* The callback signature `sqlite3_exec` expects, spelled once so a trait can
   name it. */
typedef int (*ct_sqlite_cb)(void *ud, int n, char **vals, char **cols);

/* Private C tag typedefs. The CT-visible names are `pub type` aliases in
   Sqlite.m.ct; the C helpers below stay on these package-private spellings. */
typedef struct ct_sqlite_db ct_sqlite_db;
typedef struct ct_sqlite_stmt ct_sqlite_stmt;

struct ct_sqlite_db {
    sqlite3 *db;
};

struct ct_sqlite_stmt {
    sqlite3_stmt *st;
};

/* ---- opening ---------------------------------------------------------- */

static inline int ct_sqlite_open_v2(const char *path, ct_sqlite_db **out,
                                    int flags)
{
    ct_sqlite_db *w;
    int rc;

    if (!out) return SQLITE_MISUSE;
    *out = 0;
    w = (ct_sqlite_db *)malloc(sizeof *w);
    if (!w) return SQLITE_NOMEM;
    w->db = 0;
    rc = sqlite3_open_v2(path, &w->db, flags, 0);
    /* Even on failure `w` is handed back: SQLite may have returned a handle
       that still has to be closed. */
    *out = w;
    return rc;
}

static inline int ct_sqlite_open(const char *path, ct_sqlite_db **out)
{
    return ct_sqlite_open_v2(path, out,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
}

/* ---- connection ------------------------------------------------------- */

/* Idempotent: a NULL or already-closed handle is a no-op success. */
static inline int ct_sqlite_close(ct_sqlite_db *w)
{
    int rc;

    if (!w || !w->db) return SQLITE_OK;
    rc = sqlite3_close_v2(w->db);
    w->db = 0;
    return rc;
}

/* Receiver-style open, for `dbPath.openSqliteDb()`. Success returns the
   handle; any failure returns NULL after releasing the wrapper. Reuses
   `ct_sqlite_open`, so `Lib.open` stays the single rc-reporting path. */
static inline ct_sqlite_db *ct_sqlite_open_path(const char *path)
{
    ct_sqlite_db *db = 0;

    if (ct_sqlite_open(path, &db) != SQLITE_OK) {
        ct_sqlite_close(db); /* NULL-safe and idempotent */
        return 0;
    }
    return db;
}

static inline const char *ct_sqlite_errmsg(ct_sqlite_db *w)
{
    return sqlite3_errmsg(w ? w->db : 0);
}

static inline int ct_sqlite_errcode(ct_sqlite_db *w)
{
    return sqlite3_errcode(w ? w->db : 0);
}

static inline int ct_sqlite_busy_timeout(ct_sqlite_db *w, int ms)
{
    return sqlite3_busy_timeout(w ? w->db : 0, ms);
}

static inline int ct_sqlite_exec(ct_sqlite_db *w, const char *sql)
{
    return sqlite3_exec(w ? w->db : 0, sql, 0, 0, 0);
}

static inline int ct_sqlite_exec_cb(ct_sqlite_db *w, const char *sql,
                                    ct_sqlite_cb cb, void *ud)
{
    return sqlite3_exec(w ? w->db : 0, sql, cb, ud, 0);
}

static inline int ct_sqlite_changes(ct_sqlite_db *w)
{
    return sqlite3_changes(w ? w->db : 0);
}

static inline long long ct_sqlite_last_insert_rowid(ct_sqlite_db *w)
{
    return (long long)sqlite3_last_insert_rowid(w ? w->db : 0);
}

static inline int ct_sqlite_prepare(ct_sqlite_db *w, const char *sql,
                                    ct_sqlite_stmt **out)
{
    ct_sqlite_stmt *s;

    if (!out) return SQLITE_MISUSE;
    *out = 0;
    s = (ct_sqlite_stmt *)malloc(sizeof *s);
    if (!s) return SQLITE_NOMEM;
    s->st = 0;
    *out = s;
    /* On failure SQLite leaves `s->st` at 0, so `finalize()` stays safe. */
    return sqlite3_prepare_v2(w ? w->db : 0, sql, -1, &s->st, 0);
}

/* ---- statement -------------------------------------------------------- */

static inline int ct_sqlite_step(ct_sqlite_stmt *w)
{
    return sqlite3_step(w ? w->st : 0);
}

/* Idempotent: a NULL or already-finalized handle is a no-op success. */
static inline int ct_sqlite_finalize(ct_sqlite_stmt *w)
{
    int rc;

    if (!w || !w->st) return SQLITE_OK;
    rc = sqlite3_finalize(w->st);
    w->st = 0;
    return rc;
}

static inline int ct_sqlite_reset(ct_sqlite_stmt *w)
{
    return sqlite3_reset(w ? w->st : 0);
}

static inline int ct_sqlite_clear_bindings(ct_sqlite_stmt *w)
{
    return sqlite3_clear_bindings(w ? w->st : 0);
}

/* ---- binding ---------------------------------------------------------- */

/* `n` is the byte count; -1 means "up to the NUL". The text is copied
   (`SQLITE_TRANSIENT`), so the caller's buffer is free right after the call. */
static inline int ct_sqlite_bind_text(ct_sqlite_stmt *w, int idx,
                                      const char *text, int n)
{
    return sqlite3_bind_text(w ? w->st : 0, idx, text, n, SQLITE_TRANSIENT);
}

static inline int ct_sqlite_bind_int(ct_sqlite_stmt *w, int idx, int v)
{
    return sqlite3_bind_int(w ? w->st : 0, idx, v);
}

static inline int ct_sqlite_bind_int64(ct_sqlite_stmt *w, int idx,
                                       long long v)
{
    return sqlite3_bind_int64(w ? w->st : 0, idx, (sqlite3_int64)v);
}

static inline int ct_sqlite_bind_double(ct_sqlite_stmt *w, int idx, double v)
{
    return sqlite3_bind_double(w ? w->st : 0, idx, v);
}

/* ---- columns ---------------------------------------------------------- */

static inline int ct_sqlite_column_count(ct_sqlite_stmt *w)
{
    return sqlite3_column_count(w ? w->st : 0);
}

static inline const char *ct_sqlite_column_name(ct_sqlite_stmt *w, int i)
{
    return sqlite3_column_name(w ? w->st : 0, i);
}

static inline int ct_sqlite_column_type(ct_sqlite_stmt *w, int i)
{
    return sqlite3_column_type(w ? w->st : 0, i);
}

static inline int ct_sqlite_column_int(ct_sqlite_stmt *w, int i)
{
    return sqlite3_column_int(w ? w->st : 0, i);
}

static inline long long ct_sqlite_column_int64(ct_sqlite_stmt *w, int i)
{
    return (long long)sqlite3_column_int64(w ? w->st : 0, i);
}

static inline double ct_sqlite_column_double(ct_sqlite_stmt *w, int i)
{
    return sqlite3_column_double(w ? w->st : 0, i);
}

static inline const char *ct_sqlite_column_text(ct_sqlite_stmt *w, int i)
{
    return (const char *)sqlite3_column_text(w ? w->st : 0, i);
}

#endif
