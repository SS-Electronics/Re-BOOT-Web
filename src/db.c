#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static sqlite3 *g_db = NULL;

static const char *SCHEMA =
    "CREATE TABLE IF NOT EXISTS user ("
    "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username TEXT    NOT NULL UNIQUE,"
    "  pwhash   TEXT    NOT NULL,"
    "  role     TEXT    NOT NULL DEFAULT 'user'"
    ");"
    "CREATE TABLE IF NOT EXISTS job ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name        TEXT    NOT NULL DEFAULT '',"
    "  hex_file    TEXT    NOT NULL,"
    "  node_id     TEXT    NOT NULL,"
    "  interface   TEXT    NOT NULL,"
    "  device      TEXT    NOT NULL,"
    "  tcp_port    TEXT    DEFAULT '',"
    "  retries     TEXT    DEFAULT '',"
    "  reset_flag  TEXT    DEFAULT '',"
    "  verbose     TEXT    DEFAULT '',"
    "  extra_args  TEXT    DEFAULT '',"
    "  status      TEXT    NOT NULL DEFAULT 'pending',"
    "  created_at  TEXT    DEFAULT (datetime('now')),"
    "  started_at  TEXT    DEFAULT '',"
    "  finished_at TEXT    DEFAULT '',"
    "  exit_code   INTEGER DEFAULT -1,"
    "  user_id     INTEGER NOT NULL,"
    "  FOREIGN KEY (user_id) REFERENCES user(id)"
    ");"
    "CREATE TABLE IF NOT EXISTS session ("
    "  token      TEXT    PRIMARY KEY,"
    "  user_id    INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL,"
    "  FOREIGN KEY (user_id) REFERENCES user(id)"
    ");";

#define COL(s, stmt, idx) \
    do { const char *_v = (const char *)sqlite3_column_text(stmt, idx); \
         strncpy(s, _v ? _v : "", sizeof(s)-1); } while(0)

static void fill_user(sqlite3_stmt *stmt, User *u) {
    u->id = sqlite3_column_int64(stmt, 0);
    COL(u->username, stmt, 1);
    COL(u->pwhash,   stmt, 2);
    COL(u->role,     stmt, 3);
}

static void fill_job(sqlite3_stmt *stmt, Job *j) {
    j->id = sqlite3_column_int64(stmt, 0);
    COL(j->name,        stmt,  1);
    COL(j->hex_file,    stmt,  2);
    COL(j->node_id,     stmt,  3);
    COL(j->interface,   stmt,  4);
    COL(j->device,      stmt,  5);
    COL(j->tcp_port,    stmt,  6);
    COL(j->retries,     stmt,  7);
    COL(j->reset_flag,  stmt,  8);
    COL(j->verbose,     stmt,  9);
    COL(j->extra_args,  stmt, 10);
    COL(j->status,      stmt, 11);
    COL(j->created_at,  stmt, 12);
    COL(j->started_at,  stmt, 13);
    COL(j->finished_at, stmt, 14);
    j->exit_code = sqlite3_column_int(stmt, 15);
    j->user_id   = sqlite3_column_int64(stmt, 16);
    COL(j->username,    stmt, 17);
}

int db_open(const char *path) {
    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "[db] open failed: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    char *err = NULL;
    if (sqlite3_exec(g_db, SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "[db] schema error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    return 0;
}

void db_close(void) {
    if (g_db) { sqlite3_close(g_db); g_db = NULL; }
}

/* --- Users --- */

int db_user_by_username(const char *username, User *out) {
    sqlite3_stmt *s;
    int rc = -1;
    if (sqlite3_prepare_v2(g_db,
        "SELECT id,username,pwhash,role FROM user WHERE username=?",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s, 1, username, -1, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) { fill_user(s, out); rc = 0; }
    sqlite3_finalize(s);
    return rc;
}

int db_user_by_id(int64_t id, User *out) {
    sqlite3_stmt *s;
    int rc = -1;
    if (sqlite3_prepare_v2(g_db,
        "SELECT id,username,pwhash,role FROM user WHERE id=?",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(s, 1, id);
    if (sqlite3_step(s) == SQLITE_ROW) { fill_user(s, out); rc = 0; }
    sqlite3_finalize(s);
    return rc;
}

int db_user_create(const char *username, const char *pwhash,
                   const char *role, int64_t *id_out) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "INSERT INTO user (username,pwhash,role) VALUES (?,?,?)",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 2, pwhash,   -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 3, role,     -1, SQLITE_STATIC);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    if (rc == 0 && id_out) *id_out = sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(s);
    return rc;
}

int db_user_delete(int64_t id) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "DELETE FROM user WHERE id=?", -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(s, 1, id);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(s);
    return rc;
}

int db_user_list(User **out, int *cnt) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "SELECT id,username,pwhash,role FROM user ORDER BY id",
        -1, &s, NULL) != SQLITE_OK) return -1;
    int cap = 16, n = 0;
    User *arr = malloc(sizeof(User) * cap);
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (n == cap) { cap *= 2; arr = realloc(arr, sizeof(User)*cap); }
        fill_user(s, &arr[n++]);
    }
    sqlite3_finalize(s);
    *out = arr; *cnt = n;
    return 0;
}

