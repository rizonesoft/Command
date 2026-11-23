/* thash: SHA-256, SHA-1, MD5, CRC-32 and BLAKE3 of files and standard input, in one pass. */
#include <impossible/base.h>
#include "../common/cmdline.h"
#include "../common/fs.h"
#include "../common/hash/blake3.h"
#include "../common/hash/crc32.h"
#include "../common/hash/md5.h"
#include "../common/hash/sha1.h"
#include "../common/hash/sha256.h"
#include "../common/lines.h"
#include "../common/out.h"
#include "../common/text.h"

#define TOOL "thash"

enum { SHA256 = 1, SHA1 = 2, MD5 = 4, CRC32 = 8, BLAKE3 = 16, ALL = 31 };
static const struct { unsigned bit; const char *tag, *name; size_t len; } ALGO[] = {
    {SHA256, "SHA256", "sha256", 32}, {SHA1, "SHA1", "sha1", 20}, {MD5, "MD5", "md5", 16},
    {CRC32, "CRC32", "crc32", 4},     {BLAKE3, "BLAKE3", "blake3", 32},
};
#define NALGO (sizeof ALGO / sizeof ALGO[0])

typedef struct {
    unsigned mask;
    sha256_ctx sha256;
    sha1_ctx sha1;
    md5_ctx md5;
    crc32_ctx crc;
    blake3_ctx blake3;
} hasher;

typedef struct { uint8_t bytes[NALGO][32]; } digests;

static void hasher_start(hasher *h, unsigned mask) {
    h->mask = mask;
    if (mask & SHA256) sha256_init(&h->sha256);
    if (mask & SHA1) sha1_init(&h->sha1);
    if (mask & MD5) md5_init(&h->md5);
    if (mask & CRC32) crc32_init(&h->crc);
    if (mask & BLAKE3) blake3_init(&h->blake3);
}

static void hasher_feed(hasher *h, const void *data, size_t n) {
    if (h->mask & SHA256) sha256_update(&h->sha256, data, n);
    if (h->mask & SHA1) sha1_update(&h->sha1, data, n);
    if (h->mask & MD5) md5_update(&h->md5, data, n);
    if (h->mask & CRC32) crc32_update(&h->crc, data, n);
    if (h->mask & BLAKE3) blake3_update(&h->blake3, data, n);
}

static void hasher_end(hasher *h, digests *d) {
    if (h->mask & SHA256) sha256_final(&h->sha256, d->bytes[0]);
    if (h->mask & SHA1) sha1_final(&h->sha1, d->bytes[1]);
    if (h->mask & MD5) md5_final(&h->md5, d->bytes[2]);
    if (h->mask & CRC32) {
        uint32_t v = crc32_final(&h->crc);
        for (int i = 0; i < 4; i++) d->bytes[3][i] = (uint8_t)(v >> (24 - 8 * i));
    }
    if (h->mask & BLAKE3) blake3_final(&h->blake3, d->bytes[4]);
}

static const size_t BUFFER = (size_t)1 << 20;

/* Hashes everything the reader holds. On a read error the path, offset and Windows error are reported. */
static bool hash_reader(reader *r, const wchar_t *name, unsigned mask, void *buf, digests *d) {
    hasher h;
    hasher_start(&h, mask);
    for (;;) {
        size_t got;
        if (!reader_read(r, buf, BUFFER, &got)) {
            err_read(TOOL, name, r->offset, r->error);
            return false;
        }
        if (!got) break;
        hasher_feed(&h, buf, got);
    }
    hasher_end(&h, d);
    return true;
}

static bool open_source(reader *r, const wchar_t *name) {
    if (name[0] == L'-' && !name[1]) return reader_stdin(r), true;
    if (reader_open(r, name)) return true;
    if (r->error == ERROR_DIRECTORY) err_text(TOOL, name, "is a folder, not a file (error 267)");
    else err_win(TOOL, name, NULL, r->error);
    return false;
}

static bool print_name_ok(const wchar_t *name) {
    for (const wchar_t *p = name; *p; p++)
        if (*p == L'\n' || *p == L'\r') return err_text(TOOL, name, "a line break in a file name cannot be written to a checksum list"), false;
    return true;
}

