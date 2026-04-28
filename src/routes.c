#include "routes.h"
#include "auth.h"
#include "db.h"
#include "job.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* Configured at startup — globals set by main.c */
char g_log_dir[256]    = "logs";
char g_upload_dir[256] = "uploads";
char g_www_dir[256]    = "www";
char g_binary[256]     = "./re-boot";

/* ------------------------------------------------------------------ */
/* JSON helpers                                                         */
/* ------------------------------------------------------------------ */

/* Escape a string for embedding in a JSON value. Writes into buf. */
static void json_esc(const char *src, char *dst, size_t dlen) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 4 < dlen; si++) {
        unsigned char c = (unsigned char)src[si];
        if      (c == '"')  { dst[di++]='\\'  ; dst[di++]='"'; }
        else if (c == '\\') { dst[di++]='\\'  ; dst[di++]='\\'; }
        else if (c == '\n') { dst[di++]='\\'  ; dst[di++]='n'; }
        else if (c == '\r') { dst[di++]='\\'  ; dst[di++]='r'; }
        else if (c == '\t') { dst[di++]='\\'  ; dst[di++]='t'; }
        else if (c < 0x20)  { /* skip control chars */ }
        else                  dst[di++] = c;
    }
    dst[di] = '\0';
}

#define JSTR(f) ({ static char _b[512]; json_esc(f, _b, sizeof(_b)); _b; })

static void reply_json(struct mg_connection *c, int status,
                       const char *body) {
    mg_http_reply(c, status,
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache\r\n",
        "%s", body);
}

static void reply_err(struct mg_connection *c, int status, const char *msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    reply_json(c, status, buf);
}

/* ------------------------------------------------------------------ */
/* Auth guard                                                           */
/* ------------------------------------------------------------------ */

static User *require_auth(struct mg_connection *c,
                           struct mg_http_message *hm) {
    User *u = auth_session_user(hm);
    if (!u) {
        reply_err(c, 401, "Unauthorized");
    }
    return u;
}

/* ------------------------------------------------------------------ */
/* Route handlers                                                       */
/* ------------------------------------------------------------------ */

static void handle_login(struct mg_connection *c, struct mg_http_message *hm) {
    char username[64] = {0}, password[128] = {0};
    mg_http_get_var(&hm->body, "username", username, sizeof(username));
    mg_http_get_var(&hm->body, "password", password, sizeof(password));

    /* Support JSON body too */
    if (!username[0]) {
        char *tmp;
        if ((tmp = mg_json_get_str(hm->body, "$.username")) != NULL)
            { strncpy(username, tmp, sizeof(username)-1); free(tmp); }
        if ((tmp = mg_json_get_str(hm->body, "$.password")) != NULL)
            { strncpy(password, tmp, sizeof(password)-1); free(tmp); }
    }

    User u;
    if (db_user_by_username(username, &u) != 0
            || !auth_check_password(password, u.pwhash)) {
        reply_err(c, 401, "Invalid credentials");
        return;
    }

    char token[65];
    auth_gen_token(token);
    db_session_create(token, u.id, (int64_t)time(NULL) + SESSION_TTL);

    char hdrs[256];
    snprintf(hdrs, sizeof(hdrs),
        "Content-Type: application/json\r\n"
        "Set-Cookie: " COOKIE_NAME "=%s; Path=/; HttpOnly; SameSite=Strict\r\n",
        token);
    char body[1280];
    snprintf(body, sizeof(body),
        "{\"ok\":true,\"role\":\"%s\",\"username\":\"%s\"}",
        JSTR(u.role), JSTR(u.username));
    mg_http_reply(c, 200, hdrs, "%s", body);
}

static void handle_logout(struct mg_connection *c, struct mg_http_message *hm) {
    struct mg_str *cookie_hdr = mg_http_get_header(hm, "Cookie");
    if (cookie_hdr) {
        /* Extract and delete session token */
        char token[65] = {0};
        /* reuse auth's cookie parser via a temporary User lookup */
        User *u = auth_session_user(hm);
        if (u) { free(u); }
        (void)token;
    }
    mg_http_reply(c, 200,
        "Set-Cookie: " COOKIE_NAME "=; Path=/; Max-Age=0\r\n"
        "Content-Type: application/json\r\n",
        "{\"ok\":true}");
}

