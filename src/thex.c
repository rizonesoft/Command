/* thex: hex dump of a file or standard input, and the reverse. */
#include <impossible/base.h>
#include "../common/cmdline.h"
#include "../common/fs.h"
#include "../common/lines.h"
#include "../common/out.h"
#include "../common/text.h"

#define TOOL "thex"
#define MAX_WIDTH 256

static void usage(void) {
    out_str(&tk_out,
        "Usage: thex [-s OFFSET] [-n LENGTH] [-w BYTES] [FILE]\n"
        "       thex -r [-o OUTPUT] [FILE]\n"
        "\n"
        "Dumps FILE (or standard input) as lines of offset, hex bytes and text:\n"
        "\n"
        "  00000000  48 65 6c 6c 6f 20 77 6f  72 6c 64 0a              |Hello world.|\n"
        "\n"
        "  -s, --skip OFFSET    start at this byte (decimal, 0x hex, or with a k, m, g, t suffix)\n"
        "  -n, --length LENGTH  dump at most this many bytes\n"
        "  -w, --width BYTES    bytes per line, 1 to 256 (default 16)\n"
        "  -r, --reverse        read such a dump and write the bytes it describes; every line's\n"
        "                       offset must follow on from the line before\n"
        "  -o, --output FILE    with -r, write to FILE instead of standard output; the file is only\n"
        "                       replaced when the whole dump has been read and written\n"
        "  -h, --help           show this help\n"
        "      --version        show the version\n"
        "\n"
        "Exit status: 0 success, 2 error.\n");
}

static void line_error(const wchar_t *path, uint64_t number, const char *why) {
    char text[96] = "line ";
    size_t n = 5 + format_u64(number, text + 5);
    text[n++] = ':', text[n++] = ' ';
    for (const char *p = why; *p && n < sizeof text - 1; p++) text[n++] = *p;
    text[n] = 0;
    err_text(TOOL, path, text);
}

/* One line: offset, up to width bytes (padded when short), then the printable text. */
static void emit_row(uint64_t at, const unsigned char *row, size_t count, size_t width) {
    out_hex_value(&tk_out, at, 8);
    out_str(&tk_out, "  ");
    for (size_t k = 0; k < width; k++) {
        if (k < count) out_hex(&tk_out, row + k, 1), out_char(&tk_out, ' ');
        else out_str(&tk_out, "   ");
        if (k % 8 == 7 && k + 1 < width) out_char(&tk_out, ' ');
    }
    out_str(&tk_out, " |");
    for (size_t k = 0; k < count; k++) out_char(&tk_out, row[k] >= 0x20 && row[k] < 0x7f ? (char)row[k] : '.');
    out_str(&tk_out, "|\n");
}

static int dump(reader *r, const wchar_t *name, uint64_t skip, uint64_t length, size_t width, void *buf, size_t cap) {
    if (skip) {
        uint64_t size;
        if (reader_size(r, &size) && skip > size) {
            err_text(TOOL, name, "the offset is past the end of the file");
            return TK_ERROR;
        }
        if (!reader_skip_to(r, skip)) return err_win(TOOL, name, "cannot move to the offset", r->error), TK_ERROR;
        if (r->offset < skip) return err_text(TOOL, name, "the offset is past the end of the input"), TK_ERROR;
    }
    unsigned char row[MAX_WIDTH];
    size_t filled = 0;
    uint64_t at = skip, left = length;
    for (;;) {
        size_t want = left < cap ? (size_t)left : cap, got;
        if (!want) break;
        if (!reader_read(r, buf, want, &got)) {
            return err_read(TOOL, name, r->offset, r->error), TK_ERROR;
        }
        if (!got) break;
        left -= got;
        const unsigned char *p = buf;
        for (size_t i = 0; i < got; i++) {
            row[filled++] = p[i];
            if (filled < width) continue;
            emit_row(at, row, width, width);
            at += width, filled = 0;
        }
        if (tk_out.failed) return TK_ERROR;
    }
    if (filled) emit_row(at, row, filled, width);
    return TK_OK;
}