static void print_digests(const wchar_t *name, unsigned mask, bool tagged, const digests *d) {
    for (size_t i = 0; i < NALGO; i++) {
        if (!(mask & ALGO[i].bit)) continue;
        if (tagged) {
            out_str(&tk_out, ALGO[i].tag), out_str(&tk_out, " ("), out_wide(&tk_out, name), out_str(&tk_out, ") = ");
            out_hex(&tk_out, d->bytes[i], ALGO[i].len);
        } else {
            out_hex(&tk_out, d->bytes[i], ALGO[i].len), out_str(&tk_out, "  "), out_wide(&tk_out, name);
        }
        out_char(&tk_out, '\n');
    }
}

static bool wide_equal_fold(const wchar_t *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (wascii_lower(*a) != ascii_lower((unsigned char)*b)) return false;
    return !*a && !*b;
}

static bool parse_algorithms(const wchar_t *list, unsigned *mask) {
    *mask = 0;
    while (*list) {
        wchar_t word[16];
        size_t n = 0;
        while (*list && *list != L',') {
            if (*list != L'-' && n < 15) word[n++] = *list;
            list++;
        }
        word[n] = 0;
        if (*list == L',') list++;
        unsigned bit = 0;
        if (wide_equal_fold(word, "all")) bit = ALL;
        for (size_t i = 0; i < NALGO; i++)
            if (wide_equal_fold(word, ALGO[i].name)) bit = ALGO[i].bit;
        if (!bit) return err_text(TOOL, word, "unknown algorithm (sha256, sha1, md5, crc32, blake3 or all)"), false;
        *mask |= bit;
    }
    return *mask != 0;
}

static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = ascii_lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

typedef struct {
    size_t algo;
    uint8_t want[32];
    const char *name; /* UTF-8, NUL terminated, inside the line buffer */
} listed;

static bool tag_matches(const char *line, size_t len, const char *tag) {
    size_t k = 0;
    for (size_t i = 0; i < len; i++) {
        if (line[i] == '-') continue; /* SHA-256 and SHA256 are the same tag */
        if (!tag[k] || ascii_lower((unsigned char)line[i]) != ascii_lower((unsigned char)tag[k])) return false;
        k++;
    }
    return !tag[k];
}

/* Fills out from "ALGORITHM (name) = hex" or "hex  name" (two spaces, or a space and *). The name is
   cut off in place with a NUL. */
static bool parse_check_line(char *line, size_t len, unsigned plain_mask, listed *out) {
    char *name, *hex;
    size_t name_len, hex_len, tag_len = 0;
    while (tag_len < len && line[tag_len] != ' ') tag_len++;
    out->algo = NALGO;
    if (tag_len + 1 < len && line[tag_len + 1] == '(')
        for (size_t i = 0; i < NALGO; i++)
            if (tag_matches(line, tag_len, ALGO[i].tag)) out->algo = i;
    if (out->algo != NALGO) {
        size_t close = SIZE_MAX;
        for (size_t i = len; i-- > tag_len + 2;)
            if (i + 4 <= len && line[i] == ')' && line[i + 1] == ' ' && line[i + 2] == '=' && line[i + 3] == ' ') {
                close = i;
                break;
            }
        if (close == SIZE_MAX) return false;
        name = line + tag_len + 2, name_len = close - (tag_len + 2);
        hex = line + close + 4, hex_len = len - (close + 4);
    } else {
        hex_len = 0;
        while (hex_len < len && hex_digit((unsigned char)line[hex_len]) >= 0) hex_len++;
        if (hex_len + 2 >= len || line[hex_len] != ' ' || (line[hex_len + 1] != ' ' && line[hex_len + 1] != '*')) return false;
        for (size_t i = 0; i < NALGO; i++)
            if (ALGO[i].len * 2 == hex_len && (hex_len != 64 || (plain_mask & ALGO[i].bit))) out->algo = i;
        hex = line, name = line + hex_len + 2, name_len = len - hex_len - 2;
    }
    if (out->algo == NALGO || hex_len != ALGO[out->algo].len * 2 || !name_len) return false;
    for (size_t i = 0; i < hex_len; i++)
        if (hex_digit((unsigned char)hex[i]) < 0) return false;
    for (size_t i = 0; i < hex_len / 2; i++)
        out->want[i] = (uint8_t)(hex_digit((unsigned char)hex[2 * i]) << 4 | hex_digit((unsigned char)hex[2 * i + 1]));
    name[name_len] = 0;
    out->name = name;
    return true;
}

