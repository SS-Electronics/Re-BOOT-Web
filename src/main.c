#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../mongoose/mongoose.h"
#include "auth.h"
#include "db.h"
#include "job.h"
#include "routes.h"
#include "sha256.h"

/* Defined in routes.c */
extern char g_log_dir[256];
extern char g_upload_dir[256];
extern char g_www_dir[256];
extern char g_binary[256];

static volatile int g_running = 1;

static void sig_handler(int sig) { (void)sig; g_running = 0; }

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -l <addr>     Listen address (default: 0.0.0.0:5000)\n"
        "  -b <path>     Path to re-boot binary (default: ./re-boot)\n"
        "  -d <path>     Database file       (default: reboot.db)\n"
        "  -u <dir>      Upload directory    (default: uploads)\n"
        "  -L <dir>      Log directory       (default: logs)\n"
        "  -w <dir>      Static www dir      (default: www)\n"
        "  -h            Show this help\n",
        prog);
}

static void mkdirp(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) mkdir(path, 0755);
}

static void seed_admin(const char *username, const char *password) {
    User u;
    if (db_user_by_username(username, &u) == 0) return; /* already exists */
    char pwhash[DB_STR128];
    auth_hash_password(password, pwhash);
    int64_t id;
    db_user_create(username, pwhash, "admin", &id);
    printf("[init] Created default admin user '%s'\n", username);
}

static void sse_timer_cb(void *arg) {
    (void)arg;
    job_sse_poll();
}

int main(int argc, char *argv[]) {
    char listen[64]  = "0.0.0.0:5000";
    char db_path[256] = "reboot.db";

    /* Defaults are already in route globals */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l") && i+1 < argc) strncpy(listen,     argv[++i], sizeof(listen)-1);
        else if (!strcmp(argv[i], "-b") && i+1 < argc) strncpy(g_binary,    argv[++i], sizeof(g_binary)-1);
        else if (!strcmp(argv[i], "-d") && i+1 < argc) strncpy(db_path,     argv[++i], sizeof(db_path)-1);
        else if (!strcmp(argv[i], "-u") && i+1 < argc) strncpy(g_upload_dir,argv[++i], sizeof(g_upload_dir)-1);
        else if (!strcmp(argv[i], "-L") && i+1 < argc) strncpy(g_log_dir,   argv[++i], sizeof(g_log_dir)-1);
        else if (!strcmp(argv[i], "-w") && i+1 < argc) strncpy(g_www_dir,   argv[++i], sizeof(g_www_dir)-1);
        else if (!strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    mkdirp(g_upload_dir);
    mkdirp(g_log_dir);

    if (db_open(db_path) != 0) {
        fprintf(stderr, "Failed to open database: %s\n", db_path);
        return 1;
    }
    seed_admin("admin", "admin");

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    if (!mg_http_listen(&mgr, listen, http_handler, NULL)) {
        fprintf(stderr, "Failed to listen on %s\n", listen);
        db_close();
        return 1;
    }

    /* SSE poll timer every 300ms */
    mg_timer_add(&mgr, 300, MG_TIMER_REPEAT, sse_timer_cb, NULL);

    printf("\n");
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║       Re-BOOT Web Server             ║\n");
    printf("  ║                                      ║\n");
    printf("  ║  Listening : %-22s  ║\n", listen);
    printf("  ║  Binary    : %-22s  ║\n", g_binary);
    printf("  ║  Database  : %-22s  ║\n", db_path);
    printf("  ║  Default   : admin / admin           ║\n");
    printf("  ╚══════════════════════════════════════╝\n\n");

    while (g_running) mg_mgr_poll(&mgr, 50);

    printf("\n[server] Shutting down...\n");
    mg_mgr_free(&mgr);
    db_close();
    return 0;
}
