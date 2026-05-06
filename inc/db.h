/**
 * @file db.h
 * @brief SQLite3 persistence layer — users, jobs, and sessions.
 *
 * Thin wrapper around libsqlite3.  All operations use prepared statements.
 * The database is opened once at startup (db_open()) and closed at shutdown
 * (db_close()).  WAL journal mode is enabled for concurrent read access.
 *
 * @author Subhajit Roy <subhajitroy005@gmail.com>
 * @date   2026-05-06
 */
#pragma once
#include <stdint.h>
#include <sqlite3.h>

/** @defgroup db_str_sizes Fixed-width string buffer sizes used in DB structs */
/** @{ */
#define DB_STR64   64   /**< 64-byte string field  */
#define DB_STR128  128  /**< 128-byte string field */
#define DB_STR256  256  /**< 256-byte string field */
#define DB_STR512  512  /**< 512-byte string field */
/** @} */

/**
 * @brief Represents a user account stored in the database.
 */
typedef struct
{
    int64_t id;                 /**< Auto-incremented primary key */
    char    username[DB_STR64]; /**< Unique login name */
    char    pwhash[DB_STR128];  /**< Stored as "salt:sha256hex" */
    char    role[16];           /**< "admin" or "user" */
} User;

/**
 * @brief Represents a firmware flash job stored in the database.
 */
typedef struct
{
    int64_t id;                    /**< Auto-incremented primary key */
    char    name[DB_STR128];       /**< Human-readable display name */
    char    hex_file[DB_STR256];   /**< Uploaded filename under uploads/ */
    char    node_id[32];           /**< re-boot -n argument */
    char    interface[16];         /**< "serial", "tcp", or "can" */
    char    device[DB_STR128];     /**< re-boot -i argument (path or IP) */
    char    tcp_port[8];           /**< re-boot -p argument */
    char    retries[4];            /**< re-boot -t argument */
    char    reset_flag[4];         /**< re-boot -r argument */
    char    verbose[4];            /**< re-boot -v argument */
    char    extra_args[DB_STR512]; /**< Appended verbatim to command line */
    char    status[16];            /**< "pending","running","success","failed" */
    char    created_at[32];        /**< ISO-8601 creation timestamp */
    char    started_at[32];        /**< ISO-8601 start timestamp */
    char    finished_at[32];       /**< ISO-8601 finish timestamp */
    int     exit_code;             /**< Subprocess exit code; -1 if not run */
    int64_t user_id;               /**< Foreign key into user.id */
    char    username[DB_STR64];    /**< Denormalised from joined user row */
} Job;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Open (and schema-initialise) the SQLite3 database.
 * @param path File-system path for the SQLite database file.
 * @return 0 on success, -1 on failure.
 */
int  db_open(const char *path);

/**
 * @brief Close the database connection and release resources.
 */
void db_close(void);

/* ------------------------------------------------------------------ */
/* Users                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Look up a user by username.
 * @param username Login name to search.
 * @param out      Struct to fill on success.
 * @return 0 if found, -1 otherwise.
 */
int db_user_by_username(const char *username, User *out);

/**
 * @brief Look up a user by primary-key ID.
 * @param id  Row ID to search.
 * @param out Struct to fill on success.
 * @return 0 if found, -1 otherwise.
 */
int db_user_by_id(int64_t id, User *out);

/**
 * @brief Insert a new user row.
 * @param username Login name (must be unique).
 * @param pwhash   Pre-hashed password string ("salt:sha256hex").
 * @param role     "admin" or "user".
 * @param id_out   Receives the new row ID; may be NULL.
 * @return 0 on success, -1 on failure (e.g. duplicate username).
 */
int db_user_create(const char *username, const char *pwhash,
                   const char *role, int64_t *id_out);

/**
 * @brief Delete a user by ID.
 * @param id Row ID to delete.
 * @return 0 on success, -1 on failure.
 */
int db_user_delete(int64_t id);

/**
 * @brief Fetch all users ordered by ID.
 * @param out Receives a heap-allocated array; caller must free().
 * @param cnt Receives the element count.
 * @return 0 on success, -1 on failure.
 */
int db_user_list(User **out, int *cnt);

/* ------------------------------------------------------------------ */
/* Jobs                                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Insert a new job row.
 * @param j Populated Job struct (the id field is ignored).
 * @return New row ID on success, -1 on failure.
 */
int64_t db_job_create(const Job *j);

/**
 * @brief Fetch a single job by ID (JOINs user table for username).
 * @param id  Row ID to search.
 * @param out Struct to fill on success.
 * @return 0 if found, -1 otherwise.
 */
int db_job_get(int64_t id, Job *out);

/**
 * @brief Fetch the most recent 100 jobs, newest first.
 * @param out Receives a heap-allocated array; caller must free().
 * @param cnt Receives the element count.
 * @return 0 on success, -1 on failure.
 */
int db_job_list(Job **out, int *cnt);

/**
 * @brief Mark a job as running and record its start timestamp.
 * @param id Job row ID.
 * @return 0 on success, -1 on failure.
 */
int db_job_set_started(int64_t id);

/**
 * @brief Mark a job as finished, recording final status and exit code.
 * @param id        Job row ID.
 * @param status    "success" or "failed".
 * @param exit_code Subprocess exit code.
 * @return 0 on success, -1 on failure.
 */
int db_job_set_finished(int64_t id, const char *status, int exit_code);

/**
 * @brief Delete a job row by ID.
 * @param id Row ID to delete.
 * @return 0 on success, -1 on failure.
 */
int db_job_delete(int64_t id);

/* ------------------------------------------------------------------ */
/* Sessions                                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Insert or replace a session record.
 * @param token      64-char hex token (primary key).
 * @param user_id    Owning user ID.
 * @param expires_at Unix timestamp of expiry.
 * @return 0 on success, -1 on failure.
 */
int db_session_create(const char *token, int64_t user_id, int64_t expires_at);

/**
 * @brief Validate a session token and retrieve the owning user ID.
 * @param token   Token to look up.
 * @param now     Current Unix timestamp used for expiry check.
 * @param uid_out Receives the user_id on success.
 * @return 0 if valid and not expired, -1 otherwise.
 */
int db_session_user(const char *token, int64_t now, int64_t *uid_out);

/**
 * @brief Delete a session record (logout).
 * @param token Token to delete.
 * @return 0 on success, -1 on failure.
 */
int db_session_delete(const char *token);