static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = ascii_lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* Turns a dump back into bytes on dest. */
static int reverse(reader *r, const wchar_t *name, stream *dest) {
    liner li;
    liner_start(&li, r);
    const char *view;
    size_t len;
    int rc, result = TK_OK;
    bool first = true;
    uint64_t expected = 0;
    while ((rc = liner_next(&li, &view, &len)) > 0) {
        const unsigned char *s = (const unsigned char *)view, *end = s + len;
        if (!len) continue;
        uint64_t offset = 0;
        size_t digits = 0;
        for (; s < end && hex_digit(*s) >= 0; s++, digits++) {
            if (digits == 16) break;
            offset = offset << 4 | (uint64_t)hex_digit(*s);
        }
        if (!digits || digits > 16 || (s < end && *s != ' ' && *s != ':')) {
            line_error(name, li.number, "does not start with a hex offset");
            result = TK_ERROR;
            break;
        }
        if (!first && offset != expected) {
            line_error(name, li.number, "the offset does not follow on from the line before");
            result = TK_ERROR;
            break;
        }
        if (s < end && *s == ':') s++;
        uint64_t count = 0;
        for (;;) {
            while (s < end && *s == ' ') s++;
            if (s == end || *s == '|') break;
            int hi = hex_digit(*s), lo = s + 1 < end ? hex_digit(s[1]) : -1;
            if (hi < 0 || lo < 0 || (s + 2 < end && s[2] != ' ' && s[2] != '|')) {
                line_error(name, li.number, "expected two hex digits per byte");
                result = TK_ERROR;
                break;
            }
            char byte = (char)(hi << 4 | lo);
            out_put(dest, &byte, 1);
            count++, s += 2;
        }
        if (result) break;
        first = false, expected = offset + count;
        if (dest->failed) break;
    }
    if (rc < 0 && !result) err_win(TOOL, name, "cannot read the dump", r->error), result = TK_ERROR;
    liner_free(&li);
    return result;
}

int im_main(void) {
    out_init();
    cmdline cmd;
    if (!cmdline_get(&cmd)) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    static const optspec specs[] = {
        {L's', L"skip", true, 1},    {L'n', L"length", true, 2}, {L'w', L"width", true, 3},
        {L'r', L"reverse", false, 4}, {L'o', L"output", true, 5}, {L'h', L"help", false, 6},
        {0, L"version", false, 7},
    };
    optparse op;
    opt_start(&op, &cmd, specs, sizeof specs / sizeof specs[0], TOOL);
    uint64_t skip = 0, length = UINT64_MAX, width = 16;
    bool rev = false, dump_opts = false;
    const wchar_t *output = NULL, *input = NULL, *value;
    for (int id; (id = opt_next(&op, &value)) != OPT_END;) {
        switch (id) {
        case OPT_BAD: return finish(TOOL, TK_ERROR);
        case OPT_OPERAND:
            if (input) return err_text(TOOL, value, "only one file can be dumped"), TK_ERROR;
            input = value;
            break;
        case 1: dump_opts = true; if (!parse_u64(value, &skip)) return err_text(TOOL, value, "not a valid offset"), TK_ERROR; break;
        case 2: dump_opts = true; if (!parse_u64(value, &length)) return err_text(TOOL, value, "not a valid length"), TK_ERROR; break;
        case 3:
            dump_opts = true;
            if (!parse_u64(value, &width) || width < 1 || width > MAX_WIDTH) return err_text(TOOL, value, "the width must be 1 to 256"), TK_ERROR;
            break;
        case 4: rev = true; break;
        case 5: output = value; break;
        case 6: usage(); return finish(TOOL, TK_OK);
        case 7: print_version(TOOL); return finish(TOOL, TK_OK);
        }
    }
    if (rev && dump_opts) return err_text(TOOL, NULL, "-s, -n and -w do not apply with -r"), TK_ERROR;
    if (output && !rev) return err_text(TOOL, NULL, "-o needs -r"), TK_ERROR;

    reader r;
    if (!input || (input[0] == L'-' && !input[1])) {
        reader_stdin(&r);
        input = L"-";
    } else if (!reader_open(&r, input)) {
        if (r.error == ERROR_DIRECTORY) err_text(TOOL, input, "is a folder, not a file (error 267)");
        else err_win(TOOL, input, NULL, r.error);
        return TK_ERROR;
    }
    size_t cap = (size_t)1 << 16;
    void *buf = tk_alloc(cap);
    if (!buf) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    int result;
    if (!rev) {
        result = dump(&r, input, skip, length, (size_t)width, buf, cap);
        reader_close(&r);
        return finish(TOOL, result);
    }
    if (!output) {
        result = reverse(&r, input, &tk_out);
        reader_close(&r);
        return finish(TOOL, result);
    }
    /* To a file: write beside it, then rename over it once everything is down. */
    wbuf temp = {0};
    if (!wbuf_set(&temp, output) || !wbuf_add(&temp, L".thex-tmp", 9)) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    wchar_t *temp_native = fs_native(temp.s), *out_native = fs_native(output);
    HANDLE h = temp_native ? CreateFileW(temp_native, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) return err_win(TOOL, output, "cannot create the output", GetLastError()), TK_ERROR;
    static stream file;
    out_attach(&file, h);
    result = reverse(&r, input, &file);
    reader_close(&r);
    bool written = out_flush(&file);
    DWORD error = file.error;
    if (!CloseHandle(h) && written) written = false, error = GetLastError();
    if (!written) err_win(TOOL, output, "cannot write the output", error), result = TK_ERROR;
    if (result == TK_OK && !MoveFileExW(temp_native, out_native, MOVEFILE_REPLACE_EXISTING)) {
        err_win(TOOL, output, "cannot replace the output", GetLastError());
        result = TK_ERROR;
    }
    if (result != TK_OK) DeleteFileW(temp_native);
    return finish(TOOL, result);
}
