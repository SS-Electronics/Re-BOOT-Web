#pragma once
#include "db.h"
#include "../mongoose/mongoose.h"

#define SESSION_TTL     86400   /* 1 day */
#define TOKEN_LEN       32      /* bytes from /dev/urandom → 64-char hex */
#define COOKIE_NAME     "rbsid"

/* Hash password with random salt; result stored as "salt:sha256hex" */
void auth_hash_password(const char *password, char out[DB_STR128]);

/* Verify password against stored hash */
int  auth_check_password(const char *password, const char *stored_hash);

/* Generate a random 64-char hex token */
void auth_gen_token(char out[65]);

/* Extract session cookie from request, validate in DB, return user or NULL.
 * Caller must free() the returned User* (it's heap allocated). */
User *auth_session_user(struct mg_http_message *hm);
