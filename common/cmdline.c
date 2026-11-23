#include <windows.h>
#include "cmdline.h"
#include "out.h"
#include "text.h"

static bool is_blank(wchar_t c) { return c == L' ' || c == L'\t'; }

/* Splits line into out (when not NULL) and returns the characters used; *count gets the arguments.
   The same code runs once to measure and once to fill, so the two always agree. */
static size_t split_pass(const wchar_t *line, wchar_t *out, wchar_t **argv, size_t *count) {
    size_t used = 0, n = 0;
    const wchar_t *p = line;
#define PUT(ch) ((out ? (void)(out[used] = (ch)) : (void)0), used++)
    /* The program name has no escapes: it ends at the next quote, or at white space when unquoted. */
    if (argv) argv[n] = out + used;
    if (*p == L'"') {
        for (p++; *p && *p != L'"'; p++) PUT(*p);
        if (*p) p++;
    } else {
        for (; *p && !is_blank(*p); p++) PUT(*p);
    }
    PUT(0), n++;
    for (;;) {
        while (is_blank(*p)) p++;
        if (!*p) break;
        if (argv) argv[n] = out + used;
        bool quoted = false;
        for (;;) {
            size_t slashes = 0;
            while (*p == L'\\') slashes++, p++;
            if (*p == L'"') {
                for (size_t i = 0; i < slashes / 2; i++) PUT(L'\\');
                if (slashes % 2) {
                    PUT(L'"'), p++;
                } else if (quoted && p[1] == L'"') {
                    PUT(L'"'), p += 2;
                } else {
                    quoted = !quoted, p++;
                }
            } else {
                for (size_t i = 0; i < slashes; i++) PUT(L'\\');
                if (!*p || (!quoted && is_blank(*p))) break;
                PUT(*p), p++;
            }
        }
        PUT(0), n++;
    }
#undef PUT
    if (argv) argv[n] = NULL;
    *count = n;
    return used;
}

bool cmdline_split(const wchar_t *line, cmdline *c) {
    size_t count;
    size_t chars = split_pass(line, NULL, NULL, &count);
    wchar_t *text = tk_alloc(chars * sizeof(wchar_t));
    wchar_t **argv = tk_alloc((count + 1) * sizeof(wchar_t *));
    if (!text || !argv) return tk_free(text), tk_free(argv), false;
    split_pass(line, text, argv, &count);
    c->argc = (int)count, c->argv = argv, c->text = text;
    return true;
}

void cmdline_free(cmdline *c) {
    tk_free(c->argv), tk_free(c->text);
    c->argv = NULL, c->text = NULL, c->argc = 0;
}

bool cmdline_get(cmdline *c) { return cmdline_split(GetCommandLineW(), c); }

void opt_start(optparse *p, const cmdline *c, const optspec *specs, size_t count, const char *tool) {
    p->cmd = c, p->specs = specs, p->count = count, p->tool = tool;
    p->next = 1, p->cluster = NULL, p->only_operands = false;
}

static bool same(const wchar_t *a, const wchar_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return !b[n];
}

static int bad(optparse *p, const wchar_t *what, const char *why) {
    err_text(p->tool, what, why);
    return OPT_BAD;
}

int opt_next(optparse *p, const wchar_t **value) {
    const cmdline *c = p->cmd;
    const optspec *spec = NULL;
    if (p->cluster && *p->cluster) {
        wchar_t ch = *p->cluster++;
        for (size_t i = 0; i < p->count; i++)
            if (p->specs[i].short_name == ch) spec = &p->specs[i];
        if (!spec) {
            wchar_t shown[3] = {L'-', ch, 0};
            return bad(p, shown, "unknown option (try --help)");
        }
        if (!spec->takes_value) return spec->id;
        wchar_t shown[3] = {L'-', ch, 0};
        if (*p->cluster) *value = p->cluster, p->cluster = NULL;
        else if (p->next < c->argc) *value = c->argv[p->next++];
        else return bad(p, shown, "needs a value");
        return spec->id;
    }
    p->cluster = NULL;
    for (;;) {
        if (p->next >= c->argc) return OPT_END;
        const wchar_t *a = c->argv[p->next++];
        if (p->only_operands || a[0] != L'-' || !a[1]) return *value = a, OPT_OPERAND;
        if (a[1] == L'-') {
            if (!a[2]) {
                p->only_operands = true;
                continue;
            }
            size_t len = 0;
            while (a[2 + len] && a[2 + len] != L'=') len++;
            for (size_t i = 0; i < p->count; i++)
                if (p->specs[i].long_name && same(a + 2, p->specs[i].long_name, len)) spec = &p->specs[i];
            if (!spec) return bad(p, a, "unknown option (try --help)");
            if (a[2 + len] == L'=') {
                if (!spec->takes_value) return bad(p, a, "does not take a value");
                *value = a + 3 + len;
            } else if (spec->takes_value) {
                if (p->next >= c->argc) return bad(p, a, "needs a value");
                *value = c->argv[p->next++];
            }
            return spec->id;
        }
        p->cluster = a + 1;
        return opt_next(p, value);
    }
}

void print_version(const char *tool) {
    out_str(&tk_out, tool);
    out_str(&tk_out, " (Command) " TK_VERSION "\n");
}
