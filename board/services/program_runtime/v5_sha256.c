#include "v5_sha256.h"

#include <stdint.h>
#include <string.h>

typedef struct V5Sha256 {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char buffer[64];
    size_t buffer_len;
} V5Sha256;

static const uint32_t V5_SHA256_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t rotr32(uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static void write_be32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24U);
    p[1] = (unsigned char)(value >> 16U);
    p[2] = (unsigned char)(value >> 8U);
    p[3] = (unsigned char)value;
}

static void write_be64(unsigned char *p, uint64_t value)
{
    unsigned int i;
    for (i = 0U; i < 8U; ++i) {
        p[7U - i] = (unsigned char)(value >> (i * 8U));
    }
}

static void sha256_transform(V5Sha256 *ctx, const unsigned char block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned int i;

    for (i = 0U; i < 16U; ++i) {
        w[i] = read_be32(block + i * 4U);
    }
    for (i = 16U; i < 64U; ++i) {
        uint32_t s0 = rotr32(w[i - 15U], 7U) ^ rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        uint32_t s1 = rotr32(w[i - 2U], 17U) ^ rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0U; i < 64U; ++i) {
        uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + V5_SHA256_K[i] + w[i];
        uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(V5Sha256 *ctx)
{
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bit_count = 0U;
    ctx->buffer_len = 0U;
}

static void sha256_update(V5Sha256 *ctx, const unsigned char *data, size_t size)
{
    size_t offset = 0U;
    ctx->bit_count += (uint64_t)size * 8U;
    while (offset < size) {
        size_t room = sizeof(ctx->buffer) - ctx->buffer_len;
        size_t chunk = size - offset;
        if (chunk > room) {
            chunk = room;
        }
        memcpy(ctx->buffer + ctx->buffer_len, data + offset, chunk);
        ctx->buffer_len += chunk;
        offset += chunk;
        if (ctx->buffer_len == sizeof(ctx->buffer)) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
    }
}

static void sha256_final(V5Sha256 *ctx, unsigned char digest[32])
{
    unsigned int i;
    unsigned char pad = 0x80U;
    unsigned char zero = 0U;
    unsigned char len_bytes[8];
    write_be64(len_bytes, ctx->bit_count);
    sha256_update(ctx, &pad, 1U);
    while (ctx->buffer_len != 56U) {
        sha256_update(ctx, &zero, 1U);
    }
    sha256_update(ctx, len_bytes, sizeof(len_bytes));
    for (i = 0U; i < 8U; ++i) {
        write_be32(digest + i * 4U, ctx->state[i]);
    }
}

void v5_sha256_hex(const unsigned char *data, size_t size, char out_hex[65])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    V5Sha256 ctx;
    unsigned int i;

    if (!out_hex) {
        return;
    }
    sha256_init(&ctx);
    if (data && size > 0U) {
        sha256_update(&ctx, data, size);
    }
    sha256_final(&ctx, digest);
    for (i = 0U; i < sizeof(digest); ++i) {
        out_hex[i * 2U] = hex[digest[i] >> 4U];
        out_hex[i * 2U + 1U] = hex[digest[i] & 0x0fU];
    }
    out_hex[64] = '\0';
}
