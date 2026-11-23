/* tcmp: compare two files, or two folders. */
#include <impossible/base.h>
#include "../common/cmdline.h"
#include "../common/fs.h"
#include "../common/out.h"
#include "../common/text.h"

#define TOOL "tcmp"
#define CHUNK ((size_t)1 << 20)

static unsigned char *buf_a, *buf_b;
static bool all_diffs, quiet, verbose, had_error;

static void usage(void) {
    out_str(&tk_out,
        "Usage: tcmp [OPTION]... FIRST SECOND\n"
        "\n"
        "Two files: reports the first byte that differs, or every run of differing bytes with -a.\n"
        "Two folders: lists what is only in one, what differs and (with -v) what is the same, then a total.\n"
        "Files in folders are the same when their sizes and every byte match. Symbolic links and junctions\n"
        "that are folders are listed as skipped and not entered.\n"
        "\n"
        "  -a, --all       two files: report every run of differing bytes\n"
        "  -v, --verbose   two folders: also list files that are the same\n"
        "  -q, --quiet     print nothing, only set the exit status\n"
        "  -h, --help      show this help\n"
        "      --version   show the version\n"
        "\n"
        "Exit status: 0 identical, 1 different (or something skipped), 2 an error.\n");
}

static bool fill(reader *r, const wchar_t *name, unsigned char *buf, size_t *got) {
    size_t have = 0;
    while (have < CHUNK) {
        size_t n;
        if (!reader_read(r, buf + have, CHUNK - have, &n)) {
            err_read(TOOL, name, r->offset, r->error);
            return false;
        }
        if (!n) break;
        have += n;
    }
    *got = have;
    return true;
}

typedef struct {
    bool differ;
    uint64_t size_a, size_b;
    uint64_t first, bytes, runs;
    unsigned char va, vb;
    bool byte_differs;
} result;

static void hex_line(const char *label, uint64_t n) {
    out_str(&tk_out, label), out_u64(&tk_out, n), out_str(&tk_out, " (0x"), out_hex_value(&tk_out, n, 1), out_char(&tk_out, ')');
}

static void run_line(uint64_t start, uint64_t length) {
    if (quiet) return;
    hex_line("differ at offset ", start);
    out_str(&tk_out, ": "), out_u64(&tk_out, length), out_str(&tk_out, length == 1 ? " byte\n" : " bytes\n");
}

/* Compares two open files. False after reporting a read error. */
static bool compare_streams(reader *a, reader *b, const wchar_t *na, const wchar_t *nb, bool first_only, result *res) {
    *res = (result){0};
    if (!reader_size(a, &res->size_a) || !reader_size(b, &res->size_b)) res->size_a = res->size_b = 0;
    uint64_t base = 0, run_start = 0, run_len = 0;
    for (;;) {
        size_t ga, gb;
        if (!fill(a, na, buf_a, &ga) || !fill(b, nb, buf_b, &gb)) return false;
        size_t n = ga < gb ? ga : gb;
        size_t i = 0;
        while (i < n) {
            /* Equal words are skipped eight bytes at a time. */
            if (!run_len) {
                while (i + 8 <= n) {
                    uint64_t x, y;
                    memcpy(&x, buf_a + i, 8), memcpy(&y, buf_b + i, 8);
                    if (x != y) break;
                    i += 8;
                }
            }
            if (i >= n) break;
            if (buf_a[i] != buf_b[i]) {
                if (!res->byte_differs) res->byte_differs = true, res->first = base + i, res->va = buf_a[i], res->vb = buf_b[i];
                if (first_only) return res->differ = true;
                if (!run_len) run_start = base + i, res->runs++;
                run_len++, res->bytes++;
            } else if (run_len) {
                run_line(run_start, run_len);
                run_len = 0;
            }
            i++;
        }
        base += n;
        if (ga != gb || !ga) break;
    }
    if (run_len) run_line(run_start, run_len);
    if (res->size_a != res->size_b) res->differ = true;
    if (res->byte_differs) res->differ = true;
    return true;
}

static bool open_pair(reader *a, reader *b, const wchar_t *pa, const wchar_t *pb) {
    if (!reader_open(a, pa)) return err_win(TOOL, pa, NULL, a->error), false;
    if (!reader_open(b, pb)) return err_win(TOOL, pb, NULL, b->error), reader_close(a), false;
    return true;
}