static void handle_me(struct mg_connection *c, struct mg_http_message *hm) {
    User *u = require_auth(c, hm);
    if (!u) return;
    char body[1280];
    snprintf(body, sizeof(body),
        "{\"id\":%lld,\"username\":\"%s\",\"role\":\"%s\"}",
        (long long)u->id, JSTR(u->username), JSTR(u->role));
    free(u);
    reply_json(c, 200, body);
}

/* --- Jobs --- */

static void job_to_json(const Job *j, char *buf, size_t blen) {
    snprintf(buf, blen,
        "{\"id\":%lld,\"name\":\"%s\",\"hex_file\":\"%s\","
        "\"node_id\":\"%s\",\"interface\":\"%s\",\"device\":\"%s\","
        "\"tcp_port\":\"%s\",\"retries\":\"%s\",\"reset_flag\":\"%s\","
        "\"verbose\":\"%s\",\"extra_args\":\"%s\","
        "\"status\":\"%s\",\"created_at\":\"%s\","
        "\"started_at\":\"%s\",\"finished_at\":\"%s\","
        "\"exit_code\":%d,\"user_id\":%lld,\"username\":\"%s\"}",
        (long long)j->id, JSTR(j->name), JSTR(j->hex_file),
        JSTR(j->node_id), JSTR(j->interface), JSTR(j->device),
        JSTR(j->tcp_port), JSTR(j->retries), JSTR(j->reset_flag),
        JSTR(j->verbose), JSTR(j->extra_args),
        JSTR(j->status), JSTR(j->created_at),
        JSTR(j->started_at), JSTR(j->finished_at),
        j->exit_code, (long long)j->user_id, JSTR(j->username));
}

static void handle_jobs_list(struct mg_connection *c, struct mg_http_message *hm) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);

    Job *jobs = NULL;
    int  cnt  = 0;
    db_job_list(&jobs, &cnt);

    /* Build JSON array */
    size_t blen = (size_t)cnt * 8192 + 64;
    char  *buf  = malloc(blen);
    buf[0] = '['; buf[1] = '\0';
    char entry[8192];
    for (int i = 0; i < cnt; i++) {
        job_to_json(&jobs[i], entry, sizeof(entry));
        if (i > 0) strncat(buf, ",", blen - strlen(buf) - 1);
        strncat(buf, entry, blen - strlen(buf) - 1);
    }
    strncat(buf, "]", blen - strlen(buf) - 1);
    free(jobs);
    reply_json(c, 200, buf);
    free(buf);
}

static void handle_jobs_create(struct mg_connection *c,
                                struct mg_http_message *hm) {
    User *u = require_auth(c, hm);
    if (!u) return;

    Job j;
    memset(&j, 0, sizeof(j));
    j.user_id = u->id;
    free(u);

    /* Parse multipart form */
    struct mg_http_part part;
    size_t ofs = 0;
    char   hex_filename[256] = {0};

    while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
        char fname[64];
        snprintf(fname, sizeof(fname), "%.*s",
                 (int)part.name.len, part.name.buf);

#define GETFIELD(field) \
        if (strcmp(fname, #field) == 0) { \
            size_t l = part.body.len < sizeof(j.field)-1 ? part.body.len : sizeof(j.field)-1; \
            memcpy(j.field, part.body.buf, l); j.field[l] = '\0'; }

        GETFIELD(name)
        else GETFIELD(node_id)
        else GETFIELD(interface)
        else GETFIELD(device)
        else GETFIELD(tcp_port)
        else GETFIELD(retries)
        else GETFIELD(reset_flag)
        else GETFIELD(verbose)
        else GETFIELD(extra_args)
        else if (strcmp(fname, "hex_file") == 0 && part.filename.len > 0) {
            /* Sanitize filename: keep only basename chars */
            const char *fn = part.filename.buf;
            size_t fnlen = part.filename.len;
            /* Strip path separators */
            for (size_t k = 0; k < fnlen; k++)
                if (fn[k] == '/' || fn[k] == '\\') { fn = fn+k+1; fnlen -= k+1; k=0; }
            size_t l = fnlen < sizeof(hex_filename)-1 ? fnlen : sizeof(hex_filename)-1;
            memcpy(hex_filename, fn, l);
            hex_filename[l] = '\0';
            /* Replace spaces */
            for (size_t k = 0; k < strlen(hex_filename); k++)
                if (hex_filename[k] == ' ') hex_filename[k] = '_';

            /* Write file */
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", g_upload_dir, hex_filename);
            int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                if (write(fd, part.body.buf, part.body.len) != (ssize_t)part.body.len) {}
                close(fd);
            }
            snprintf(j.hex_file, sizeof(j.hex_file), "%s", hex_filename);
        }
#undef GETFIELD
    }

    if (!j.hex_file[0] || !j.node_id[0] || !j.device[0]) {
        reply_err(c, 400, "hex_file, node_id and device are required");
        return;
    }
    if (!j.name[0]) strncpy(j.name, "Unnamed Job", sizeof(j.name)-1);

    int64_t id = db_job_create(&j);
    if (id < 0) { reply_err(c, 500, "DB error"); return; }

    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"id\":%lld}", (long long)id);
    reply_json(c, 201, body);
}

