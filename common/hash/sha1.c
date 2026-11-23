#include "sha1.h"

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void compress(uint32_t s[5], const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5a827999;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ed9eba1;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdc;
        else f = b ^ c ^ d, k = 0xca62c1d6;
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    s[0] += a, s[1] += b, s[2] += c, s[3] += d, s[4] += e;
}

void sha1_init(sha1_ctx *c) {
    c->state[0] = 0x67452301, c->state[1] = 0xefcdab89, c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476, c->state[4] = 0xc3d2e1f0;
    c->bytes = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t n) {
    const uint8_t *p = data;
    size_t fill = (size_t)(c->bytes & 63);
    c->bytes += n;
    if (fill) {
        while (n && fill < 64) c->block[fill++] = *p++, n--;
        if (fill < 64) return;
        compress(c->state, c->block);
    }
    for (; n >= 64; p += 64, n -= 64) compress(c->state, p);
    for (size_t i = 0; i < n; i++) c->block[i] = p[i];
}

void sha1_final(sha1_ctx *c, uint8_t out[20]) {
    uint64_t bits = c->bytes << 3;
    uint8_t pad[72] = {0x80};
    size_t fill = (size_t)(c->bytes & 63);
    size_t padlen = (fill < 56 ? 56 : 120) - fill;
    for (int i = 0; i < 8; i++) pad[padlen + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(c, pad, padlen + 8);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(c->state[i] >> (24 - 8 * j));
}
