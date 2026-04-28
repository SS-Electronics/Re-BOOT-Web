#include "auth.h"
#include "sha256.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void random_hex(int bytes, char *out) {
    uint8_t buf[64];
    if (bytes > 64) bytes = 64;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { if (read(fd, buf, bytes) < bytes) {} close(fd); }
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < bytes; i++) {
        out[i*2]   = h[buf[i] >> 4];
        out[i*2+1] = h[buf[i] & 0xf];
    }
    out[bytes*2] = '\0';
}

void auth_hash_password(const char *password, char out[DB_STR128]) {
    char salt[17];
    random_hex(8, salt);  /* 16-char salt */

    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%s", salt, password);
    char hexhash[65];
    sha256_hex((uint8_t *)buf, strlen(buf), hexhash);
    snprintf(out, DB_STR128, "%s:%s", salt, hexhash);
}

int auth_check_password(const char *password, const char *stored_hash) {
    /* Format: "salt:sha256hex" */
    char salt[17] = {0};
    const char *colon = strchr(stored_hash, ':');
    if (!colon) return 0;
    size_t salt_len = (size_t)(colon - stored_hash);
    if (salt_len >= sizeof(salt)) return 0;
    memcpy(salt, stored_hash, salt_len);
    salt[salt_len] = '\0';

    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%s", salt, password);
    char hexhash[65];
    sha256_hex((uint8_t *)buf, strlen(buf), hexhash);

    char expected[DB_STR128];
    snprintf(expected, sizeof(expected), "%s:%s", salt, hexhash);
    return strcmp(expected, stored_hash) == 0 ? 1 : 0;
}

void auth_gen_token(char out[65]) {
    random_hex(TOKEN_LEN, out);
}

/* Parse "key=value; key2=value2" cookie header, extract named cookie */
static int get_cookie(const char *header, size_t hlen,
                       const char *name, char *val, size_t vlen) {
    size_t nlen = strlen(name);
    const char *p = header;
    const char *end = header + hlen;
    while (p < end) {
        while (p < end && (*p==' ' || *p=='\t')) p++;
        if ((size_t)(end - p) > nlen && memcmp(p, name, nlen) == 0
                && p[nlen] == '=') {
            p += nlen + 1;
            const char *v = p;
            while (p < end && *p != ';') p++;
            size_t len = (size_t)(p - v);
            if (len >= vlen) len = vlen - 1;
            memcpy(val, v, len);
            val[len] = '\0';
            return 1;
        }
        while (p < end && *p != ';') p++;
        if (p < end) p++; /* skip ';' */
    }
    return 0;
}

User *auth_session_user(struct mg_http_message *hm) {
    struct mg_str *cookie_hdr = mg_http_get_header(hm, "Cookie");
    if (!cookie_hdr) return NULL;

    char token[65] = {0};
    if (!get_cookie(cookie_hdr->buf, cookie_hdr->len,
                    COOKIE_NAME, token, sizeof(token))) return NULL;
    if (strlen(token) < 32) return NULL;

    int64_t uid = 0;
    if (db_session_user(token, (int64_t)time(NULL), &uid) != 0) return NULL;

    User *u = malloc(sizeof(User));
    if (db_user_by_id(uid, u) != 0) { free(u); return NULL; }
    return u;
}
