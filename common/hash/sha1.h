/* SHA-1 (FIPS 180-4). Integer code only, no operating system calls. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[5];
    uint64_t bytes;
    uint8_t block[64];
} sha1_ctx;

void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t n);
void sha1_final(sha1_ctx *c, uint8_t out[20]);
