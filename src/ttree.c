/* ttree: a folder tree with sizes and totals. */
#include <impossible/base.h>
#include "../common/cmdline.h"
#include "../common/fs.h"
#include "../common/out.h"
#include "../common/text.h"

#define TOOL "ttree"

enum { KIND_FILE, KIND_DIR, KIND_LINK, KIND_DIR_LINK, KIND_UNREADABLE };

/* Only the entries that will be shown are kept: the tree is printed once every folder total is known. */
typedef struct {
    size_t name;      /* offset into the name pool */
    uint64_t size;    /* a folder's size is its whole contents */
    DWORD detail;     /* the reparse tag, or the Windows error of an unreadable folder */
    uint32_t depth;
    uint8_t kind;
    bool last;        /* the last of its siblings that is shown */
} node;

static node *nodes;
static size_t node_count, node_cap;
static wchar_t *pool;
static size_t pool_len, pool_cap;
static bool human = true, ascii = false, dirs_only = false, had_error = false;

static void usage(void) {
    out_str(&tk_out,
        "Usage: ttree [OPTION]... [PATH]...\n"
        "\n"
        "Prints PATH (default: the current folder) as a tree. Every file and folder shows its size; a\n"
        "folder's size is everything inside it, including what -d leaves out of the listing.\n"
        "Symbolic links and junctions are listed, marked, and never entered or counted.\n"
        "\n"
        "  -d, --depth N   list at most N levels below PATH (0 prints only PATH); totals still cover all\n"
        "  -b, --bytes     exact sizes in bytes instead of KiB, MiB and so on\n"
        "  -D, --dirs      list folders only\n"
        "      --ascii     draw the tree with | + and - instead of box characters\n"
        "  -h, --help      show this help\n"
        "      --version   show the version\n"
        "\n"
        "The last line gives the folders, files and total size under PATH.\n"
        "A folder that cannot be read is marked [error N], reported on standard error, and the exit\n"
        "status is 2.\n");
}

static bool add_node(const wchar_t *name, uint8_t kind, uint32_t depth, uint64_t size, DWORD detail, size_t *index) {
    size_t len = tk_wlen(name) + 1;
    if (node_count == node_cap) {
        node *grown = tk_realloc(nodes, (node_cap ? node_cap * 2 : 256) * sizeof(node));
        if (!grown) return false;
        nodes = grown, node_cap = node_cap ? node_cap * 2 : 256;
    }
    if (pool_len + len > pool_cap) {
        size_t want = pool_cap ? pool_cap * 2 : 4096;
        while (want < pool_len + len) want *= 2;
        wchar_t *grown = tk_realloc(pool, want * sizeof(wchar_t));
        if (!grown) return false;
        pool = grown, pool_cap = want;
    }
    for (size_t i = 0; i < len; i++) pool[pool_len + i] = name[i];
    nodes[node_count] = (node){.name = pool_len, .size = size, .detail = detail, .depth = depth, .kind = kind};
    pool_len += len;
    *index = node_count++;
    return true;
}

typedef struct {
    dirlist list;
    size_t next, path_len;
    size_t node;       /* the folder's own node, or SIZE_MAX when it is not shown */
    size_t last_shown; /* node of the last child shown so far, or SIZE_MAX */
    uint64_t total;
    uint32_t depth;    /* of this folder; its children are depth + 1 */
} frame;

static uint64_t folders, files;