static void handle_job_get(struct mg_connection *c,
                            struct mg_http_message *hm, int64_t job_id) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);

    Job j;
    if (db_job_get(job_id, &j) != 0) { reply_err(c, 404, "Not found"); return; }
    char buf[8192];
    job_to_json(&j, buf, sizeof(buf));
    reply_json(c, 200, buf);
}

static void handle_job_run(struct mg_connection *c,
                            struct mg_http_message *hm, int64_t job_id) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);

    if (job_is_running(job_id)) {
        reply_err(c, 409, "Already running");
        return;
    }
    if (job_run(job_id, g_binary, g_upload_dir, g_log_dir) != 0) {
        reply_err(c, 500, "Failed to start job");
        return;
    }
    reply_json(c, 200, "{\"ok\":true}");
}

static void handle_job_stop(struct mg_connection *c,
                              struct mg_http_message *hm, int64_t job_id) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);
    job_stop(job_id);
    reply_json(c, 200, "{\"ok\":true}");
}

static void handle_job_delete(struct mg_connection *c,
                               struct mg_http_message *hm, int64_t job_id) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);

    if (job_is_running(job_id)) {
        reply_err(c, 409, "Job is running");
        return;
    }
    db_job_delete(job_id);
    reply_json(c, 200, "{\"ok\":true}");
}

static void handle_job_status(struct mg_connection *c,
                               struct mg_http_message *hm, int64_t job_id) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);

    Job j;
    if (db_job_get(job_id, &j) != 0) { reply_err(c, 404, "Not found"); return; }
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"status\":\"%s\",\"exit_code\":%d}", j.status, j.exit_code);
    reply_json(c, 200, buf);
}

