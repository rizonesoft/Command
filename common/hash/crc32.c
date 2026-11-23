#include "crc32.h"

/* Slicing by eight: eight tables, one 64-bit word per step. Built on first use. */
static uint32_t table[8][256];
static int ready;

static void build(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ c >> 1 : c >> 1;
        table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++)
        for (int t = 1; t < 8; t++) table[t][i] = table[0][table[t - 1][i] & 255] ^ table[t - 1][i] >> 8;
    ready = 1;
}

void crc32_init(crc32_ctx *c) {
    if (!ready) build();
    c->crc = 0xffffffffu;
}

void crc32_update(crc32_ctx *c, const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t crc = c->crc;
    for (; n >= 8; p += 8, n -= 8) {
        uint32_t lo = (p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24) ^ crc;
        uint32_t hi = p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
        crc = table[7][lo & 255] ^ table[6][lo >> 8 & 255] ^ table[5][lo >> 16 & 255] ^ table[4][lo >> 24] ^
              table[3][hi & 255] ^ table[2][hi >> 8 & 255] ^ table[1][hi >> 16 & 255] ^ table[0][hi >> 24];
    }
    for (; n; n--) crc = table[0][(crc ^ *p++) & 255] ^ crc >> 8;
    c->crc = crc;
}

uint32_t crc32_final(const crc32_ctx *c) { return ~c->crc; }