/* Walks root and appends nodes. False on out of memory. */
static bool walk(const wchar_t *root, uint64_t *grand_total, size_t limit) {
    wbuf path = {0};
    frame *frames = NULL;
    size_t count = 0, cap = 0;
    bool ok = wbuf_set(&path, root);
    size_t root_node = SIZE_MAX;
    ok = ok && add_node(root, KIND_DIR, 0, 0, 0, &root_node);
    bool entering = ok;
    size_t enter_node = root_node;
    uint32_t enter_depth = 0;
    while (ok) {
        if (entering) {
            entering = false;
            frame f = {.path_len = path.len, .node = enter_node, .last_shown = SIZE_MAX, .depth = enter_depth};
            DWORD error;
            if (!dir_read(path.s, &f.list, &error)) {
                err_win(TOOL, path.s, "cannot list the folder", error);
                had_error = true;
                if (enter_node != SIZE_MAX) nodes[enter_node].kind = KIND_UNREADABLE, nodes[enter_node].detail = error;
            } else {
                if (count == cap) {
                    frame *grown = tk_realloc(frames, (cap ? cap * 2 : 16) * sizeof(frame));
                    if (!grown) { dir_free(&f.list), ok = false; break; }
                    frames = grown, cap = cap ? cap * 2 : 16;
                }
                frames[count++] = f;
            }
        }
        if (!count) break;
        frame *f = &frames[count - 1];
        if (f->next == f->list.count) {
            uint64_t total = f->total;
            if (f->node != SIZE_MAX) {
                nodes[f->node].size = total;
                if (f->last_shown != SIZE_MAX) nodes[f->last_shown].last = true;
            }
            dir_free(&f->list);
            count--;
            if (count) frames[count - 1].total += total;
            else *grand_total = total;
            continue;
        }
        const entry *e = &f->list.items[f->next++];
        uint32_t depth = f->depth + 1;
        bool shown = depth <= limit && (!dirs_only || ENTRY_IS_DIR(e));
        bool is_dir = ENTRY_IS_DIR(e), is_link = (e->attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        uint8_t kind = is_dir ? (is_link ? KIND_DIR_LINK : KIND_DIR) : (is_link ? KIND_LINK : KIND_FILE);
        size_t index = SIZE_MAX;
        if (shown) {
            if (!add_node(e->name, kind, depth, is_dir ? 0 : e->size, e->reparse_tag, &index)) { ok = false; break; }
            if (f->node != SIZE_MAX) f->last_shown = index;
        }
        if (is_dir && !is_link) {
            folders++;
            wbuf_cut(&path, f->path_len);
            if (!wbuf_join(&path, e->name)) { ok = false; break; }
            entering = true, enter_node = index, enter_depth = depth;
        } else if (!is_dir) {
            files++;
            if (!is_link) f->total += e->size;
        }
    }
    for (size_t i = 0; i < count; i++) dir_free(&frames[i].list);
    tk_free(frames);
    wbuf_free(&path);
    return ok;
}

static void put_size(uint64_t bytes) {
    char text[32];
    format_size(bytes, human, text);
    out_str(&tk_out, text);
}

static bool print_tree(void) {
    uint32_t deepest = 0;
    for (size_t i = 0; i < node_count; i++)
        if (nodes[i].depth > deepest) deepest = nodes[i].depth;
    bool *last_at = tk_alloc((size_t)deepest + 1);
    if (!last_at) return false;
    const char *bar = ascii ? "|   " : "\xE2\x94\x82   ", *tee = ascii ? "+-- " : "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ";
    const char *corner = ascii ? "\\-- " : "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 ";
    for (size_t i = 0; i < node_count && !tk_out.failed; i++) {
        const node *n = &nodes[i];
        if (n->depth) {
            for (uint32_t k = 1; k < n->depth; k++) out_str(&tk_out, last_at[k] ? "    " : bar);
            out_str(&tk_out, n->last ? corner : tee);
            last_at[n->depth] = n->last;
        }
        out_wide(&tk_out, pool + n->name);
        if (n->depth && (n->kind == KIND_DIR || n->kind == KIND_DIR_LINK || n->kind == KIND_UNREADABLE)) out_char(&tk_out, '\\');
        out_str(&tk_out, "  ");
        if (n->kind == KIND_LINK || n->kind == KIND_DIR_LINK) {
            out_char(&tk_out, '['), out_str(&tk_out, reparse_kind(n->detail)), out_char(&tk_out, ']');
        } else if (n->kind == KIND_UNREADABLE) {
            out_str(&tk_out, "[error "), out_u64(&tk_out, n->detail), out_char(&tk_out, ']');
        } else {
            put_size(n->size);
        }
        out_char(&tk_out, '\n');
    }
    tk_free(last_at);
    return true;
}

static void count_line(uint64_t n, const char *one, const char *many) {
    out_u64(&tk_out, n), out_char(&tk_out, ' '), out_str(&tk_out, n == 1 ? one : many);
}

int im_main(void) {
    out_init();
    cmdline cmd;
    if (!cmdline_get(&cmd)) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    static const optspec specs[] = {
        {L'd', L"depth", true, 1}, {L'b', L"bytes", false, 2}, {L'D', L"dirs", false, 3},
        {0, L"ascii", false, 4},   {L'h', L"help", false, 5},  {0, L"version", false, 6},
    };
    optparse op;
    opt_start(&op, &cmd, specs, sizeof specs / sizeof specs[0], TOOL);
    uint64_t limit = UINT64_MAX;
    const wchar_t *value;
    const wchar_t **roots = tk_alloc(((size_t)cmd.argc + 1) * sizeof(wchar_t *));
    size_t nroots = 0;
    if (!roots) return err_text(TOOL, NULL, "out of memory"), TK_ERROR;
    for (int id; (id = opt_next(&op, &value)) != OPT_END;) {
        switch (id) {
        case OPT_BAD: return finish(TOOL, TK_ERROR);
        case OPT_OPERAND: roots[nroots++] = value; break;
        case 1:
            if (!parse_u64(value, &limit)) return err_text(TOOL, value, "not a valid depth"), TK_ERROR;
            break;
        case 2: human = false; break;
        case 3: dirs_only = true; break;
        case 4: ascii = true; break;
        case 5: usage(); return finish(TOOL, TK_OK);
        case 6: print_version(TOOL); return finish(TOOL, TK_OK);
        }
    }
    if (!nroots) roots[nroots++] = L".";
    for (size_t i = 0; i < nroots && !tk_out.failed; i++) {
        DWORD error;
        DWORD attrs = fs_attributes(roots[i], &error);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            err_win(TOOL, roots[i], NULL, error), had_error = true;
            continue;
        }
        if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            err_text(TOOL, roots[i], "is not a folder"), had_error = true;
            continue;
        }
        node_count = pool_len = 0, folders = files = 0;
        uint64_t total = 0;
        if (!walk(roots[i], &total, limit > 0xFFFFFFF0u ? 0xFFFFFFF0u : (size_t)limit) || !print_tree())
            return err_text(TOOL, roots[i], "out of memory"), TK_ERROR;
        out_char(&tk_out, '\n');
        count_line(folders, "folder", "folders"), out_str(&tk_out, ", ");
        count_line(files, "file", "files"), out_str(&tk_out, ", ");
        put_size(total), out_str(&tk_out, " in total\n");
    }
    return finish(TOOL, had_error ? TK_ERROR : TK_OK);
}
