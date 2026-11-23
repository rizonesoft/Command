#include "fs.h"
#include "text.h"

bool wbuf_reserve(wbuf *b, size_t chars) {
    if (chars + 1 <= b->cap) return true;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < chars + 1) cap *= 2;
    wchar_t *s = tk_realloc(b->s, cap * sizeof(wchar_t));
    if (!s) return false;
    b->s = s, b->cap = cap;
    return true;
}

bool wbuf_add(wbuf *b, const wchar_t *text, size_t n) {
    if (!wbuf_reserve(b, b->len + n)) return false;
    for (size_t i = 0; i < n; i++) b->s[b->len + i] = text[i];
    b->len += n;
    b->s[b->len] = 0;
    return true;
}

bool wbuf_set(wbuf *b, const wchar_t *text) {
    b->len = 0;
    return wbuf_add(b, text, tk_wlen(text)) || (b->len = 0, false);
}

static bool is_sep(wchar_t c) { return c == L'\\' || c == L'/'; }

bool wbuf_join(wbuf *b, const wchar_t *name) {
    if (b->len && !is_sep(b->s[b->len - 1]) && b->s[b->len - 1] != L':' && !wbuf_add(b, L"\\", 1)) return false;
    return wbuf_add(b, name, tk_wlen(name));
}

void wbuf_cut(wbuf *b, size_t len) {
    b->len = len;
    b->s[len] = 0;
}

void wbuf_free(wbuf *b) {
    tk_free(b->s);
    b->s = NULL, b->len = b->cap = 0;
}

wchar_t *fs_native(const wchar_t *p) {
    size_t n = tk_wlen(p);
    wchar_t *out = tk_alloc((n + 16) * sizeof(wchar_t));
    if (!out) return NULL;
    bool drive = n >= 3 && ((p[0] | 32) >= L'a' && (p[0] | 32) <= L'z') && p[1] == L':' && is_sep(p[2]);
    bool unc = n >= 3 && is_sep(p[0]) && is_sep(p[1]) && !is_sep(p[2]) && p[2] != L'?' && p[2] != L'.';
    if (n < 240 || !(drive || unc)) {
        for (size_t i = 0; i <= n; i++) out[i] = p[i];
        return out;
    }
    size_t *marks = tk_alloc((n / 2 + 2) * sizeof(size_t));
    if (!marks) return tk_free(out), NULL;
    size_t o = 0, depth = 0, keep = unc ? 2 : 0; /* a share's server and name are never removed by ".." */
    const wchar_t *prefix = unc ? L"\\\\?\\UNC" : L"\\\\?\\";
    for (; *prefix; prefix++) out[o++] = *prefix;
    const wchar_t *at = p + 2;
    if (drive) out[o++] = p[0], out[o++] = L':';
    size_t root = o;
    while (*at) {
        while (is_sep(*at)) at++;
        const wchar_t *end = at;
        while (*end && !is_sep(*end)) end++;
        size_t len = (size_t)(end - at);
        if (!len || (len == 1 && at[0] == L'.')) {
        } else if (len == 2 && at[0] == L'.' && at[1] == L'.') {
            if (depth > keep) o = marks[--depth];
        } else {
            marks[depth++] = o;
            out[o++] = L'\\';
            for (size_t i = 0; i < len; i++) out[o++] = at[i];
        }
        at = end;
    }
    if (o == root && drive) out[o++] = L'\\';
    out[o] = 0;
    tk_free(marks);
    return out;
}

DWORD fs_attributes(const wchar_t *path, DWORD *error) {
    wchar_t *native = fs_native(path);
    if (!native) return *error = ERROR_NOT_ENOUGH_MEMORY, INVALID_FILE_ATTRIBUTES;
    DWORD a = GetFileAttributesW(native);
    if (a == INVALID_FILE_ATTRIBUTES) *error = GetLastError();
    tk_free(native);
    return a;
}

