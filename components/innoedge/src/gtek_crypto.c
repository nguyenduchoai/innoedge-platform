// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_crypto.h"

#include <stdio.h>
#include <string.h>

#define SHA256_BLOCK_SIZE 64

// ── Bộ cài đặt SHA-256 chuẩn FIPS 180-2 độc lập ──────────────────────────────
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} sha256_ctx_t;

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init_ctx(sha256_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i = 0;
    size_t index = (size_t)((ctx->count >> 3) & 63);
    ctx->count += (uint64_t)len << 3;
    size_t part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha256_transform(ctx, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha256_transform(ctx, &data[i]);
        }
        index = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32])
{
    uint8_t finalcount[8];
    for (int i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count >> ((7 - i) * 8)) & 0xff);
    }
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while ((ctx->count & 504) != 448) {
        sha256_update(ctx, &zero, 1);
    }
    sha256_update(ctx, finalcount, 8);
    for (int i = 0; i < 8; i++) {
        hash[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xff);
        hash[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xff);
        hash[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xff);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xff);
    }
}

void gtek_sha256(const uint8_t *data, size_t len, uint8_t hash_out[32])
{
    sha256_ctx_t ctx;
    sha256_init_ctx(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash_out);
}

// ── HMAC-SHA256 ─────────────────────────────────────────────────────────────
static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32])
{
    uint8_t k[SHA256_BLOCK_SIZE] = {0};
    if (key_len > SHA256_BLOCK_SIZE) {
        gtek_sha256(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }

    uint8_t ipad[SHA256_BLOCK_SIZE];
    uint8_t opad[SHA256_BLOCK_SIZE];
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    uint8_t inner_hash[32];
    sha256_ctx_t ctx;
    sha256_init_ctx(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init_ctx(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out);
}

// ── Quản lý khoá thiết bị ───────────────────────────────────────────────────
static uint8_t s_device_key[32] = {
    0x49,0x6e,0x6e,0x6f,0x45,0x64,0x67,0x65,
    0x2d,0x53,0x65,0x63,0x75,0x72,0x65,0x2d,
    0x48,0x61,0x72,0x64,0x77,0x61,0x72,0x65,
    0x2d,0x4b,0x65,0x79,0x2d,0x32,0x36,0x21
};
static size_t s_key_len = 32;

esp_err_t gtek_crypto_init(const uint8_t *key, size_t key_len)
{
    if (key && key_len > 0) {
        size_t n = (key_len > sizeof(s_device_key)) ? sizeof(s_device_key) : key_len;
        memcpy(s_device_key, key, n);
        s_key_len = n;
    }
    return ESP_OK;
}

esp_err_t gtek_crypto_sign_transaction(const char *device_id, uint32_t seq,
                                       int kind, int count, int64_t amount_vnd,
                                       char *tac_out, size_t tac_len)
{
    if (!device_id || !tac_out || tac_len <= GTEK_TAC_HEX_LEN) return ESP_ERR_INVALID_ARG;

    char msg[128];
    int len = snprintf(msg, sizeof(msg), "%s:%u:%d:%d:%lld",
                       device_id, (unsigned int)seq, kind, count, (long long)amount_vnd);
    if (len < 0 || (size_t)len >= sizeof(msg)) return ESP_ERR_NO_MEM;

    uint8_t hmac[32];
    hmac_sha256(s_device_key, s_key_len, (const uint8_t *)msg, (size_t)len, hmac);

    for (int i = 0; i < 32; i++) {
        snprintf(tac_out + (i * 2), tac_len - (i * 2), "%02x", hmac[i]);
    }
    tac_out[GTEK_TAC_HEX_LEN] = '\0';
    return ESP_OK;
}

bool gtek_crypto_verify_transaction(const char *device_id, uint32_t seq,
                                    int kind, int count, int64_t amount_vnd,
                                    const char *expected_tac)
{
    if (!expected_tac || strlen(expected_tac) != GTEK_TAC_HEX_LEN) return false;

    char calculated[GTEK_TAC_HEX_LEN + 1];
    if (gtek_crypto_sign_transaction(device_id, seq, kind, count, amount_vnd,
                                     calculated, sizeof(calculated)) != ESP_OK) {
        return false;
    }

    // So sánh thời gian hằng số (Constant-time comparison chống timing attack)
    int diff = 0;
    for (int i = 0; i < GTEK_TAC_HEX_LEN; i++) {
        diff |= (calculated[i] ^ expected_tac[i]);
    }
    return (diff == 0);
}
