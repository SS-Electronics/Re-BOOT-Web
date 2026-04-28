#pragma once
#include <stdint.h>
#include "../mongoose/mongoose.h"

#define MAX_RUNNING_JOBS 32
#define MAX_SSE_CONNS    64

typedef struct {
    int64_t          job_id;
    pid_t            pid;
    int              active; /* 1 while subprocess is alive */
} RunningJob;

typedef struct {
    int64_t          job_id;
    struct mg_connection *conn;
    long             log_offset; /* bytes already sent */
    int              active;
} SseConn;

/* Start re-boot in a background thread; updates DB on start/finish */
int  job_run(int64_t job_id, const char *binary,
             const char *upload_dir, const char *log_dir);

/* Kill the subprocess for a job */
int  job_stop(int64_t job_id);

/* Return 1 if job is currently running */
int  job_is_running(int64_t job_id);

/* Register an SSE connection to tail a job's log */
void job_sse_add(int64_t job_id, struct mg_connection *conn);

/* Called from Mongoose timer — pushes new log lines to SSE connections */
void job_sse_poll(void);
