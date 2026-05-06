/**
 * @file job.h
 * @brief Background job runner and Server-Sent Events (SSE) log streamer.
 *
 * Each firmware flash job runs as a detached POSIX thread that fork/exec's
 * the re-boot binary, captures its stdout/stderr into a per-job log file,
 * and updates the database on start and finish.  SSE connections registered
 * via job_sse_add() receive new log lines every 300 ms via job_sse_poll().
 *
 * @author Subhajit Roy <subhajitroy005@gmail.com>
 * @date   2026-05-06
 */
#pragma once
#include <stdint.h>
#include "../mongoose/mongoose.h"

#define MAX_RUNNING_JOBS  32  /**< Maximum concurrent firmware flash jobs */
#define MAX_SSE_CONNS     64  /**< Maximum concurrent SSE log-tail connections */

/**
 * @brief Tracks an in-progress subprocess spawned for a job.
 */
typedef struct
{
    int64_t job_id; /**< Database job ID this slot belongs to */
    pid_t   pid;    /**< OS process ID of the re-boot subprocess */
    int     active; /**< 1 while subprocess is alive, 0 when done */
} RunningJob;

/**
 * @brief Tracks a client connection receiving live log output via SSE.
 */
typedef struct
{
    int64_t              job_id;     /**< Job whose log is being tailed */
    struct mg_connection *conn;      /**< Mongoose connection to write SSE into */
    long                 log_offset; /**< File offset of last byte already sent */
    int                  active;     /**< 1=streaming, 2=flush-and-close, 0=free */
} SseConn;

/**
 * @brief Start a job in a detached background thread.
 *
 * The thread builds the re-boot command line from the job's database record,
 * opens a log file at @p log_dir/job_ID.log, then fork/exec's the binary.
 * Subprocess stdout and stderr are piped into the log file.  The database
 * is updated to "running" on start and "success"/"failed" on finish.
 *
 * @param job_id     Database job ID.
 * @param binary     Path to the re-boot executable.
 * @param upload_dir Directory where uploaded hex files are stored.
 * @param log_dir    Directory where per-job log files are written.
 * @return 0 on success, -1 if the job is already running or thread fails.
 */
int job_run(int64_t job_id, const char *binary,
            const char *upload_dir, const char *log_dir);

/**
 * @brief Send SIGTERM to the subprocess of a running job.
 * @param job_id Database job ID.
 * @return 0 if the signal was delivered, -1 if the job is not running.
 */
int job_stop(int64_t job_id);

/**
 * @brief Check whether a job's subprocess is currently alive.
 * @param job_id Database job ID.
 * @return 1 if running, 0 if not.
 */
int job_is_running(int64_t job_id);

/**
 * @brief Register an SSE connection to stream a job's log output.
 * @param job_id Database job ID.
 * @param conn   Mongoose connection to write SSE events into.
 */
void job_sse_add(int64_t job_id, struct mg_connection *conn);

/**
 * @brief Poll all active SSE connections and push any new log lines.
 *
 * Called from the Mongoose timer callback (300 ms interval).  Tails each
 * job's log file from the last known offset and sends new lines as SSE
 * "data:" frames.  Closes connections whose job has finished.
 *
 * Must be called from the single-threaded Mongoose event loop.
 */
void job_sse_poll(void);
