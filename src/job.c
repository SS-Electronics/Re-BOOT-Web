/**
 * @file job.c
 * @brief Background job runner and SSE log-streaming implementation.
 *
 * @author Subhajit Roy <subhajitroy005@gmail.com>
 * @date   2026-05-06
 */
#include "job.h"
#include "db.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/** Array of all currently active subprocess slots. */
static RunningJob g_jobs[MAX_RUNNING_JOBS];

/** Array of all currently active SSE connection slots. */
static SseConn    g_sse[MAX_SSE_CONNS];

/** Mutex protecting both g_jobs[] and g_sse[]. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Find the active RunningJob slot for @p job_id.
 * @param job_id Database job ID.
 * @return Pointer to the slot, or NULL if not found.
 */
static RunningJob *find_job_slot(int64_t job_id)
{
    for (int i = 0; i < MAX_RUNNING_JOBS; i++)
    {
        if (g_jobs[i].active && g_jobs[i].job_id == job_id)
        {
            return &g_jobs[i];
        }
    }
    return NULL;
}

/**
 * @brief Find a free RunningJob slot.
 * @return Pointer to an inactive slot, or NULL if all slots are full.
 */
static RunningJob *alloc_job_slot(void)
{
    for (int i = 0; i < MAX_RUNNING_JOBS; i++)
    {
        if (!g_jobs[i].active)
        {
            return &g_jobs[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Job runner thread                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Arguments passed to runner_thread() at creation.
 */
typedef struct
{
    int64_t job_id;        /**< Database job ID */
    char    binary[256];   /**< Path to re-boot executable */
    char    upload_dir[256]; /**< Directory containing uploaded hex files */
    char    log_dir[256];  /**< Directory for per-job log files */
} ThreadArg;

/**
 * @brief Thread entry point: build command, fork/exec re-boot, stream output.
 *
 * Owns the heap-allocated @p arg and frees it before doing any blocking
 * work.  Updates the database on start and finish, and signals SSE
 * connections when the job is complete.
 *
 * @param arg Heap-allocated ThreadArg; freed inside this function.
 * @return Always NULL.
 */
static void *runner_thread(void *arg)
{
    ThreadArg *ta = arg;
    int64_t    job_id = ta->job_id;
    char       binary[256], upload_dir[256], log_dir[256];

    snprintf(binary,     sizeof(binary),     "%s", ta->binary);
    snprintf(upload_dir, sizeof(upload_dir), "%s", ta->upload_dir);
    snprintf(log_dir,    sizeof(log_dir),    "%s", ta->log_dir);
    free(ta);

    /* Fetch job configuration from the database */
    Job j;
    if (db_job_get(job_id, &j) != 0)
    {
        return NULL;
    }

    /* Build argv for re-boot */
    char hex_path[512];
    snprintf(hex_path, sizeof(hex_path), "%s/%s", upload_dir, j.hex_file);

    char *argv[32];
    int   argc = 0;
    argv[argc++] = binary;
    argv[argc++] = "-f"; argv[argc++] = hex_path;
    argv[argc++] = "-n"; argv[argc++] = j.node_id;
    argv[argc++] = "-c"; argv[argc++] = j.interface;
    argv[argc++] = "-i"; argv[argc++] = j.device;

    if (j.tcp_port[0])   { argv[argc++] = "-p"; argv[argc++] = j.tcp_port;   }
    if (j.retries[0])    { argv[argc++] = "-t"; argv[argc++] = j.retries;    }
    if (j.reset_flag[0]) { argv[argc++] = "-r"; argv[argc++] = j.reset_flag; }
    if (j.verbose[0])    { argv[argc++] = "-v"; argv[argc++] = j.verbose;    }

    /* Append extra_args split on spaces (no quoting support) */
    char extra_buf[DB_STR512];
    if (j.extra_args[0])
    {
        snprintf(extra_buf, sizeof(extra_buf), "%s", j.extra_args);
        char *tok = strtok(extra_buf, " ");
        while (tok && argc < 30)
        {
            argv[argc++] = tok;
            tok = strtok(NULL, " ");
        }
    }
    argv[argc] = NULL;

    /* Open log file; log the command line first */
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/job_%lld.log",
             log_dir, (long long)job_id);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (log_fd >= 0)
    {
        dprintf(log_fd, "CMD:");
        for (int i = 0; i < argc; i++)
        {
            dprintf(log_fd, " %s", argv[i]);
        }
        dprintf(log_fd, "\n\n");
    }

    /* Fork the subprocess, piping its stdout+stderr into the log */
    int pipefd[2];
    if (pipe(pipefd) != 0)
    {
        db_job_set_finished(job_id, "failed", -1);
        return NULL;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        /* Child: redirect stdout/stderr into the write end of the pipe */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(binary, argv);
        _exit(127);
    }

    /* Parent: register the running slot, then relay output to the log */
    close(pipefd[1]);

    pthread_mutex_lock(&g_lock);
    RunningJob *slot = alloc_job_slot();
    if (slot)
    {
        slot->job_id = job_id;
        slot->pid    = pid;
        slot->active = 1;
    }
    pthread_mutex_unlock(&g_lock);

    db_job_set_started(job_id);

    char    buf[1024];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
    {
        if (log_fd >= 0)
        {
            if (write(log_fd, buf, (size_t)n) != n) { /* best-effort */ }
        }
    }
    close(pipefd[0]);
    if (log_fd >= 0)
    {
        close(log_fd);
    }

    /* Reap child and record final status */
    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    int         exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
    const char *status    = (exit_code == 0) ? "success" : "failed";

    pthread_mutex_lock(&g_lock);
    RunningJob *rj = find_job_slot(job_id);
    if (rj)
    {
        rj->active = 0;
    }
    pthread_mutex_unlock(&g_lock);

    db_job_set_finished(job_id, status, exit_code);

    /* Signal SSE connections that the stream has ended */
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (g_sse[i].active && g_sse[i].job_id == job_id)
        {
            g_sse[i].active = 2; /* 2 = flush remaining log then close */
        }
    }
    pthread_mutex_unlock(&g_lock);

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int job_run(int64_t job_id, const char *binary,
            const char *upload_dir, const char *log_dir)
{
    pthread_mutex_lock(&g_lock);
    if (find_job_slot(job_id))
    {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    pthread_mutex_unlock(&g_lock);

    ThreadArg *ta = malloc(sizeof(ThreadArg));
    ta->job_id = job_id;
    strncpy(ta->binary,     binary,     sizeof(ta->binary) - 1);
    strncpy(ta->upload_dir, upload_dir, sizeof(ta->upload_dir) - 1);
    strncpy(ta->log_dir,    log_dir,    sizeof(ta->log_dir) - 1);

    pthread_t      tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&tid, &attr, runner_thread, ta) != 0)
    {
        free(ta);
        pthread_attr_destroy(&attr);
        return -1;
    }

    pthread_attr_destroy(&attr);
    return 0;
}

int job_stop(int64_t job_id)
{
    pthread_mutex_lock(&g_lock);
    RunningJob *rj = find_job_slot(job_id);
    if (rj)
    {
        kill(rj->pid, SIGTERM);
    }
    pthread_mutex_unlock(&g_lock);
    return rj ? 0 : -1;
}

int job_is_running(int64_t job_id)
{
    pthread_mutex_lock(&g_lock);
    RunningJob *rj = find_job_slot(job_id);
    int r = (rj != NULL);
    pthread_mutex_unlock(&g_lock);
    return r;
}

void job_sse_add(int64_t job_id, struct mg_connection *conn)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (!g_sse[i].active)
        {
            g_sse[i].job_id     = job_id;
            g_sse[i].conn       = conn;
            g_sse[i].log_offset = 0;
            g_sse[i].active     = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void job_sse_poll(void)
{
    /* g_log_dir is defined in routes.c and declared extern here */
    extern char g_log_dir[256];

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        SseConn *sc = &g_sse[i];
        if (!sc->active)
        {
            continue;
        }

        /* Tail the log file from the offset of the last byte sent */
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/job_%lld.log",
                 g_log_dir, (long long)sc->job_id);

        FILE *f = fopen(log_path, "r");
        if (f)
        {
            fseek(f, sc->log_offset, SEEK_SET);
            char line[1024];
            while (fgets(line, sizeof(line), f))
            {
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n')
                {
                    line[--len] = '\0';
                }
                mg_printf(sc->conn, "data: %s\n\n", line);
            }
            sc->log_offset = ftell(f);
            fclose(f);
        }

        if (sc->active == 2)
        {
            /* Job finished — flush any remaining output then close */
            mg_printf(sc->conn, "data: [STREAM END]\n\n");
            sc->conn->is_draining = 1;
            sc->active = 0;
        }
    }
    pthread_mutex_unlock(&g_lock);
}
