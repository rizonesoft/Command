#include <windows.h>
#include "out.h"
#include "text.h"

stream tk_out, tk_err;

void *tk_alloc(size_t bytes) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1); }
void *tk_realloc(void *memory, size_t bytes) {
    if (!memory) return HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
    return HeapReAlloc(GetProcessHeap(), 0, memory, bytes ? bytes : 1);
}
void tk_free(void *memory) {
    if (memory) HeapFree(GetProcessHeap(), 0, memory);
}

static void bind(stream *s, DWORD which) {
    DWORD mode;
    s->handle = GetStdHandle(which);
    if (!s->handle || s->handle == INVALID_HANDLE_VALUE) s->handle = NULL, s->failed = true, s->error = ERROR_INVALID_HANDLE;
    s->console = s->handle && GetConsoleMode(s->handle, &mode);
}

void out_init(void) {
    bind(&tk_out, STD_OUTPUT_HANDLE);
    bind(&tk_err, STD_ERROR_HANDLE);
}

void out_attach(stream *s, void *file_handle) {
    s->handle = file_handle, s->console = false, s->failed = false, s->error = 0, s->len = 0;
}

static void write_console(stream *s, const unsigned char *p, size_t n) {
    wchar_t wide[1024];
    while (n) {
        size_t count = 0;
        while (n && count < 1020) {
            /* One character at a time, so a piece never ends inside one. A bad lead byte still advances
               by its nominal length; each of those bytes then comes out as U+FFFD or itself. */
            unsigned lead = p[0];
            size_t used = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC2 ? 2 : 1;
            if (used > n) used = n;
            count += utf8_to_utf16((const char *)p, used, wide + count, true);
            p += used, n -= used;
        }
        for (DWORD sent = 0, done; sent < count; sent += done) {
            if (!WriteConsoleW(s->handle, wide + sent, (DWORD)count - sent, &done, NULL) || !done) {
                s->failed = true, s->error = GetLastError();
                return;
            }
        }
    }
}

static bool flush(stream *s, bool final) {
    size_t n = s->len;
    if (s->failed || !n) return !s->failed;
    size_t keep = 0;
    if (s->console && !final) keep = utf8_partial_tail((const unsigned char *)s->buf, n);
    if (s->console) {
        write_console(s, (const unsigned char *)s->buf, n - keep);
    } else {
        for (size_t at = 0; at < n && !s->failed;) {
            DWORD done, part = n - at > 0x100000 ? 0x100000 : (DWORD)(n - at);
            if (!WriteFile(s->handle, s->buf + at, part, &done, NULL) || !done) s->failed = true, s->error = GetLastError();
            at += done;
        }
    }
    for (size_t i = 0; i < keep; i++) s->buf[i] = s->buf[n - keep + i];
    s->len = s->failed ? 0 : keep;
    return !s->failed;
}

bool out_flush(stream *s) { return flush(s, false); }

void out_put(stream *s, const void *bytes, size_t n) {
    const char *p = bytes;
    if (s->failed) return;
    while (n) {
        size_t room = sizeof s->buf - s->len, take = n < room ? n : room;
        for (size_t i = 0; i < take; i++) s->buf[s->len + i] = p[i];
        s->len += take, p += take, n -= take;
        if (s->len == sizeof s->buf && !out_flush(s)) return;
    }
    /* A console shows each line as it is written, so Ctrl+C loses nothing already printed. */
    if (s->console && s->len && s->buf[s->len - 1] == '\n') out_flush(s);
}

void out_str(stream *s, const char *utf8) { out_put(s, utf8, tk_slen(utf8)); }
void out_char(stream *s, char c) { out_put(s, &c, 1); }

void out_wide(stream *s, const wchar_t *utf16) {
    char piece[1024];
    size_t n = tk_wlen(utf16);
    while (n) {
        size_t take = n > 250 ? 250 : n;
        /* Do not split a surrogate pair between pieces. */
        if (take < n && (uint16_t)utf16[take - 1] >= 0xD800 && (uint16_t)utf16[take - 1] <= 0xDBFF) take--;
        out_put(s, piece, utf16_to_utf8(utf16, take, piece));
        utf16 += take, n -= take;
    }
}

void out_u64(stream *s, uint64_t value) {
    char digits[24];
    out_put(s, digits, format_u64(value, digits));
}

void out_hex(stream *s, const uint8_t *bytes, size_t n) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        char two[2] = {hex[bytes[i] >> 4], hex[bytes[i] & 15]};
        out_put(s, two, 2);
    }
}

void out_hex_value(stream *s, uint64_t value, int min_digits) {
    static const char hex[] = "0123456789abcdef";
    char digits[16];
    int n = 0;
    do digits[n++] = hex[value & 15], value >>= 4;
    while (value);
    for (; n < min_digits && n < 16; n++) digits[n] = '0';
    while (n) out_char(s, digits[--n]);
}

static const char *describe(uint32_t code) {
    switch (code) {
    case 2: return "The system cannot find the file specified.";
    case 3: return "The system cannot find the path specified.";
    case 5: return "Access is denied.";
    case 8: return "Not enough memory resources are available.";
    case 19: return "The media is write protected.";
    case 21: return "The device is not ready.";
    case 23: return "Data error (cyclic redundancy check).";
    case 32: return "The process cannot access the file because it is being used by another process.";
    case 33: return "The process cannot access the file because another process has locked a portion of the file.";
    case 53: return "The network path was not found.";
    case 55: return "The specified network resource is no longer available.";
    case 64: return "The specified network name is no longer available.";
    case 67: return "The network name cannot be found.";
    case 87: return "The parameter is incorrect.";
    case 109: return "The pipe has been ended.";
    case 112: return "There is not enough space on the disk.";
    case 123: return "The filename, directory name, or volume label syntax is incorrect.";
    case 206: return "The filename or extension is too long.";
    case 232: return "The pipe is being closed.";
    case 267: return "The directory name is invalid.";
    case 1117: return "The request could not be performed because of an I/O device error.";
    case 1920: return "The file cannot be accessed by the system.";
    case 1921: return "The name of the file cannot be resolved by the system.";
    default: return "Operation failed.";
    }
}

static void line_start(const char *tool, const wchar_t *path, const char *utf8_path) {
    out_flush(&tk_out); /* keep standard output ahead of the error when both go to one place */
    out_str(&tk_err, tool);
    out_str(&tk_err, ": ");
    if (path) out_wide(&tk_err, path), out_str(&tk_err, ": ");
    if (utf8_path) out_str(&tk_err, utf8_path), out_str(&tk_err, ": ");
}

void err_win(const char *tool, const wchar_t *path, const char *action, uint32_t code) {
    line_start(tool, path, NULL);
    if (action) out_str(&tk_err, action), out_str(&tk_err, ": ");
    out_str(&tk_err, describe(code));
    out_str(&tk_err, " (error ");
    out_u64(&tk_err, code);
    out_str(&tk_err, ")\n");
    out_flush(&tk_err);
}

void err_text(const char *tool, const wchar_t *path, const char *text) {
    line_start(tool, path, NULL);
    out_str(&tk_err, text);
    out_char(&tk_err, '\n');
    out_flush(&tk_err);
}

void err_text8(const char *tool, const char *path, const char *text) {
    line_start(tool, NULL, path);
    out_str(&tk_err, text);
    out_char(&tk_err, '\n');
    out_flush(&tk_err);
}

int finish(const char *tool, int code) {
    flush(&tk_out, true);
    if (tk_out.failed) {
        err_win(tool, NULL, "cannot write standard output", tk_out.error);
        return TK_ERROR;
    }
    return code;
}
