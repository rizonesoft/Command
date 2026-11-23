/* BLAKE3, default hash mode with a 32 byte result. Integer code only, no operating system calls. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t cv[8];            /* chaining value of the chunk being filled */
    uint64_t chunk_counter;
    uint8_t block[64];
    uint8_t block_len;
    uint8_t blocks_done;       /* blocks of this chunk already compressed */
    uint32_t stack[54][8];     /* finished subtrees, one per set bit of the chunk count */
    uint8_t stack_len;
} blake3_ctx;

void blake3_init(blake3_ctx *c);
void blake3_update(blake3_ctx *c, const void *data, size_t n);
void blake3_final(const blake3_ctx *c, uint8_t out[32]);
