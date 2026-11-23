/* Reads text lines of any length up to a limit from a reader. */
#pragma once
#include "fs.h"

typedef struct {
    reader *r;
    char *buf;
    size_t cap, pos, len;
    bool eof;
    uint64_t number; /* of the line last returned, from 1 */
} liner;

#define LINE_LIMIT ((size_t)16 << 20)

void liner_start(liner *l, reader *r);
void liner_free(liner *l);
/* 1 and the line without its "\n" or "\r\n", 0 at the end, -1 on a read error or a line over the limit
   (in l->r->error). The line stays valid until the next call. */
int liner_next(liner *l, const char **line, size_t *len);
