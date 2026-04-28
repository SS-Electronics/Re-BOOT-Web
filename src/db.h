#pragma once
#include <stdint.h>
#include <sqlite3.h>

#define DB_STR64   64
#define DB_STR128  128
#define DB_STR256  256
#define DB_STR512  512

typedef struct {
    int64_t id;
    char    username[DB_STR64];
    char    pwhash[DB_STR128];  /* "salt:sha256hex" */
    char    role[16];           /* "admin" | "user" */
} User;

typedef struct {
    int64_t id;
    char    name[DB_STR128];
    char    hex_file[DB_STR256];
    char    node_id[32];
    char    interface[16];      /* serial | tcp | can */
    char    device[DB_STR128];
    char    tcp_port[8];
    char    retries[4];
    char    reset_flag[4];
    char    verbose[4];
    char    extra_args[DB_STR512];
    char    status[16];         /* pending | running | success | failed */
    char    created_at[32];
    char    started_at[32];
    char    finished_at[32];
    int     exit_code;
    int64_t user_id;
    char    username[DB_STR64]; /* joined from user table */
} Job;

/* Lifecycle */
int  db_open(const char *path);
void db_close(void);

/* Users */
int     db_user_by_username(const char *username, User *out);
int     db_user_by_id(int64_t id, User *out);
int     db_user_create(const char *username, const char *pwhash,
                       const char *role, int64_t *id_out);
int     db_user_delete(int64_t id);
int     db_user_list(User **out, int *cnt);

/* Jobs */
int64_t db_job_create(const Job *j);
int     db_job_get(int64_t id, Job *out);
int     db_job_list(Job **out, int *cnt);
int     db_job_set_started(int64_t id);
int     db_job_set_finished(int64_t id, const char *status, int exit_code);
int     db_job_delete(int64_t id);

/* Sessions */
int  db_session_create(const char *token, int64_t user_id, int64_t expires_at);
int  db_session_user(const char *token, int64_t now, int64_t *uid_out);
int  db_session_delete(const char *token);