static bool equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
    return !diff;
}

static int check(const wchar_t *list_path, unsigned plain_mask, bool quiet, void *buf) {
    reader lr;
    if (list_path[0] == L'-' && !list_path[1]) reader_stdin(&lr);
    else if (!reader_open(&lr, list_path)) return err_win(TOOL, list_path, NULL, lr.error), TK_ERROR;
    liner li;
    liner_start(&li, &lr);
    uint64_t ok = 0, mismatched = 0, unreadable = 0, malformed = 0;
    bool list_is_stdin = list_path[0] == L'-' && !list_path[1];
    int stdin_state = 0; /* 0 not read yet, 1 read, 2 failed */
    digests stdin_digests;
    const char *view;
    size_t len;
    int rc;
    while ((rc = liner_next(&li, &view, &len)) > 0) {
        char *line = (char *)view;
        listed item;
        if (!len || line[0] == '#') continue;
        /* A NUL would cut the name short and check a different file than the one listed. */
        if (tk_slen(line) != len || !parse_check_line(line, len, plain_mask, &item)) {
            out_flush(&tk_out);
            out_str(&tk_err, TOOL ": "), out_wide(&tk_err, list_path), out_str(&tk_err, ": line ");
            out_u64(&tk_err, li.number), out_str(&tk_err, ": improperly formatted checksum line\n");
            out_flush(&tk_err);
            malformed++;
            continue;
        }
        size_t wn = utf8_to_utf16(item.name, tk_slen(item.name), NULL, false);
        wchar_t *wname = wn == SIZE_MAX ? NULL : tk_alloc((wn + 1) * sizeof(wchar_t));
        bool good = false, readable = false;
        if (wname) {
            utf8_to_utf16(item.name, tk_slen(item.name), wname, false);
            digests d = {0};
            bool is_stdin = wname[0] == L'-' && !wname[1];
            if (is_stdin && list_is_stdin) {
                err_text(TOOL, wname, "standard input is the list, so it cannot also be a file to check");
            } else if (is_stdin && stdin_state) {
                /* Standard input can be read once: every algorithm is computed on the first record that
                   names it, and the others use that. */
                if (stdin_state == 1) readable = true, d = stdin_digests;
            } else {
                reader fr;
                unsigned mask = is_stdin ? ALL : ALGO[item.algo].bit;
                if (open_source(&fr, wname)) {
                    readable = hash_reader(&fr, wname, mask, buf, &d);
                    reader_close(&fr);
                    if (is_stdin) stdin_state = readable ? 1 : 2, stdin_digests = d;
                }
            }
            good = readable && equal(d.bytes[item.algo], item.want, ALGO[item.algo].len);
        } else {
            err_text8(TOOL, item.name, "the name in the list is not valid UTF-8");
        }
        tk_free(wname);
        if (good) {
            ok++;
            if (!quiet) out_str(&tk_out, item.name), out_str(&tk_out, ": OK\n");
        } else if (readable) {
            mismatched++;
            out_str(&tk_out, item.name), out_str(&tk_out, ": FAILED\n");
        } else {
            unreadable++;
            out_str(&tk_out, item.name), out_str(&tk_out, ": FAILED open or read\n");
        }
        if (tk_out.failed) break;
    }
    if (rc < 0) err_win(TOOL, list_path, "cannot read the checksum list", lr.error), malformed++;
    liner_free(&li);
    reader_close(&lr);
    if (!ok && !mismatched && !unreadable && !malformed) err_text(TOOL, list_path, "no checksum lines found"), malformed++;
    char n[24];
    if (mismatched) {
        format_u64(mismatched, n);
        out_flush(&tk_out), out_str(&tk_err, TOOL ": WARNING: "), out_str(&tk_err, n), out_str(&tk_err, " computed checksum(s) did NOT match\n");
    }
    if (unreadable) {
        format_u64(unreadable, n);
        out_flush(&tk_out), out_str(&tk_err, TOOL ": WARNING: "), out_str(&tk_err, n), out_str(&tk_err, " listed file(s) could not be read\n");
    }
    if (malformed) {
        format_u64(malformed, n);
        out_flush(&tk_out), out_str(&tk_err, TOOL ": WARNING: "), out_str(&tk_err, n), out_str(&tk_err, " line(s) could not be used\n");
    }
    out_flush(&tk_err);
    if (unreadable || malformed) return TK_ERROR;
    return mismatched ? TK_NO : TK_OK;
}

