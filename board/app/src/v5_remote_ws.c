#include "v5_remote_ws.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define V5_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct {
    uint32_t state[5];
    uint64_t bit_count;
    unsigned char buffer[64];
    size_t buffer_len;
} V5Sha1;

static int send_all(int fd, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (n == 0) {
            return 0;
        }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static uint32_t rol32(uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32U - bits));
}

static void sha1_block(V5Sha1 *ctx, const unsigned char block[64])
{
    uint32_t w[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    unsigned int i;
    for (i = 0; i < 16U; ++i) {
        w[i] = ((uint32_t)block[i * 4U] << 24) |
               ((uint32_t)block[i * 4U + 1U] << 16) |
               ((uint32_t)block[i * 4U + 2U] << 8) |
               (uint32_t)block[i * 4U + 3U];
    }
    for (i = 16U; i < 80U; ++i) {
        w[i] = rol32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    for (i = 0; i < 80U; ++i) {
        uint32_t f;
        uint32_t k;
        uint32_t temp;
        if (i < 20U) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcU;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6U;
        }
        temp = rol32(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30U);
        b = a;
        a = temp;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(V5Sha1 *ctx)
{
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xefcdab89U;
    ctx->state[2] = 0x98badcfeU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xc3d2e1f0U;
    ctx->bit_count = 0U;
    ctx->buffer_len = 0U;
}

static void sha1_update(V5Sha1 *ctx, const unsigned char *data, size_t size)
{
    ctx->bit_count += (uint64_t)size * 8U;
    while (size > 0U) {
        size_t take = 64U - ctx->buffer_len;
        if (take > size) {
            take = size;
        }
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        size -= take;
        if (ctx->buffer_len == 64U) {
            sha1_block(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
    }
}

static void sha1_final(V5Sha1 *ctx, unsigned char digest[20])
{
    unsigned char pad = 0x80U;
    unsigned char zero = 0U;
    unsigned char len_bytes[8];
    uint64_t bits = ctx->bit_count;
    unsigned int i;
    sha1_update(ctx, &pad, 1U);
    while (ctx->buffer_len != 56U) {
        sha1_update(ctx, &zero, 1U);
    }
    for (i = 0; i < 8U; ++i) {
        len_bytes[7U - i] = (unsigned char)(bits >> (i * 8U));
    }
    sha1_update(ctx, len_bytes, sizeof(len_bytes));
    for (i = 0; i < 5U; ++i) {
        digest[i * 4U] = (unsigned char)(ctx->state[i] >> 24);
        digest[i * 4U + 1U] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4U + 2U] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4U + 3U] = (unsigned char)ctx->state[i];
    }
}

static void base64_encode(const unsigned char *data, size_t size, char *out, size_t out_size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0U;
    size_t o = 0U;
    while (i < size && o + 4U < out_size) {
        unsigned int a = data[i++];
        unsigned int b = 0U;
        unsigned int c = 0U;
        unsigned int have = 1U;
        if (i < size) {
            b = data[i++];
            ++have;
        }
        if (i < size) {
            c = data[i++];
            ++have;
        }
        out[o++] = table[(a >> 2) & 0x3fU];
        out[o++] = table[((a & 0x3U) << 4) | ((b >> 4) & 0xfU)];
        out[o++] = have > 1U ? table[((b & 0xfU) << 2) | ((c >> 6) & 0x3U)] : '=';
        out[o++] = have > 2U ? table[c & 0x3fU] : '=';
    }
    if (out_size > 0U) {
        out[o < out_size ? o : out_size - 1U] = '\0';
    }
}

static int extract_ws_key(const char *request, char *key, size_t key_size)
{
    const char *p = strstr(request, "Sec-WebSocket-Key:");
    const char *end;
    size_t n;
    if (!p || key_size == 0U) {
        return 0;
    }
    p += strlen("Sec-WebSocket-Key:");
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    end = p;
    while (*end && *end != '\r' && *end != '\n') {
        ++end;
    }
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    n = (size_t)(end - p);
    if (n == 0U || n >= key_size) {
        return 0;
    }
    memcpy(key, p, n);
    key[n] = '\0';
    return 1;
}

int v5_remote_ws_accept(int fd, const char *http_request)
{
    char key[96];
    char source[160];
    unsigned char digest[20];
    char accept[40];
    char response[220];
    V5Sha1 sha1;
    if (!extract_ws_key(http_request, key, sizeof(key))) {
        return 0;
    }
    snprintf(source, sizeof(source), "%s%s", key, V5_WS_GUID);
    sha1_init(&sha1);
    sha1_update(&sha1, (const unsigned char *)source, strlen(source));
    sha1_final(&sha1, digest);
    base64_encode(digest, sizeof(digest), accept, sizeof(accept));
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept);
    return send_all(fd, response, strlen(response));
}

static int recv_exact(int fd, unsigned char *buffer, size_t size)
{
    while (size > 0U) {
        ssize_t n = recv(fd, buffer, size, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        buffer += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

int v5_remote_ws_recv_text(int fd, char *buffer, size_t size)
{
    unsigned char header[2];
    unsigned char mask[4];
    uint64_t payload_len;
    unsigned int i;
    int opcode;
    int masked;
    if (!buffer || size == 0U) {
        return -1;
    }
    if (recv_exact(fd, header, sizeof(header)) <= 0) {
        return -1;
    }
    opcode = header[0] & 0x0f;
    masked = (header[1] & 0x80U) != 0U;
    payload_len = header[1] & 0x7fU;
    if (opcode == 0x8) {
        return -1;
    }
    if (opcode != 0x1 || !masked) {
        return -1;
    }
    if (payload_len == 126U) {
        unsigned char ext[2];
        if (recv_exact(fd, ext, sizeof(ext)) <= 0) {
            return -1;
        }
        payload_len = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1];
    } else if (payload_len == 127U) {
        return -1;
    }
    if (payload_len + 1U > size) {
        return -1;
    }
    if (recv_exact(fd, mask, sizeof(mask)) <= 0) {
        return -1;
    }
    if (recv_exact(fd, (unsigned char *)buffer, (size_t)payload_len) <= 0) {
        return -1;
    }
    for (i = 0; i < payload_len; ++i) {
        buffer[i] = (char)((unsigned char)buffer[i] ^ mask[i & 3U]);
    }
    buffer[payload_len] = '\0';
    return (int)payload_len;
}

int v5_remote_ws_send_text(int fd, const char *text)
{
    unsigned char header[4];
    size_t len = strlen(text ? text : "");
    header[0] = 0x81U;
    if (len < 126U) {
        header[1] = (unsigned char)len;
        return send_all(fd, header, 2U) && send_all(fd, text ? text : "", len);
    }
    if (len <= 65535U) {
        header[1] = 126U;
        header[2] = (unsigned char)(len >> 8);
        header[3] = (unsigned char)len;
        return send_all(fd, header, 4U) && send_all(fd, text ? text : "", len);
    }
    return 0;
}
