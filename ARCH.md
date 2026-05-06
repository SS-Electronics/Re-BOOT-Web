# Re-BOOT Web — Architectural Description

**Author:** Subhajit Roy <subhajitroy005@gmail.com>
**Date:** 2026-05-06

---

## 1. Overview

Re-BOOT Web is a single-binary C web server that provides a browser-based
OTA (Over-The-Air) firmware flashing portal for embedded targets supported
by the [Re-BOOT](https://github.com/SS-Electronics/Re-BOOT) command-line
flasher utility.

The server runs on any Linux host (designed for Raspberry Pi) and exposes:

- A REST JSON API for job and user management
- A Server-Sent Events (SSE) endpoint for real-time log streaming
- A static-file directory serving the HTML/CSS/JS frontend

There are no runtime dependencies beyond `libsqlite3` and `libpthread`.
The HTTP server is the [Mongoose](https://github.com/cesanta/mongoose)
single-file embedded C library, downloaded at build time.

---

## 2. Top-Level Directory Layout

```
Re-BOOT-Web/
├── src/          C source files (.c)
├── inc/          C header files (.h)
├── mongoose/     Mongoose HTTP library (auto-downloaded at build time)
├── www/          Frontend static assets (HTML, CSS, JS)
├── uploads/      Uploaded .hex firmware files (created at runtime)
├── logs/         Per-job subprocess output logs (created at runtime)
├── Makefile
├── install_deps.sh
├── README.md
└── ARCH.md       (this file)
```

---

## 3. Module Map

| Module | Source | Header | Responsibility |
|--------|--------|--------|----------------|
| **main** | `src/main.c` | — | Entry point, CLI parsing, Mongoose event loop |
| **db** | `src/db.c` | `inc/db.h` | SQLite3 wrapper: users, jobs, sessions |
| **auth** | `src/auth.c` | `inc/auth.h` | Password hashing, session token generation and validation |
| **job** | `src/job.c` | `inc/job.h` | Background subprocess runner, SSE log streaming |
| **routes** | `src/routes.c` | `inc/routes.h` | HTTP request dispatcher and REST API handlers |
| **sha256** | `src/sha256.c` | `inc/sha256.h` | Public-domain SHA-256 implementation |
| **mongoose** | `mongoose/mongoose.c` | `mongoose/mongoose.h` | Embedded HTTP server (third-party) |

---

## 4. Component Architecture

```
┌─────────────────────────────────────────────────────┐
│                    Browser / Nginx                   │
│          (HTML/CSS/JS served from www/)              │
└────────────────────────┬────────────────────────────┘
                         │ HTTP / SSE
                         ▼
┌─────────────────────────────────────────────────────┐
│               Mongoose Event Loop (main.c)           │
│                                                     │
│   mg_mgr_poll() ◄─── Timer (300ms SSE poll)        │
└────────────────────────┬────────────────────────────┘
                         │ MG_EV_HTTP_MSG
                         ▼
┌─────────────────────────────────────────────────────┐
│                   routes.c                           │
│   http_handler()                                    │
│   ├── /api/login, /api/logout, /api/me   → auth.c  │
│   ├── /api/jobs/*                        → job.c   │
│   ├── /api/users/*                       → db.c    │
│   └── /* (static files)                 → www/     │
└──────┬──────────────┬──────────────────┬────────────┘
       │              │                  │
       ▼              ▼                  ▼
   auth.c           job.c             db.c
   sha256.c      (threads)          sqlite3
```

---

## 5. Data Flow

### 5.1 Login

```
Browser  →  POST /api/login  →  routes.c : handle_login()
                                    │
                                    ├─ db_user_by_username()  →  db.c
                                    ├─ auth_check_password()  →  auth.c → sha256.c
                                    ├─ auth_gen_token()       →  auth.c → /dev/urandom
                                    └─ db_session_create()    →  db.c
                                         │
                                    Set-Cookie: rbsid=<64-char-hex>
```

### 5.2 Authenticated Request

```
Browser  →  GET /api/jobs  + Cookie: rbsid=<token>
                │
                └→  routes.c : require_auth()
                        │
                        └→  auth_session_user()  →  auth.c
                                │
                                ├─ parse Cookie header (get_cookie)
                                ├─ db_session_user()  →  db.c  (token + expiry check)
                                └─ db_user_by_id()   →  db.c  (load User struct)
                                       │
                                   User* (heap-allocated, caller frees)
```

### 5.3 Job Creation and Execution

```
Browser  →  POST /api/jobs  (multipart/form-data)
                │
                └→  routes.c : handle_jobs_create()
                        │
                        ├─ mg_http_next_multipart()  (parse form fields)
                        ├─ Save uploaded .hex file to uploads/<timestamp>_<name>.hex
                        └─ db_job_create()  →  db.c  → INSERT INTO job ...
                               │
                               id (int64_t)

Browser  →  POST /api/jobs/:id/run
                │
                └→  routes.c : handle_job_run()
                        │
                        └─ job_run(id, binary, upload_dir, log_dir)  →  job.c
                               │
                               └─ pthread_create(runner_thread)
                                      │
                                      ├─ db_job_get()
                                      ├─ build argv[]
                                      ├─ open log file  logs/job_<id>.log
                                      ├─ pipe() + fork() + execvp()
                                      ├─ db_job_set_started()
                                      ├─ relay pipe → log file
                                      ├─ waitpid()
                                      ├─ db_job_set_finished()
                                      └─ signal SSE connections (active = 2)
```

### 5.4 SSE Log Streaming

```
Browser  →  GET /api/jobs/:id/stream
                │
                └→  routes.c : handle_job_stream()
                        │
                        ├─ mg_printf(SSE headers)
                        └─ job_sse_add(id, conn)  →  job.c  (register SseConn slot)

Every 300ms:
    Mongoose timer  →  sse_timer_cb()  →  main.c
                            │
                            └─ job_sse_poll()  →  job.c
                                    │
                                    for each active SseConn:
                                    ├─ fopen(logs/job_<id>.log)
                                    ├─ fseek(log_offset)
                                    ├─ fgets() each new line
                                    ├─ mg_printf("data: <line>\n\n")
                                    └─ update log_offset
                                    │
                                    if active == 2 (job finished):
                                    ├─ mg_printf("data: [STREAM END]\n\n")
                                    └─ conn->is_draining = 1
```

---

## 6. Database Schema

Single SQLite3 file (`reboot.db` by default) with WAL journal mode.

```sql
CREATE TABLE user (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT    NOT NULL UNIQUE,
    pwhash   TEXT    NOT NULL,        -- "16-char-salt:64-char-sha256hex"
    role     TEXT    NOT NULL DEFAULT 'user'   -- "admin" | "user"
);

CREATE TABLE job (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL DEFAULT '',
    hex_file    TEXT    NOT NULL,     -- filename under uploads/
    node_id     TEXT    NOT NULL,     -- re-boot -n
    interface   TEXT    NOT NULL,     -- "serial" | "tcp" | "can"
    device      TEXT    NOT NULL,     -- re-boot -i
    tcp_port    TEXT    DEFAULT '',   -- re-boot -p
    retries     TEXT    DEFAULT '',   -- re-boot -t
    reset_flag  TEXT    DEFAULT '',   -- re-boot -r
    verbose     TEXT    DEFAULT '',   -- re-boot -v
    extra_args  TEXT    DEFAULT '',   -- appended verbatim
    status      TEXT    NOT NULL DEFAULT 'pending',  -- pending|running|success|failed
    created_at  TEXT    DEFAULT (datetime('now')),
    started_at  TEXT    DEFAULT '',
    finished_at TEXT    DEFAULT '',
    exit_code   INTEGER DEFAULT -1,
    user_id     INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES user(id)
);

CREATE TABLE session (
    token      TEXT    PRIMARY KEY,   -- 64-char hex
    user_id    INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,      -- Unix timestamp
    FOREIGN KEY (user_id) REFERENCES user(id)
);
```

---

## 7. Authentication Design

### Password Storage

Passwords are never stored in plaintext.  The storage format is:

```
"<16-char-hex-salt>:<64-char-sha256-hexdigest>"
```

Where the digest is `SHA256(salt + ":" + password)`.  The salt is 8 bytes
read from `/dev/urandom`, encoded as 16 lowercase hex characters.

### Session Tokens

On login, `auth_gen_token()` reads 32 bytes from `/dev/urandom` and encodes
them as a 64-char lowercase hex string.  This token is stored in the
`session` table with a Unix expiry timestamp (default: now + 86400 seconds).

The token is delivered to the browser as an HttpOnly, SameSite=Strict cookie
named `rbsid`.  Every authenticated API request reads this cookie,
validates it against the session table (token match + expiry check), and
loads the associated user record.

### Role-Based Access

Two roles exist: `"admin"` and `"user"`.  Admin-only endpoints
(`GET/POST /api/users`, `DELETE /api/users/:id`) check the role field of
the authenticated user and return 403 Forbidden to non-admins.

---

## 8. Concurrency Model

```
┌─────────────────────────────────────────────────────┐
│  Main thread — Mongoose event loop (single-threaded) │
│                                                     │
│  mg_mgr_poll(50ms)  ◄────────────────────────────┐ │
│  SSE timer (300ms)  → job_sse_poll()             │ │
│  HTTP handlers      → job_run() creates threads  │ │
└──────────────────────────────┬──────────────────┘ │
                               │                    │
              pthread_create() │                    │
                               ▼                    │
        ┌─────────────────────────────────────┐     │
        │  runner_thread (one per active job) │     │
        │                                     │     │
        │  fork/exec re-boot                  │     │
        │  relay pipe → log file              │     │
        │  waitpid                            │     │
        │  db_job_set_finished()              │     │
        │  g_sse[i].active = 2   ────────────────→ │
        └─────────────────────────────────────┘
```

A single `pthread_mutex_t g_lock` in `job.c` protects both the
`g_jobs[]` array (running subprocess slots) and the `g_sse[]` array
(SSE connection slots).  The Mongoose event loop and runner threads
coordinate via this mutex — the timer callback acquires it, reads SSE
state, and sends data, while runner threads acquire it to register
their subprocess slot and signal completion.

### Limits

| Resource | Limit | Defined in |
|---|---|---|
| Concurrent jobs | 32 | `MAX_RUNNING_JOBS` in `inc/job.h` |
| Concurrent SSE connections | 64 | `MAX_SSE_CONNS` in `inc/job.h` |
| Recent jobs returned by list | 100 | `db_job_list()` in `src/db.c` |

---

## 9. File Upload Handling

Firmware HEX files are uploaded as multipart form fields.  The upload flow:

1. Mongoose accumulates the entire HTTP request body in memory.
2. `mg_http_next_multipart()` iterates over form parts.
3. The `hex_file` part is detected by its `filename` attribute.
4. The filename is sanitised: path separators stripped, spaces replaced
   with underscores.
5. A Unix timestamp prefix is prepended: `<epoch>_<original-name>.hex`.
   This avoids collisions between concurrent uploads of identically-named
   files.
6. The raw bytes are written to `uploads/<prefixed-name>` via `open()/write()`.
7. Only the filename (not the full path) is stored in the `job.hex_file`
   column; the runner thread reconstructs the path as `uploads/<filename>`.

---

## 10. Build System

The Makefile compiles all C files in a single `gcc` invocation (unity-style
build for simplicity — no incremental object files).

```makefile
CFLAGS = -Wall -Wextra -O2 -Isrc -Iinc -Imongoose -DMG_ENABLE_SSI=0
LDFLAGS = -lsqlite3 -lpthread
```

The `fetch-mongoose` target downloads `mongoose.c` and `mongoose.h` from
the Mongoose GitHub master branch using `curl` if they are absent.

---

## 11. Frontend Architecture

The `www/` directory contains a vanilla HTML/CSS/JS SPA.  There are no
build tools, bundlers, or frameworks — the files are served as-is.

| File | Purpose |
|---|---|
| `login.html` | Login form; submits to `POST /api/login` |
| `dashboard.html` | Job list with run/stop/delete actions |
| `new_job.html` | Multipart form for uploading a hex file and configuring job parameters |
| `job.html` | Live log view using `EventSource` SSE API |
| `users.html` | Admin-only user management (create / delete) |
| `app.js` | Shared JS: session check, fetch wrappers, nav helpers |
| `style.css` | Shared stylesheet |

The frontend communicates exclusively with the `/api/` prefix.  In
production the recommended setup serves static files directly via nginx
while proxying only `/api/` to the C server.

---

## 12. Deployment Topology

```
Internet / LAN
      │
      │ :80 / :443
      ▼
   nginx
   ├── location /          → root /path/to/Re-BOOT-Web/www   (static)
   └── location /api/      → proxy_pass http://127.0.0.1:5000 (C server)
                                  │
                                  │ :5000
                                  ▼
                            reboot-web (C binary)
                            ├── SQLite3  reboot.db
                            ├── uploads/ (hex files)
                            └── logs/    (job output)
```

SSE (`/api/jobs/:id/stream`) requires nginx to disable response buffering
for that location (`proxy_buffering off`) to ensure event frames are
flushed to the browser immediately.

---

## 13. Security Notes

- All passwords use salted SHA-256 (no plaintext stored).
- Session cookies are `HttpOnly` and `SameSite=Strict`.
- All database queries use SQLite prepared statements — no string-format SQL.
- Uploaded filenames are sanitised (path separator stripping) before writing to disk.
- The server does not validate that uploaded files are valid Intel HEX; that
  is left to the `re-boot` binary, which reports errors in the job log.
- The default admin password is `admin` — operators must change it immediately
  after deployment.
