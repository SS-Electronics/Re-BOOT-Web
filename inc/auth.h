/**
 * @file auth.h
 * @brief Password hashing and session-cookie authentication.
 *
 * Passwords are stored as "salt:sha256hex" where the salt is 8 random bytes
 * encoded as 16 hex characters.  Sessions are random 64-char hex tokens
 * persisted in the SQLite session table with a configurable TTL.
 *
 * @author Subhajit Roy <subhajitroy005@gmail.com>
 * @date   2026-05-06
 */
#pragma once
#include "db.h"
#include "../mongoose/mongoose.h"

#define SESSION_TTL  86400    /**< Session lifetime in seconds (24 hours) */
#define TOKEN_LEN    32       /**< Random bytes consumed per token => 64-char hex */
#define COOKIE_NAME  "rbsid"  /**< HTTP cookie name used to carry session tokens */

/**
 * @brief Hash a plaintext password with a fresh random salt.
 *
 * Reads TOKEN_LEN/4 bytes from /dev/urandom to generate the salt, then
 * computes SHA-256("salt:password") and stores the result as
 * "16-char-salt:64-char-sha256hex" in @p out.
 *
 * @param password Plaintext password string (NUL-terminated).
 * @param out      Output buffer; must be at least DB_STR128 bytes.
 */
void auth_hash_password(const char *password, char out[DB_STR128]);

/**
 * @brief Verify a plaintext password against a stored hash.
 * @param password    Plaintext password to check.
 * @param stored_hash Hash string in "salt:sha256hex" format.
 * @return 1 if the password matches, 0 otherwise.
 */
int auth_check_password(const char *password, const char *stored_hash);

/**
 * @brief Generate a cryptographically random 64-char hex session token.
 *
 * Reads TOKEN_LEN bytes from /dev/urandom.
 *
 * @param out Output buffer; must be at least 65 bytes.
 */
void auth_gen_token(char out[65]);

/**
 * @brief Extract and validate the session cookie from an HTTP request.
 *
 * Reads the COOKIE_NAME cookie, validates it against the session table,
 * and returns a heap-allocated User struct on success.
 * The caller is responsible for calling free() on the returned pointer.
 *
 * @param hm Parsed Mongoose HTTP message.
 * @return Heap-allocated User* on success, NULL if unauthenticated.
 */
User *auth_session_user(struct mg_http_message *hm);
