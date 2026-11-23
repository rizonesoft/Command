/* CRC-32 (IEEE 802.3, the zip and PNG one). Integer code only, no operating system calls. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t crc; } crc32_ctx;

void crc32_init(crc32_ctx *c);
void crc32_update(crc32_ctx *c, const void *data, size_t n);
uint32_t crc32_final(const crc32_ctx *c);