static int compare_two_files(const wchar_t *pa, const wchar_t *pb) {
    reader a, b;
    result r;
    if (!open_pair(&a, &b, pa, pb)) return TK_ERROR;
    bool ok = compare_streams(&a, &b, pa, pb, !all_diffs, &r);
    reader_close(&a), reader_close(&b);
    if (!ok) return TK_ERROR;
    if (!r.differ) {
        if (!quiet) out_wide(&tk_out, pa), out_str(&tk_out, " and "), out_wide(&tk_out, pb), out_str(&tk_out, " are identical\n");
        return TK_OK;
    }
    if (quiet) return TK_NO;
    if (!all_diffs && r.byte_differs) {
        hex_line("differ at offset ", r.first);
        out_str(&tk_out, ": 0x"), out_hex_value(&tk_out, r.va, 2), out_str(&tk_out, " vs 0x"), out_hex_value(&tk_out, r.vb, 2);
        out_char(&tk_out, '\n');
        return TK_NO;
    }
    if (r.size_a != r.size_b) {
        out_str(&tk_out, "differ in size: "), out_wide(&tk_out, pa), out_str(&tk_out, " is "), out_u64(&tk_out, r.size_a);
        out_str(&tk_out, " bytes, "), out_wide(&tk_out, pb), out_str(&tk_out, " is "), out_u64(&tk_out, r.size_b);
        out_str(&tk_out, r.byte_differs ? " bytes\n" : " bytes; the bytes they share are identical\n");
    }
    if (r.byte_differs) {
        out_u64(&tk_out, r.bytes), out_str(&tk_out, " bytes differ in "), out_u64(&tk_out, r.runs), out_str(&tk_out, r.runs == 1 ? " place\n" : " places\n");
    }
    return TK_NO;
}

typedef struct {
    dirlist a, b;
    size_t ia, ib, len_a, len_b, len_rel;
} frame;

static uint64_t same, different, only_a, only_b, skipped;

static void say(const char *label, const wchar_t *rel, const char *note) {
    if (quiet) return;
    out_str(&tk_out, label), out_wide(&tk_out, rel);
    if (note) out_str(&tk_out, note);
    out_char(&tk_out, '\n');
}

static bool push(frame **frames, size_t *count, size_t *cap, const wchar_t *pa, const wchar_t *pb, size_t la, size_t lb, size_t lr) {
    frame f = {.len_a = la, .len_b = lb, .len_rel = lr};
    DWORD error;
    if (!dir_read(pa, &f.a, &error)) return err_win(TOOL, pa, "cannot list the folder", error), had_error = true, true;
    if (!dir_read(pb, &f.b, &error)) return dir_free(&f.a), err_win(TOOL, pb, "cannot list the folder", error), had_error = true, true;
    if (*count == *cap) {
        frame *grown = tk_realloc(*frames, (*cap ? *cap * 2 : 16) * sizeof(frame));
        if (!grown) return dir_free(&f.a), dir_free(&f.b), false;
        *frames = grown, *cap = *cap ? *cap * 2 : 16;
    }
    (*frames)[(*count)++] = f;
    return true;
}

