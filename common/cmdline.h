/* The command line as Windows programs read it, and a small option parser. */
#pragma once
#include "tk.h"

typedef struct {
    int argc;
    wchar_t **argv; /* argv[0] is the program name; argv[argc] is NULL */
    wchar_t *text;  /* the storage behind argv */
} cmdline;

/* Splits GetCommandLineW() by the usual rules (2n backslashes and a quote make n backslashes and
   toggle quoting, 2n+1 make n and a literal quote, "" inside quotes is a quote). False when out of memory. */
bool cmdline_get(cmdline *c);

/* The same rules on any string, for tests and for tools that parse a given text. */
bool cmdline_split(const wchar_t *line, cmdline *c);

void cmdline_free(cmdline *c);

typedef struct {
    wchar_t short_name;       /* 0 for none */
    const wchar_t *long_name; /* without the leading dashes, or NULL */
    bool takes_value;
    int id;                   /* returned by opt_next; must be 0 or more */
} optspec;

typedef struct {
    const cmdline *cmd;
    const optspec *specs;
    size_t count;
    const char *tool;
    int next;                 /* index of the next argument */
    const wchar_t *cluster;   /* the rest of a "-abc" group */
    bool only_operands;       /* after "--" */
} optparse;

enum { OPT_END = -1, OPT_BAD = -2, OPT_OPERAND = -3 };

void opt_start(optparse *p, const cmdline *c, const optspec *specs, size_t count, const char *tool);

/* The next option's id with its value, an operand (OPT_OPERAND, text in *value), OPT_END, or
   OPT_BAD after printing a one-line error. A lone "-" is an operand. */
int opt_next(optparse *p, const wchar_t **value);

/* Prints "tool VERSION" to standard output. */
void print_version(const char *tool);