static void usage(void) {
    out_str(&tk_out,
        "Usage: thash [OPTION]... [FILE]...\n"
        "       thash -c LIST [OPTION]...\n"
        "\n"
        "Prints checksums of each FILE, or of standard input when there is none or FILE is -.\n"
        "All the chosen algorithms are computed in one pass over the data.\n"
        "\n"
        "  -a, --algorithm LIST  comma separated: sha256 (default), sha1, md5, crc32, blake3, all\n"
        "      --tag             print \"SHA256 (name) = hash\" lines; always used with more than one algorithm\n"
        "  -c, --check LIST      read checksums from the file LIST (- for standard input) and check each file\n"
        "  -q, --quiet           with -c, print only the files that fail\n"
        "  -h, --help            show this help\n"
        "      --version         show the version\n"
        "\n"
        "Lines are \"hash  name\" or \"ALGORITHM (name) = hash\". With -c, a plain 64 digit hash is read as\n"
        "SHA-256, or as BLAKE3 when -a blake3 is given; other lengths identify themselves.\n"
        "\n"
        "Exit status: 0 success, 1 a checksum did not match, 2 an error (unreadable file, bad line, bad usage).\n");
}

int im_main(void) {
    out_init();
    cmdline cmd;
    if (!cmdline_get(&cmd)) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    static const optspec specs[] = {
        {L'a', L"algorithm", true, 1}, {L'c', L"check", true, 2}, {L'q', L"quiet", false, 3},
        {0, L"tag", false, 4},         {L'h', L"help", false, 5}, {0, L"version", false, 6},
    };
    optparse op;
    opt_start(&op, &cmd, specs, sizeof specs / sizeof specs[0], TOOL);
    unsigned mask = SHA256;
    bool algo_given = false, tag = false, quiet = false;
    const wchar_t *list = NULL, *value;
    const wchar_t **files = tk_alloc(((size_t)cmd.argc + 1) * sizeof(wchar_t *));
    size_t nfiles = 0;
    if (!files) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    for (int id; (id = opt_next(&op, &value)) != OPT_END;) {
        switch (id) {
        case OPT_BAD: return finish(TOOL, TK_ERROR);
        case OPT_OPERAND: files[nfiles++] = value; break;
        case 1:
            if (!parse_algorithms(value, &mask)) return finish(TOOL, TK_ERROR);
            algo_given = true;
            break;
        case 2: list = value; break;
        case 3: quiet = true; break;
        case 4: tag = true; break;
        case 5: usage(); return finish(TOOL, TK_OK);
        case 6: print_version(TOOL); return finish(TOOL, TK_OK);
        }
    }
    void *buf = tk_alloc(BUFFER);
    if (!buf) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    if (list) {
        if (nfiles) return err_text(TOOL, files[0], "file names cannot be given with -c"), TK_ERROR;
        return finish(TOOL, check(list, algo_given && mask == BLAKE3 ? BLAKE3 : SHA256, quiet, buf));
    }
    if (quiet) return err_text(TOOL, NULL, "-q needs -c"), TK_ERROR;
    size_t dashes = 0;
    for (size_t i = 0; i < nfiles; i++) dashes += files[i][0] == L'-' && !files[i][1];
    if (dashes > 1) return err_text(TOOL, NULL, "standard input can be named only once (-)"), TK_ERROR;
    bool tagged = tag || (mask & (mask - 1));
    static const wchar_t *const dash[] = {L"-"};
    const wchar_t *const *list_of = nfiles ? files : dash;
    size_t count = nfiles ? nfiles : 1;
    bool failed = false;
    for (size_t i = 0; i < count && !tk_out.failed; i++) {
        reader r;
        digests d;
        if (!open_source(&r, list_of[i])) {
            failed = true;
            continue;
        }
        bool ok = print_name_ok(list_of[i]) && hash_reader(&r, list_of[i], mask, buf, &d);
        reader_close(&r);
        if (ok) print_digests(list_of[i], mask, tagged, &d);
        else failed = true;
    }
    return finish(TOOL, failed ? TK_ERROR : TK_OK);
}
