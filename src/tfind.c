/* tfind: find text in files and folders. */
#include <impossible/base.h>
#include "../common/cmdline.h"
#include "../common/fs.h"
#include "../common/out.h"
#include "../common/text.h"

#define TOOL "tfind"
#define MAX_PATTERN 65536
#define MAX_LINE ((size_t)64 << 20)
#define FIRST_LOOK 65536 /* a NUL in this much of the start makes a file binary */

static unsigned char pattern[MAX_PATTERN];
static size_t plen, skip[256];
static bool fold, line_numbers = true, list_only, quiet;
static bool found, had_error, stop;
static unsigned char *buf;
static size_t buf_cap = (size_t)1 << 20;

static void usage(void) {
    out_str(&tk_out,
        "Usage: tfind [OPTION]... TEXT [PATH]...\n"
        "\n"
        "Prints the lines that contain TEXT, as \"path:line:text\". A PATH that is a folder is searched\n"
        "in full, folders inside it too, except symbolic links and junctions, which are not entered.\n"
        "With no PATH, or a PATH of -, standard input is searched.\n"
        "\n"
        "  -i, --ignore-case     A to Z match a to z (ASCII only)\n"
        "  -N, --no-line-number  print \"path:text\"\n"
        "  -l, --list            print only the names of files that match\n"
        "  -q, --quiet           print nothing; stop at the first match\n"
        "  -h, --help            show this help\n"
        "      --version         show the version\n"
        "\n"
        "TEXT is a fixed string and is matched against UTF-8 bytes. A file with a NUL byte in its first\n"
        "64 KiB is binary: a match is reported as \"path: binary file matches\" and the bytes are not\n"
        "printed. UTF-16 files (with a byte order mark) are not searched and count as an error.\n"
        "Lines may be up to 64 MiB long.\n"
        "\n"
        "Exit status: 0 a match was found, 1 none, 2 an error (even when something matched).\n");
}

static unsigned char fold_byte(unsigned char c) { return fold ? ascii_lower(c) : c; }

static void prepare(void) {
    for (size_t i = 0; i < plen; i++) pattern[i] = fold_byte(pattern[i]);
    for (size_t i = 0; i < 256; i++) skip[i] = plen;
    for (size_t i = 0; i + 1 < plen; i++) skip[pattern[i]] = plen - 1 - i;
}

/* Horspool search; returns the offset of the first match or SIZE_MAX. */
static size_t find(const unsigned char *t, size_t n) {
    for (size_t i = 0; i + plen <= n;) {
        size_t j = plen;
        while (j && fold_byte(t[i + j - 1]) == pattern[j - 1]) j--;
        if (!j) return i;
        i += skip[fold_byte(t[i + plen - 1])];
    }
    return SIZE_MAX;
}

static bool has_nul(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!p[i]) return true;
    return false;
}

static void read_error(const wchar_t *name, reader *r) {
    err_read(TOOL, name, r->offset, r->error);
    had_error = true;
}

/* Reports a match in a file once; true when the file needs no more reading. */
static bool report_file(const wchar_t *name, bool binary) {
    found = true;
    if (quiet) return stop = true;
    if (list_only || binary) {
        out_wide(&tk_out, name);
        out_str(&tk_out, list_only ? "\n" : ": binary file matches\n");
        return true;
    }
    return false;
}

static void search_binary(reader *r, const wchar_t *name, size_t len) {
    for (;;) {
        if (find(buf, len) != SIZE_MAX) {
            report_file(name, true);
            return;
        }
        size_t keep = plen - 1 < len ? plen - 1 : len;
        for (size_t i = 0; i < keep; i++) buf[i] = buf[len - keep + i];
        size_t got;
        if (!reader_read(r, buf + keep, buf_cap - keep, &got)) return read_error(name, r);
        if (!got) return;
        len = keep + got;
    }
}

static void search_reader(reader *r, const wchar_t *name) {
    size_t len = 0, start = 0, scanned = 0;
    uint64_t number = 0;
    bool first = true, eof = false;
    while (!eof && !stop) {
        size_t got;
        if (!reader_read(r, buf + len, buf_cap - len, &got)) return read_error(name, r);
        if (!got) eof = true;
        len += got;
        if (first && len) {
            first = false;
            if (len >= 2 && ((buf[0] == 0xFF && buf[1] == 0xFE) || (buf[0] == 0xFE && buf[1] == 0xFF))) {
                err_text(TOOL, name, "UTF-16 text is not searched");
                had_error = true;
                return;
            }
            if (has_nul(buf, len < FIRST_LOOK ? len : FIRST_LOOK)) return search_binary(r, name, len);
        }
        for (size_t i = scanned; i <= len; i++) {
            if (i < len && buf[i] != '\n') continue;
            if (i == len && !(eof && start < len)) break; /* the last line has no newline */
            size_t end = i;
            if (end > start && buf[end - 1] == '\r') end--;
            number++;
            if (find(buf + start, end - start) != SIZE_MAX) {
                /* A matching line holding a NUL is binary data: report it like a binary file. */
                bool binary = has_nul(buf + start, end - start);
                if (report_file(name, binary) || stop) return;
                out_wide(&tk_out, name);
                out_char(&tk_out, ':');
                if (line_numbers) out_u64(&tk_out, number), out_char(&tk_out, ':');
                out_put(&tk_out, buf + start, end - start);
                out_char(&tk_out, '\n');
                if (tk_out.failed) return (void)(stop = true);
            }
            start = i < len ? i + 1 : len;
        }
        scanned = len;
        if (start) {
            for (size_t i = start; i < len; i++) buf[i - start] = buf[i];
            len -= start, scanned -= start, start = 0;
        } else if (len == buf_cap && !eof) {
            if (buf_cap >= MAX_LINE) {
                char text[64] = "line ";
                size_t n = 5 + format_u64(number + 1, text + 5);
                for (const char *p = " is longer than 64 MiB"; *p; p++) text[n++] = *p;
                text[n] = 0;
                err_text(TOOL, name, text);
                had_error = true;
                return;
            }
            unsigned char *grown = tk_realloc(buf, buf_cap * 2);
            if (!grown) return err_text(TOOL, name, "out of memory"), (void)(had_error = true);
            buf = grown, buf_cap *= 2;
        }
    }
}

