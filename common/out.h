/* Buffered output to standard output and standard error, and one-line error reports.
   UTF-8 goes out as bytes when redirected and as UTF-16 to a console. */
#pragma once
#include "tk.h"

typedef struct {
    void *handle;
    bool console;
    bool failed;      /* a write failed; later output is dropped */
    uint32_t error;   /* the Windows error of that failure */
    size_t len;
    char buf[1 << 16];
} stream;

extern stream tk_out, tk_err;

void out_init(void);                                /* binds tk_out and tk_err to the standard handles */
void out_attach(stream *s, void *file_handle);      /* a stream onto an open file */
void out_put(stream *s, const void *bytes, size_t n);
void out_str(stream *s, const char *utf8);
void out_wide(stream *s, const wchar_t *utf16);
void out_char(stream *s, char c);
void out_u64(stream *s, uint64_t value);
void out_hex(stream *s, const uint8_t *bytes, size_t n);
void out_hex_value(stream *s, uint64_t value, int min_digits);
bool out_flush(stream *s);                          /* false when any write on s has failed */

/* One line on standard error: "tool: path: action: message (error N)". path and action may be NULL. */
void err_win(const char *tool, const wchar_t *path, const char *action, uint32_t code);
/* The same without a Windows error: "tool: path: text". */
void err_text(const char *tool, const wchar_t *path, const char *text);
/* Same, with a UTF-8 path. */
void err_text8(const char *tool, const char *path, const char *text);

/* The usual end of a tool: flushes, and turns a failed write on standard output into an error. */
int finish(const char *tool, int code);