static int compare_two_folders(const wchar_t *root_a, const wchar_t *root_b) {
    wbuf pa = {0}, pb = {0}, rel = {0};
    frame *frames = NULL;
    size_t count = 0, cap = 0;
    bool ok = wbuf_set(&pa, root_a) && wbuf_set(&pb, root_b) && wbuf_set(&rel, L"") && push(&frames, &count, &cap, pa.s, pb.s, pa.len, pb.len, 0);
    while (ok && count && !tk_out.failed) {
        frame *f = &frames[count - 1];
        if (f->ia == f->a.count && f->ib == f->b.count) {
            dir_free(&f->a), dir_free(&f->b);
            count--;
            continue;
        }
        const entry *ea = f->ia < f->a.count ? &f->a.items[f->ia] : NULL, *eb = f->ib < f->b.count ? &f->b.items[f->ib] : NULL;
        int order = !ea ? 1 : !eb ? -1 : wname_fold_compare(ea->name, eb->name);
        size_t la = f->len_a, lb = f->len_b, lr = f->len_rel;
        const wchar_t *name = order <= 0 ? ea->name : eb->name;
        wbuf_cut(&rel, lr), wbuf_cut(&pa, la), wbuf_cut(&pb, lb);
        ok = wbuf_join(&rel, name) && wbuf_join(&pa, name) && wbuf_join(&pb, order == 0 ? eb->name : name);
        if (!ok) break;
        if (order < 0) {
            f->ia++, only_a++;
            say("only in first:  ", rel.s, ENTRY_IS_DIR(ea) ? "\\" : NULL);
        } else if (order > 0) {
            f->ib++, only_b++;
            say("only in second: ", rel.s, ENTRY_IS_DIR(eb) ? "\\" : NULL);
        } else {
            f->ia++, f->ib++;
            bool da = ENTRY_IS_DIR(ea), db = ENTRY_IS_DIR(eb);
            if (da && db) {
                if (ENTRY_IS_DIR_LINK(ea) || ENTRY_IS_DIR_LINK(eb)) {
                    skipped++;
                    say("skipped:        ", rel.s, " (symbolic link or junction, not entered)");
                } else {
                    ok = push(&frames, &count, &cap, pa.s, pb.s, pa.len, pb.len, rel.len);
                }
            } else if (da != db) {
                different++;
                say("different:      ", rel.s, da ? " (a folder in the first, a file in the second)" : " (a file in the first, a folder in the second)");
            } else if (ea->size != eb->size) {
                different++;
                say("different:      ", rel.s, " (size)");
            } else {
                reader ra, rb;
                result r;
                if (!open_pair(&ra, &rb, pa.s, pb.s)) {
                    had_error = true;
                } else {
                    bool done = compare_streams(&ra, &rb, pa.s, pb.s, true, &r);
                    reader_close(&ra), reader_close(&rb);
                    if (!done) had_error = true;
                    else if (r.differ) different++, say("different:      ", rel.s, NULL);
                    else {
                        same++;
                        if (verbose) say("same:           ", rel.s, NULL);
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < count; i++) dir_free(&frames[i].a), dir_free(&frames[i].b);
    tk_free(frames), wbuf_free(&pa), wbuf_free(&pb), wbuf_free(&rel);
    if (!ok) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    if (!quiet) {
        out_u64(&tk_out, same), out_str(&tk_out, " same, "), out_u64(&tk_out, different), out_str(&tk_out, " different, ");
        out_u64(&tk_out, only_a), out_str(&tk_out, " only in the first, "), out_u64(&tk_out, only_b), out_str(&tk_out, " only in the second");
        if (skipped) out_str(&tk_out, ", "), out_u64(&tk_out, skipped), out_str(&tk_out, " skipped");
        out_char(&tk_out, '\n');
    }
    if (had_error) return TK_ERROR;
    return different || only_a || only_b || skipped ? TK_NO : TK_OK;
}

int im_main(void) {
    out_init();
    cmdline cmd;
    if (!cmdline_get(&cmd)) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    static const optspec specs[] = {
        {L'a', L"all", false, 1}, {L'v', L"verbose", false, 2}, {L'q', L"quiet", false, 3},
        {L'h', L"help", false, 4}, {0, L"version", false, 5},
    };
    optparse op;
    opt_start(&op, &cmd, specs, sizeof specs / sizeof specs[0], TOOL);
    const wchar_t *operand[3] = {0}, *value;
    int n = 0;
    for (int id; (id = opt_next(&op, &value)) != OPT_END;) {
        switch (id) {
        case OPT_BAD: return finish(TOOL, TK_ERROR);
        case OPT_OPERAND:
            if (n == 2) return err_text(TOOL, value, "too many arguments (try --help)"), TK_ERROR;
            operand[n++] = value;
            break;
        case 1: all_diffs = true; break;
        case 2: verbose = true; break;
        case 3: quiet = true; break;
        case 4: usage(); return finish(TOOL, TK_OK);
        case 5: print_version(TOOL); return finish(TOOL, TK_OK);
        }
    }
    if (n != 2) return err_text(TOOL, NULL, "two files or two folders are needed (try --help)"), TK_ERROR;
    DWORD ea = 0, eb = 0;
    DWORD aa = fs_attributes(operand[0], &ea), ab = fs_attributes(operand[1], &eb);
    if (aa == INVALID_FILE_ATTRIBUTES) return err_win(TOOL, operand[0], NULL, ea), finish(TOOL, TK_ERROR);
    if (ab == INVALID_FILE_ATTRIBUTES) return err_win(TOOL, operand[1], NULL, eb), finish(TOOL, TK_ERROR);
    bool da = aa & FILE_ATTRIBUTE_DIRECTORY, db = ab & FILE_ATTRIBUTE_DIRECTORY;
    if (da != db) return err_text(TOOL, NULL, "cannot compare a file with a folder"), finish(TOOL, TK_ERROR);
    buf_a = tk_alloc(CHUNK), buf_b = tk_alloc(CHUNK);
    if (!buf_a || !buf_b) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    return finish(TOOL, da ? compare_two_folders(operand[0], operand[1]) : compare_two_files(operand[0], operand[1]));
}