bool reader_open(reader *r, const wchar_t *path) {
    wchar_t *native = fs_native(path);
    *r = (reader){0};
    if (!native) return r->error = ERROR_NOT_ENOUGH_MEMORY, false;
    HANDLE h = CreateFileW(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    tk_free(native);
    if (h == INVALID_HANDLE_VALUE) {
        r->error = GetLastError();
        DWORD ignored;
        DWORD a = r->error == ERROR_ACCESS_DENIED ? fs_attributes(path, &ignored) : INVALID_FILE_ATTRIBUTES;
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) r->error = ERROR_DIRECTORY;
        return false;
    }
    r->h = h, r->seekable = true, r->owns = true;
    LARGE_INTEGER size;
    if (GetFileSizeEx(h, &size)) r->sized = true, r->size0 = (uint64_t)size.QuadPart;
    return true;
}

void reader_stdin(reader *r) {
    DWORD mode;
    *r = (reader){0};
    r->h = GetStdHandle(STD_INPUT_HANDLE);
    if (!r->h || r->h == INVALID_HANDLE_VALUE) r->h = NULL, r->error = ERROR_INVALID_HANDLE;
    else r->console = GetConsoleMode(r->h, &mode) != 0;
    /* Standard input is not watched for change: it may be a pipe, and with tier 0 alone a pipe answers
       GetFileSizeEx and SetFilePointerEx like a file does (its size is the bytes waiting), so the two cannot
       be told apart. */
}

void reader_close(reader *r) {
    if (r->owns && r->h) CloseHandle(r->h);
    r->h = NULL, r->owns = false;
}

static bool read_console(reader *r) {
    wchar_t wide[4096];
    DWORD n;
    if (!ReadConsoleW(r->h, wide, 4096, &n, NULL)) return r->error = GetLastError(), false;
    for (DWORD i = 0; i < n; i++)
        if (wide[i] == 0x1A) n = i; /* Ctrl+Z ends the input */
    r->stage_pos = 0;
    r->stage_len = utf16_to_utf8(wide, n, r->stage);
    return true;
}

bool reader_read(reader *r, void *buf, size_t cap, size_t *got) {
    *got = 0;
    if (!r->h) return false;
    if (r->console) {
        if (r->stage_pos == r->stage_len && !read_console(r)) return false;
        size_t n = r->stage_len - r->stage_pos;
        if (n > cap) n = cap;
        unsigned char *to = buf;
        for (size_t i = 0; i < n; i++) to[i] = (unsigned char)r->stage[r->stage_pos + i];
        r->stage_pos += n, r->offset += n, *got = n;
        return true;
    }
    DWORD n;
    if (cap > 0x40000000) cap = 0x40000000;
    if (!ReadFile(r->h, buf, (DWORD)cap, &n, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) return true; /* the writer closed: end of input */
        return r->error = e, false;
    }
    r->offset += n, *got = n;
    if (r->sized && (!reader_check(r) || (!n && r->pos0 + r->offset != r->size0))) {
        r->error = TK_ERR_CHANGED, *got = 0;
        return false;
    }
    return true;
}

bool reader_check(reader *r) {
    LARGE_INTEGER now;
    if (!r->sized) return true;
    if (!GetFileSizeEx(r->h, &now) || (uint64_t)now.QuadPart != r->size0) return r->error = TK_ERR_CHANGED, false;
    return true;
}

bool reader_skip_to(reader *r, uint64_t offset) {
    if (offset < r->offset) return r->error = ERROR_NEGATIVE_SEEK, false;
    if (r->seekable) {
        LARGE_INTEGER to;
        to.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(r->h, to, NULL, FILE_BEGIN)) return r->error = GetLastError(), false;
        r->offset = offset;
        return true;
    }
    char scratch[8192];
    while (r->offset < offset) {
        uint64_t want = offset - r->offset;
        size_t got;
        if (!reader_read(r, scratch, want < sizeof scratch ? (size_t)want : sizeof scratch, &got)) return false;
        if (!got) return true; /* ended early: the caller sees the end on its next read */
    }
    return true;
}

bool reader_size(const reader *r, uint64_t *size) {
    LARGE_INTEGER s;
    if (!r->sized || !GetFileSizeEx(r->h, &s)) return false;
    *size = (uint64_t)s.QuadPart;
    return true;
}

static void sift(entry *a, size_t root, size_t end) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= end) return;
        if (child + 1 < end && wname_compare(a[child].name, a[child + 1].name) < 0) child++;
        if (wname_compare(a[root].name, a[child].name) >= 0) return;
        entry t = a[root];
        a[root] = a[child], a[child] = t;
        root = child;
    }
}