/* --- Jobs --- */

#define JOB_SELECT \
    "SELECT j.id,j.name,j.hex_file,j.node_id,j.interface,j.device," \
    "j.tcp_port,j.retries,j.reset_flag,j.verbose,j.extra_args," \
    "j.status,j.created_at,j.started_at,j.finished_at,j.exit_code," \
    "j.user_id,u.username " \
    "FROM job j JOIN user u ON j.user_id=u.id "

int64_t db_job_create(const Job *j) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "INSERT INTO job (name,hex_file,node_id,interface,device,tcp_port,"
        "retries,reset_flag,verbose,extra_args,user_id) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s,  1, j->name,       -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  2, j->hex_file,   -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  3, j->node_id,    -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  4, j->interface,  -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  5, j->device,     -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  6, j->tcp_port,   -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  7, j->retries,    -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  8, j->reset_flag, -1, SQLITE_STATIC);
    sqlite3_bind_text(s,  9, j->verbose,    -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 10, j->extra_args, -1, SQLITE_STATIC);
    sqlite3_bind_int64(s,11, j->user_id);
    int64_t id = -1;
    if (sqlite3_step(s) == SQLITE_DONE) id = sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(s);
    return id;
}

int db_job_get(int64_t id, Job *out) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db, JOB_SELECT "WHERE j.id=?",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(s, 1, id);
    int rc = -1;
    if (sqlite3_step(s) == SQLITE_ROW) { fill_job(s, out); rc = 0; }
    sqlite3_finalize(s);
    return rc;
}

int db_job_list(Job **out, int *cnt) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        JOB_SELECT "ORDER BY j.id DESC LIMIT 100",
        -1, &s, NULL) != SQLITE_OK) return -1;
    int cap = 16, n = 0;
    Job *arr = malloc(sizeof(Job) * cap);
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (n == cap) { cap *= 2; arr = realloc(arr, sizeof(Job)*cap); }
        fill_job(s, &arr[n++]);
    }
    sqlite3_finalize(s);
    *out = arr; *cnt = n;
    return 0;
}

int db_job_set_started(int64_t id) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "UPDATE job SET status='running', started_at=datetime('now') WHERE id=?",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(s, 1, id);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(s);
    return rc;
}

int db_job_set_finished(int64_t id, const char *status, int exit_code) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "UPDATE job SET status=?, finished_at=datetime('now'), exit_code=? WHERE id=?",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s,  1, status,    -1, SQLITE_STATIC);
    sqlite3_bind_int(s,   2, exit_code);
    sqlite3_bind_int64(s, 3, id);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(s);
    return rc;
}

int db_job_delete(int64_t id) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "DELETE FROM job WHERE id=?", -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(s, 1, id);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(s);
    return rc;
}

/* --- Sessions --- */

int db_session_create(const char *token, int64_t user_id, int64_t expires_at) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "INSERT OR REPLACE INTO session (token,user_id,expires_at) VALUES (?,?,?)",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s,  1, token,      -1, SQLITE_STATIC);
    sqlite3_bind_int64(s, 2, user_id);
    sqlite3_bind_int64(s, 3, expires_at);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(s);
    return rc;
}

int db_session_user(const char *token, int64_t now, int64_t *uid_out) {
    sqlite3_stmt *s;
    int rc = -1;
    if (sqlite3_prepare_v2(g_db,
        "SELECT user_id FROM session WHERE token=? AND expires_at>?",
        -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s,  1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(s, 2, now);
    if (sqlite3_step(s) == SQLITE_ROW) {
        *uid_out = sqlite3_column_int64(s, 0);
        rc = 0;
    }
    sqlite3_finalize(s);
    return rc;
}

int db_session_delete(const char *token) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(g_db,
        "DELETE FROM session WHERE token=?", -1, &s, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(s, 1, token, -1, SQLITE_STATIC);
    int rc = sqlite3_step(s) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(s);
    return rc;
}
