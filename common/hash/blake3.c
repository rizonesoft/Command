#include "blake3.h"

enum { CHUNK_START = 1, CHUNK_END = 2, PARENT = 4, ROOT = 8, CHUNK_LEN = 1024 };

static const uint32_t IV[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
static const uint8_t PERM[16] = {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 8, 15, 14};

static uint32_t ror(uint32_t x, int n) { return x >> n | x << (32 - n); }

static void g(uint32_t v[16], int a, int b, int c, int d, uint32_t x, uint32_t y) {
    v[a] += v[b] + x, v[d] = ror(v[d] ^ v[a], 16);
    v[c] += v[d], v[b] = ror(v[b] ^ v[c], 12);
    v[a] += v[b] + y, v[d] = ror(v[d] ^ v[a], 8);
    v[c] += v[d], v[b] = ror(v[b] ^ v[c], 7);
}

static void round_(uint32_t v[16], const uint32_t m[16]) {
    g(v, 0, 4, 8, 12, m[0], m[1]), g(v, 1, 5, 9, 13, m[2], m[3]);
    g(v, 2, 6, 10, 14, m[4], m[5]), g(v, 3, 7, 11, 15, m[6], m[7]);
    g(v, 0, 5, 10, 15, m[8], m[9]), g(v, 1, 6, 11, 12, m[10], m[11]);
    g(v, 2, 7, 8, 13, m[12], m[13]), g(v, 3, 4, 9, 14, m[14], m[15]);
}

/* The full 16 word output; the first eight are the chaining value. */
static void compress(const uint32_t cv[8], const uint32_t block[16], uint64_t counter, uint32_t len,
                     uint32_t flags, uint32_t out[16]) {
    uint32_t v[16] = {cv[0], cv[1], cv[2], cv[3], cv[4], cv[5], cv[6], cv[7],
                      IV[0], IV[1], IV[2], IV[3], (uint32_t)counter, (uint32_t)(counter >> 32), len, flags};
    uint32_t m[16], t[16];
    for (int i = 0; i < 16; i++) m[i] = block[i];
    for (int r = 0; r < 7; r++) {
        round_(v, m);
        if (r < 6) {
            for (int i = 0; i < 16; i++) t[i] = m[PERM[i]];
            for (int i = 0; i < 16; i++) m[i] = t[i];
        }
    }
    for (int i = 0; i < 8; i++) out[i] = v[i] ^ v[i + 8], out[i + 8] = v[i + 8] ^ cv[i];
}

static void words(const uint8_t *p, size_t n, uint32_t w[16]) {
    uint8_t padded[64] = {0};
    for (size_t i = 0; i < n; i++) padded[i] = p[i];
    for (int i = 0; i < 16; i++)
        w[i] = padded[4 * i] | (uint32_t)padded[4 * i + 1] << 8 | (uint32_t)padded[4 * i + 2] << 16 |
               (uint32_t)padded[4 * i + 3] << 24;
}

static void parent_cv(const uint32_t left[8], const uint32_t right[8], uint32_t flags, uint32_t out[8]) {
    uint32_t block[16], full[16];
    for (int i = 0; i < 8; i++) block[i] = left[i], block[i + 8] = right[i];
    compress(IV, block, 0, 64, PARENT | flags, full);
    for (int i = 0; i < 8; i++) out[i] = full[i];
}

void blake3_init(blake3_ctx *c) {
    for (int i = 0; i < 8; i++) c->cv[i] = IV[i];
    c->chunk_counter = 0, c->block_len = 0, c->blocks_done = 0, c->stack_len = 0;
}

static void finish_chunk(blake3_ctx *c) {
    uint32_t w[16], full[16], cv[8];
    words(c->block, c->block_len, w);
    compress(c->cv, w, c->chunk_counter, c->block_len, (c->blocks_done ? 0 : CHUNK_START) | CHUNK_END, full);
    for (int i = 0; i < 8; i++) cv[i] = full[i];
    /* Merge with finished left siblings: one merge per trailing zero bit of the new chunk count. */
    uint64_t total = ++c->chunk_counter;
    for (; !(total & 1); total >>= 1) parent_cv(c->stack[--c->stack_len], cv, 0, cv);
    for (int i = 0; i < 8; i++) c->stack[c->stack_len][i] = cv[i];
    c->stack_len++;
    for (int i = 0; i < 8; i++) c->cv[i] = IV[i];
    c->block_len = 0, c->blocks_done = 0;
}

static void compress_block(blake3_ctx *c, const uint8_t *p) {
    uint32_t w[16], full[16];
    words(p, 64, w);
    compress(c->cv, w, c->chunk_counter, 64, c->blocks_done ? 0 : CHUNK_START, full);
    for (int i = 0; i < 8; i++) c->cv[i] = full[i];
    c->blocks_done++;
}

/* The last block of a chunk is held back until more input proves it is not the final one. */
void blake3_update(blake3_ctx *c, const void *data, size_t n) {
    const uint8_t *p = data;
    while (n) {
        if (c->blocks_done == 15 && c->block_len == 64) finish_chunk(c);
        if (c->block_len == 64) compress_block(c, c->block), c->block_len = 0;
        if (c->block_len == 0 && n > 64 && c->blocks_done < 15) {
            /* Whole blocks straight from the input, keeping at least one byte back. */
            size_t whole = (n - 1) / 64;
            if (whole > (size_t)(15 - c->blocks_done)) whole = (size_t)(15 - c->blocks_done);
            for (size_t i = 0; i < whole; i++, p += 64) compress_block(c, p);
            n -= whole * 64;
            continue;
        }
        size_t take = 64 - c->block_len;
        if (take > n) take = n;
        for (size_t i = 0; i < take; i++) c->block[c->block_len + i] = p[i];
        c->block_len = (uint8_t)(c->block_len + take), p += take, n -= take;
    }
}

void blake3_final(const blake3_ctx *c, uint8_t out[32]) {
    uint32_t w[16], full[16], cv[8];
    words(c->block, c->block_len, w);
    uint32_t flags = (c->blocks_done ? 0 : CHUNK_START) | CHUNK_END;
    if (!c->stack_len) {
        compress(c->cv, w, c->chunk_counter, c->block_len, flags | ROOT, full);
    } else {
        compress(c->cv, w, c->chunk_counter, c->block_len, flags, full);
        for (int i = 0; i < 8; i++) cv[i] = full[i];
        for (int s = c->stack_len - 1; s >= 0; s--) {
            uint32_t block[16];
            for (int i = 0; i < 8; i++) block[i] = c->stack[s][i], block[i + 8] = cv[i];
            compress(IV, block, 0, 64, PARENT | (s == 0 ? ROOT : 0), full);
            for (int i = 0; i < 8; i++) cv[i] = full[i];
        }
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(full[i] >> (8 * j));
}
