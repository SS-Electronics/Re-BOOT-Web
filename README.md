# Re-BOOT Web

A lightweight C web server for remotely flashing embedded firmware via the [Re-BOOT](https://github.com/SS-Electronics/Re-BOOT) utility. Runs on a Raspberry Pi, acts as a Jenkins-style OTA portal.

```
  ╔══════════════════════════════════════╗
  ║       Re-BOOT Web Server             ║
  ║                                      ║
  ║  Language  : C (no runtime deps)     ║
  ║  HTTP lib  : Mongoose (embedded)     ║
  ║  Database  : SQLite3                 ║
  ║  Auth      : Session cookies + SHA256║
  ║  Streaming : Server-Sent Events      ║
  ╚══════════════════════════════════════╝
```

---

## Features

- Upload Intel HEX firmware files from the browser
- Configure all `re-boot` CLI arguments from the UI (interface, device, node ID, retries, etc.)
- **Live log streaming** via Server-Sent Events — watch flashing progress in real time
- Stop a running flash job at any time
- Job history with status, timestamps, and exit codes
- Multi-user login with role-based access (admin / user)
- Simple SQLite database — no server, no ORM, one file

---

## Directory Structure

```
Re-BOOT-Web/
├── src/
│   ├── main.c          # Entry point — arg parsing, Mongoose event loop
│   ├── db.c / db.h     # SQLite3 wrapper (users, jobs, sessions)
│   ├── auth.c / auth.h # Password hashing (SHA256+salt), session tokens
│   ├── job.c / job.h   # Subprocess runner (fork/exec), SSE log tailing
│   ├── routes.c / .h   # HTTP route handlers, REST API
│   └── sha256.c / .h   # Embedded SHA256 (public domain, no OpenSSL dep)
├── www/
│   ├── login.html
│   ├── dashboard.html
│   ├── new_job.html
│   ├── job.html
│   ├── users.html
│   ├── style.css
│   └── app.js
├── mongoose/           # Created by `make` — Mongoose single-file HTTP lib
├── uploads/            # Created at runtime — uploaded .hex files
├── logs/               # Created at runtime — per-job output logs
├── Makefile
├── install_deps.sh
└── README.md
```

---

## Dependencies

| Package | Purpose |
|---|---|
| `build-essential` | GCC + Make |
| `curl` | Fetch Mongoose at build time |
| `libsqlite3-dev` | SQLite3 C library |
| `nginx` | Reverse proxy (optional but recommended) |

No Python, no Node.js, no heavy runtime.

---

## Installation

### 1. Install Dependencies

```bash
sudo ./install_deps.sh
```

This installs `build-essential`, `curl`, `libsqlite3-dev`, and `nginx` on Debian/Ubuntu/Raspberry Pi OS.

### 2. Build

```bash
make
```

The first build automatically downloads `mongoose.c` and `mongoose.h` from the Mongoose GitHub repository using `curl`. Subsequent builds skip the download.

```
Downloading Mongoose...
Linking reboot-web...
Build complete: ./reboot-web
```

### 3. Place the re-boot Binary

Copy the compiled `re-boot` binary from the [Re-BOOT](../Re-BOOT/) project into this directory:

```bash
cp ../Re-BOOT/re-boot .
```

Or point to it with the `-b` flag at runtime.

### 4. Run

```bash
./reboot-web
```

Server starts on `0.0.0.0:5000` by default.

#### CLI Options

```
./reboot-web [options]
  -l <addr>     Listen address      (default: 0.0.0.0:5000)
  -b <path>     Path to re-boot     (default: ./re-boot)
  -d <path>     Database file       (default: reboot.db)
  -u <dir>      Upload directory    (default: uploads)
  -L <dir>      Log directory       (default: logs)
  -w <dir>      Static www dir      (default: www)
  -h            Show help
```

Default login: **admin / admin** — change it immediately after first login via the Users page.

---

## Running as a systemd Service (Raspberry Pi)

Create `/etc/systemd/system/reboot-web.service`:

```ini
[Unit]
Description=Re-BOOT Web Server
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/Re-BOOT-Web
ExecStart=/home/pi/Re-BOOT-Web/reboot-web -b /home/pi/Re-BOOT-Web/re-boot
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable reboot-web
sudo systemctl start  reboot-web
sudo systemctl status reboot-web
```

---

## Nginx Configuration

Re-BOOT Web's C server binds to `localhost:5000`. Nginx acts as the public-facing reverse proxy and handles HTTPS termination.

> **Important:** nginx ships with a default site that serves `/var/www/html` and conflicts
> with any custom config. **Disable it first** before adding the Re-BOOT site:
>
> ```bash
> sudo rm -f /etc/nginx/sites-enabled/default
> ```

### Architecture

The recommended setup splits responsibilities between nginx and the C server:

```
Browser
  │
  ▼  port 80 / 443
nginx
  ├── /api/*       → proxy_pass → reboot-web :5000  (API + SSE)
  └── /            → serve www/ directly             (static HTML/CSS/JS)
```

nginx serves static files from the `www/` directory directly (fast, no subprocess).
Only `/api/` requests are forwarded to the C server. SSE buffering is disabled only
for the stream endpoint.

---

### Step 1 — Create the nginx site

Create `/etc/nginx/sites-available/reboot-web` (replace the path with your actual
deployment directory):

```nginx
server {
    listen 80;
    server_name _;       # replace with your Pi's hostname or IP if needed

    # Absolute path to the www/ directory of this project
    root /home/pi/Re-BOOT-Web/www;
    index login.html;

    # Serve static files (HTML, CSS, JS) directly — fast path
    location / {
        try_files $uri $uri/ /login.html;
    }

    # Proxy all API calls to the C server
    location /api/ {
        proxy_pass         http://127.0.0.1:5000;
        proxy_http_version 1.1;
        proxy_set_header   Host            $host;
        proxy_set_header   X-Real-IP       $remote_addr;
        proxy_set_header   X-Forwarded-For $proxy_add_x_forwarded_for;

        # SSE requires these — disable nginx response buffering
        proxy_buffering    off;
        proxy_cache        off;
        proxy_set_header   Connection '';
        chunked_transfer_encoding on;

        proxy_read_timeout 3600s;
        proxy_send_timeout 3600s;
    }
}
```

### Step 2 — Enable and test

```bash
# Disable the default nginx site (serves /var/www/html — conflicts with ours)
sudo rm -f /etc/nginx/sites-enabled/default

# Enable Re-BOOT Web site
sudo ln -sf /etc/nginx/sites-available/reboot-web /etc/nginx/sites-enabled/

# Test config syntax
sudo nginx -t

# Apply
sudo systemctl reload nginx
```

Visit `http://<raspberry-pi-ip>/` — you should see the login page.

---

### HTTPS with Self-Signed Certificate

```bash
sudo openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout /etc/ssl/private/reboot-web.key \
    -out    /etc/ssl/certs/reboot-web.crt \
    -subj "/CN=reboot-web"
```

Replace the `server` block in `/etc/nginx/sites-available/reboot-web` with:

```nginx
server {
    listen 443 ssl;
    server_name _;

    root /home/pi/Re-BOOT-Web/www;
    index login.html;

    ssl_certificate     /etc/ssl/certs/reboot-web.crt;
    ssl_certificate_key /etc/ssl/private/reboot-web.key;
    ssl_protocols       TLSv1.2 TLSv1.3;

    location / {
        try_files $uri $uri/ /login.html;
    }

    location /api/ {
        proxy_pass         http://127.0.0.1:5000;
        proxy_http_version 1.1;
        proxy_buffering    off;
        proxy_cache        off;
        proxy_set_header   Connection '';
        chunked_transfer_encoding on;
        proxy_set_header   Host            $host;
        proxy_set_header   X-Real-IP       $remote_addr;
        proxy_read_timeout 3600s;
        proxy_send_timeout 3600s;
    }
}

# Redirect HTTP → HTTPS
server {
    listen 80;
    server_name _;
    return 301 https://$host$request_uri;
}

```

---

## REST API Reference

All endpoints except `/api/login` require a valid session cookie (`rbsid`).

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/login` | Login — body: `{"username":"","password":""}` |
| `POST` | `/api/logout` | Logout |
| `GET`  | `/api/me` | Current user info |
| `GET`  | `/api/jobs` | List all jobs |
| `POST` | `/api/jobs` | Create job (multipart form) |
| `GET`  | `/api/jobs/:id` | Job detail |
| `POST` | `/api/jobs/:id/run` | Start job |
| `POST` | `/api/jobs/:id/stop` | Stop job (SIGTERM) |
| `DELETE` | `/api/jobs/:id` | Delete job |
| `GET`  | `/api/jobs/:id/stream` | SSE log stream |
| `GET`  | `/api/jobs/:id/status` | Job status JSON |
| `GET`  | `/api/users` | List users (admin) |
| `POST` | `/api/users` | Add user (admin) |
| `DELETE` | `/api/users/:id` | Delete user (admin) |

### Create Job — Form Fields

| Field | Required | Description |
|-------|----------|-------------|
| `hex_file` | Yes | Intel HEX file (upload) |
| `name` | No | Display name |
| `node_id` | Yes | `-n` — target node ID |
| `interface` | Yes | `-c` — `serial`, `tcp`, or `can` |
| `device` | Yes | `-i` — serial device or IP address |
| `tcp_port` | TCP only | `-p` — TCP port |
| `retries` | No | `-t` — max retries per sector |
| `reset_flag` | No | `-r` — `0` or `1` |
| `verbose` | No | `-v` — `1`, `2`, or `3` |
| `extra_args` | No | Appended verbatim to command |

---

## Build Options

| Variable | Default | Description |
|---|---|---|
| `CC` | `gcc` | C compiler |
| `TARGET` | `reboot-web` | Output binary name |

```bash
# Debug build
make CFLAGS="-Wall -Wextra -g -O0 -I src -I mongoose"

# Clean binary only
make clean

# Clean binary + Mongoose sources
make distclean
```

---

## License

GPL-3.0 — see [LICENSE](LICENSE).
