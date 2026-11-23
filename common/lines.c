#include "lines.h"

void liner_start(liner *l, reader *r) { *l = (liner){.r = r}; }

void liner_free(liner *l) { tk_free(l->buf); }

int liner_next(liner *l, const char **line, size_t *len) {
    size_t scanned = 0;
    for (;;) {
        for (size_t i = l->pos + scanned; i < l->len; i++) {
            if (l->buf[i] != '\n') continue;
            size_t end = i;
            if (end > l->pos && l->buf[end - 1] == '\r') end--;
            *line = l->buf + l->pos, *len = end - l->pos;
            l->buf[end] = 0; /* a terminator for callers; the "\n" or "\r" is ours to overwrite */
            l->pos = i + 1, l->number++;
            return 1;
        }
        scanned = l->len - l->pos;
        if (l->eof) {
            if (l->len == l->pos) return 0;
            size_t end = l->len;
            if (end > l->pos && l->buf[end - 1] == '\r') end--;
            *line = l->buf + l->pos, *len = end - l->pos;
            l->buf[end] = 0;
            l->pos = l->len, l->number++;
            return 1;
        }
        if (l->pos) { /* make room by dropping what was handed out */
            for (size_t i = l->pos; i < l->len; i++) l->buf[i - l->pos] = l->buf[i];
            l->len -= l->pos, l->pos = 0;
        }
        if (l->len + 1 >= l->cap) {
            if (l->cap >= LINE_LIMIT) return l->r->error = ERROR_INSUFFICIENT_BUFFER, -1;
            size_t cap = l->cap ? l->cap * 2 : 65536;
            char *grown = tk_realloc(l->buf, cap);
            if (!grown) return l->r->error = ERROR_NOT_ENOUGH_MEMORY, -1;
            l->buf = grown, l->cap = cap;
        }
        size_t got;
        if (!reader_read(l->r, l->buf + l->len, l->cap - l->len - 1, &got)) return -1;
        if (!got) l->eof = true;
        l->len += got;
    }
}