static void search_file(const wchar_t *path) {
    reader r;
    if (!reader_open(&r, path)) {
        if (r.error == ERROR_DIRECTORY) err_text(TOOL, path, "is a folder");
        else err_win(TOOL, path, NULL, r.error);
        had_error = true;
        return;
    }
    search_reader(&r, path);
    reader_close(&r);
}

typedef struct {
    dirlist list;
    size_t next, path_len;
} frame;

/* Folders are walked with a list of frames on the heap, so depth is limited by memory, not the stack. */
static void search_folder(const wchar_t *root) {
    wbuf path = {0};
    frame *frames = NULL;
    size_t count = 0, cap = 0;
    DWORD error;
    if (!wbuf_set(&path, root)) return err_text(TOOL, NULL, "out of memory"), (void)(had_error = true);
    for (bool entering = true;;) {
        if (entering) {
            entering = false;
            frame f = {.path_len = path.len};
            if (!dir_read(path.s, &f.list, &error)) {
                err_win(TOOL, path.s, "cannot list the folder", error);
                had_error = true;
            } else {
                if (count == cap) {
                    frame *grown = tk_realloc(frames, (cap ? cap * 2 : 16) * sizeof(frame));
                    if (!grown) { err_text(TOOL, path.s, "out of memory"), had_error = true, stop = true; dir_free(&f.list); break; }
                    frames = grown, cap = cap ? cap * 2 : 16;
                }
                frames[count++] = f;
            }
        }
        if (!count || stop) break;
        frame *f = &frames[count - 1];
        if (f->next == f->list.count) {
            dir_free(&f->list);
            count--;
            continue;
        }
        const entry *e = &f->list.items[f->next++];
        wbuf_cut(&path, f->path_len);
        if (!wbuf_join(&path, e->name)) { err_text(TOOL, NULL, "out of memory"), had_error = true, stop = true; break; }
        if (ENTRY_IS_DIR(e)) entering = !ENTRY_IS_DIR_LINK(e);
        else search_file(path.s);
    }
    for (size_t i = 0; i < count; i++) dir_free(&frames[i].list);
    tk_free(frames);
    wbuf_free(&path);
}

int im_main(void) {
    out_init();
    cmdline cmd;
    if (!cmdline_get(&cmd)) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    static const optspec specs[] = {
        {L'i', L"ignore-case", false, 1}, {L'N', L"no-line-number", false, 2}, {L'l', L"list", false, 3},
        {L'q', L"quiet", false, 4},       {L'h', L"help", false, 5},           {0, L"version", false, 6},
    };
    optparse op;
    opt_start(&op, &cmd, specs, sizeof specs / sizeof specs[0], TOOL);
    const wchar_t *text = NULL, *value;
    const wchar_t **paths = tk_alloc(((size_t)cmd.argc + 1) * sizeof(wchar_t *));
    size_t npaths = 0;
    if (!paths) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    for (int id; (id = opt_next(&op, &value)) != OPT_END;) {
        switch (id) {
        case OPT_BAD: return finish(TOOL, TK_ERROR);
        case OPT_OPERAND:
            if (!text) text = value;
            else paths[npaths++] = value;
            break;
        case 1: fold = true; break;
        case 2: line_numbers = false; break;
        case 3: list_only = true; break;
        case 4: quiet = true; break;
        case 5: usage(); return finish(TOOL, TK_OK);
        case 6: print_version(TOOL); return finish(TOOL, TK_OK);
        }
    }
    if (!text) return err_text(TOOL, NULL, "no text to look for (try --help)"), TK_ERROR;
    size_t bytes = utf16_to_utf8(text, tk_wlen(text), NULL);
    if (!bytes) return err_text(TOOL, NULL, "the text to look for is empty"), TK_ERROR;
    if (bytes > MAX_PATTERN) return err_text(TOOL, NULL, "the text to look for is longer than 64 KiB"), TK_ERROR;
    utf16_to_utf8(text, tk_wlen(text), (char *)pattern);
    for (size_t i = 0; i < bytes; i++)
        if (pattern[i] == '\n') return err_text(TOOL, NULL, "the text to look for may not contain a line break"), TK_ERROR;
    plen = bytes;
    prepare();
    buf = tk_alloc(buf_cap);
    if (!buf) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;

    if (!npaths) paths[npaths++] = L"-";
    for (size_t i = 0; i < npaths && !stop; i++) {
        const wchar_t *p = paths[i];
        if (p[0] == L'-' && !p[1]) {
            reader r;
            reader_stdin(&r);
            search_reader(&r, L"(stdin)");
            continue;
        }
        DWORD error;
        DWORD attrs = fs_attributes(p, &error);
        if (attrs == INVALID_FILE_ATTRIBUTES) err_win(TOOL, p, NULL, error), had_error = true;
        else if (attrs & FILE_ATTRIBUTE_DIRECTORY) search_folder(p);
        else search_file(p);
    }
    return finish(TOOL, had_error || tk_out.failed ? TK_ERROR : found ? TK_OK : TK_NO);
}