static void sort_entries(entry *a, size_t n) {
    for (size_t i = n / 2; i-- > 0;) sift(a, i, n);
    for (size_t end = n; end-- > 1;) {
        entry t = a[0];
        a[0] = a[end], a[end] = t;
        sift(a, 0, end);
    }
}

bool dir_read(const wchar_t *path, dirlist *list, DWORD *error) {
    wbuf pattern = {0};
    *list = (dirlist){0};
    if (!wbuf_set(&pattern, path) || !wbuf_join(&pattern, L"*")) return wbuf_free(&pattern), *error = ERROR_NOT_ENOUGH_MEMORY, false;
    wchar_t *native = fs_native(pattern.s);
    wbuf_free(&pattern);
    if (!native) return *error = ERROR_NOT_ENOUGH_MEMORY, false;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(native, &d);
    tk_free(native);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) return true; /* nothing in it */
        return *error = e, false;
    }
    size_t cap = 0, pool_len = 0, pool_cap = 0;
    bool ok = true;
    do {
        const wchar_t *name = d.cFileName;
        if (name[0] == L'.' && (!name[1] || (name[1] == L'.' && !name[2]))) continue;
        size_t len = tk_wlen(name) + 1;
        if (list->count == cap) {
            entry *grown = tk_realloc(list->items, (cap ? cap * 2 : 32) * sizeof(entry));
            if (!grown) { ok = false, *error = ERROR_NOT_ENOUGH_MEMORY; break; }
            list->items = grown, cap = cap ? cap * 2 : 32;
        }
        if (pool_len + len > pool_cap) {
            size_t want = pool_cap ? pool_cap * 2 : 1024;
            while (want < pool_len + len) want *= 2;
            wchar_t *grown = tk_realloc(list->pool, want * sizeof(wchar_t));
            if (!grown) { ok = false, *error = ERROR_NOT_ENOUGH_MEMORY; break; }
            list->pool = grown, pool_cap = want;
        }
        for (size_t i = 0; i < len; i++) list->pool[pool_len + i] = name[i];
        entry *e = &list->items[list->count++];
        e->name = (wchar_t *)(uintptr_t)pool_len; /* an offset until the pool stops moving */
        e->size = (uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow;
        e->attrs = d.dwFileAttributes;
        e->reparse_tag = d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? d.dwReserved0 : 0;
        pool_len += len;
    } while (FindNextFileW(h, &d));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = false, *error = GetLastError();
    FindClose(h);
    if (!ok) return dir_free(list), false;
    for (size_t i = 0; i < list->count; i++) list->items[i].name = list->pool + (uintptr_t)list->items[i].name;
    sort_entries(list->items, list->count);
    return true;
}

void dir_free(dirlist *list) {
    tk_free(list->items), tk_free(list->pool);
    *list = (dirlist){0};
}

bool fs_classify(const wchar_t *path, pathinfo *info) {
    *info = (pathinfo){0};
    size_t n = tk_wlen(path);
    /* Trailing slashes are dropped, but "C:\" keeps its one: "C:" alone means the current folder of drive C. */
    while (n > 1 && (path[n - 1] == L'\\' || path[n - 1] == L'/') && !(n == 3 && path[1] == L':')) n--;
    wbuf b = {0};
    if (!wbuf_add(&b, path, n)) return wbuf_free(&b), info->error = ERROR_NOT_ENOUGH_MEMORY, false;
    wchar_t *native = fs_native(b.s);
    wbuf_free(&b);
    if (!native) return info->error = ERROR_NOT_ENOUGH_MEMORY, false;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(native, &d);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        info->is_dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info->is_link = (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IS_LINK_TAG(d.dwReserved0);
        info->tag = (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? d.dwReserved0 : 0;
        info->size = (uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow;
        tk_free(native);
        return true;
    }
    /* A drive or share root has no entry of its own to find; ask for its attributes instead. */
    DWORD a = GetFileAttributesW(native);
    if (a == INVALID_FILE_ATTRIBUTES) info->error = GetLastError();
    else info->is_dir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    tk_free(native);
    return a != INVALID_FILE_ATTRIBUTES;
}

const char *reparse_kind(DWORD tag) {
    if (tag == 0xA000000C) return "symlink";
    if (tag == 0xA0000003) return "junction";
    return "reparse point";
}