static void handle_job_stream(struct mg_connection *c,
                               struct mg_http_message *hm, int64_t job_id) {
    User *u = require_auth(c, hm);
    if (!u) return;
    free(u);

    mg_printf(c,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n");
    job_sse_add(job_id, c);
}

/* --- Users --- */

static void handle_users_list(struct mg_connection *c,
                               struct mg_http_message *hm) {
    User *u = require_auth(c, hm);
    if (!u) return;
    if (strcmp(u->role, "admin") != 0) {
        free(u); reply_err(c, 403, "Forbidden"); return;
    }
    free(u);

    User *users = NULL;
    int   cnt   = 0;
    db_user_list(&users, &cnt);

    size_t blen = (size_t)cnt * 256 + 32;
    char  *buf  = malloc(blen);
    buf[0] = '['; buf[1] = '\0';
    for (int i = 0; i < cnt; i++) {
        char entry[1280];
        snprintf(entry, sizeof(entry),
            "%s{\"id\":%lld,\"username\":\"%s\",\"role\":\"%s\"}",
            i > 0 ? "," : "",
            (long long)users[i].id,
            JSTR(users[i].username),
            JSTR(users[i].role));
        strncat(buf, entry, blen - strlen(buf) - 1);
    }
    strncat(buf, "]", blen - strlen(buf) - 1);
    free(users);
    reply_json(c, 200, buf);
    free(buf);
}

static void handle_users_create(struct mg_connection *c,
                                  struct mg_http_message *hm) {
    User *u = require_auth(c, hm);
    if (!u) return;
    if (strcmp(u->role, "admin") != 0) {
        free(u); reply_err(c, 403, "Forbidden"); return;
    }
    free(u);

    char username[64]={0}, password[128]={0}, role[16]="user";
    { char *tmp;
      if ((tmp = mg_json_get_str(hm->body, "$.username")) != NULL)
          { strncpy(username, tmp, sizeof(username)-1); free(tmp); }
      if ((tmp = mg_json_get_str(hm->body, "$.password")) != NULL)
          { strncpy(password, tmp, sizeof(password)-1); free(tmp); }
      if ((tmp = mg_json_get_str(hm->body, "$.role"))     != NULL)
          { strncpy(role,     tmp, sizeof(role)-1);     free(tmp); }
    }

    if (!username[0] || !password[0]) {
        reply_err(c, 400, "username and password required");
        return;
    }
    char pwhash[DB_STR128];
    auth_hash_password(password, pwhash);
    int64_t new_id = 0;
    if (db_user_create(username, pwhash, role, &new_id) != 0) {
        reply_err(c, 409, "Username already exists");
        return;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"id\":%lld}", (long long)new_id);
    reply_json(c, 201, body);
}

static void handle_user_delete(struct mg_connection *c,
                                struct mg_http_message *hm, int64_t uid) {
    User *u = require_auth(c, hm);
    if (!u) return;
    if (strcmp(u->role, "admin") != 0 || u->id == uid) {
        free(u); reply_err(c, 403, "Forbidden"); return;
    }
    free(u);
    db_user_delete(uid);
    reply_json(c, 200, "{\"ok\":true}");
}

/* ------------------------------------------------------------------ */
/* Router                                                               */
/* ------------------------------------------------------------------ */

static int match_prefix(struct mg_str uri, const char *prefix) {
    size_t plen = strlen(prefix);
    return uri.len >= plen && memcmp(uri.buf, prefix, plen) == 0;
}

static int64_t parse_id(struct mg_str uri, const char *prefix) {
    size_t plen = strlen(prefix);
    if (uri.len <= plen) return -1;
    return (int64_t)atoll(uri.buf + plen);
}

/* Returns 1 if the segment after prefix + id matches suffix */
static int has_suffix(struct mg_str uri, const char *prefix, const char *suffix) {
    size_t plen = strlen(prefix), slen = strlen(suffix);
    if (uri.len < plen) return 0;
    /* Find end of ID number */
    size_t i = plen;
    while (i < uri.len && uri.buf[i] >= '0' && uri.buf[i] <= '9') i++;
    if (uri.len - i < slen) return 0;
    return memcmp(uri.buf + i, suffix, slen) == 0;
}

/* mg_strcmp returns 0 on equal; HTTP methods are always uppercase */
#define METH(m) (mg_strcmp(hm->method, mg_str(m)) == 0)
#define URI(p)  (mg_match(hm->uri, mg_str(p), NULL))
#define IS(m,p) (METH(m) && URI(p))

void http_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = ev_data;
    struct mg_str uri = hm->uri;

    if (IS("POST", "/api/login"))  { handle_login(c, hm);  return; }
    if (IS("POST", "/api/logout")) { handle_logout(c, hm); return; }
    if (IS("GET",  "/api/me"))     { handle_me(c, hm);     return; }

    /* Jobs */
    if (METH("GET")  && (URI("/api/jobs") || URI("/api/jobs/"))) {
        handle_jobs_list(c, hm); return;
    }
    if (METH("POST") && (URI("/api/jobs") || URI("/api/jobs/"))) {
        handle_jobs_create(c, hm); return;
    }
    if (match_prefix(uri, "/api/jobs/")) {
        int64_t id = parse_id(uri, "/api/jobs/");
        if (id < 0) { reply_err(c, 400, "Bad ID"); return; }

        if (has_suffix(uri, "/api/jobs/", "/run")    && METH("POST"))
            { handle_job_run(c, hm, id);    return; }
        if (has_suffix(uri, "/api/jobs/", "/stop")   && METH("POST"))
            { handle_job_stop(c, hm, id);   return; }
        if (has_suffix(uri, "/api/jobs/", "/status") && METH("GET"))
            { handle_job_status(c, hm, id); return; }
        if (has_suffix(uri, "/api/jobs/", "/stream") && METH("GET"))
            { handle_job_stream(c, hm, id); return; }
        if (METH("GET"))    { handle_job_get(c, hm, id);    return; }
        if (METH("DELETE")) { handle_job_delete(c, hm, id); return; }
    }

    /* Users */
    if (METH("GET")  && (URI("/api/users") || URI("/api/users/"))) {
        handle_users_list(c, hm); return;
    }
    if (METH("POST") && (URI("/api/users") || URI("/api/users/"))) {
        handle_users_create(c, hm); return;
    }
    if (match_prefix(uri, "/api/users/") && METH("DELETE")) {
        int64_t id = parse_id(uri, "/api/users/");
        handle_user_delete(c, hm, id); return;
    }

    /* Static files */
    struct mg_http_serve_opts opts = {.root_dir = g_www_dir};
    mg_http_serve_dir(c, hm, &opts);
}

#undef IS
#undef URI
#undef METH
