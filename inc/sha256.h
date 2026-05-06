/**
 * @file sha256.h
 * @brief SHA-256 hash algorithm interface.
 *
 * Public-domain SHA-256 implementation originally by Brad Conte.
 * Source: https://github.com/B-Con/crypto-algorithms
 *
 * @author Subhajit Roy <subhajitroy005@gmail.com>
 * @date   2026-05-06
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief SHA-256 computation context.
 */
typedef struct
{
    uint8_t  data[64];  /**< Current 512-bit data block being processed */
    uint32_t datalen;   /**< Bytes accumulated in data[] (0–63) */
    uint64_t bitlen;    /**< Total bits processed so far */
    uint32_t state[8];  /**< Running hash state (eight 32-bit words) */
} SHA256_CTX;

/**
 * @brief Initialise a SHA-256 context with the standard IV.
 * @param ctx Context to initialise.
 */
void sha256_init(SHA256_CTX *ctx);

/**
 * @brief Feed bytes into an ongoing SHA-256 computation.
 * @param ctx  Context previously initialised with sha256_init().
 * @param data Pointer to input bytes.
 * @param len  Number of bytes to process.
 */
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finalise the digest and write 32 raw bytes to @p hash.
 * @param ctx  Context with accumulated data.
 * @param hash Output buffer; must be at least 32 bytes.
 */
void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]);

/**
 * @brief Compute SHA-256 of @p data and write it as a 64-char lowercase
 *        hex string (plus NUL terminator).
 * @param data Input bytes.
 * @param len  Number of input bytes.
 * @param out  Output buffer; must be at least 65 bytes.
 */
void sha256_hex(const uint8_t *data, size_t len, char out[65]);
