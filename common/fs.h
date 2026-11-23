/* Files and folders: long paths, streamed reads, sorted folder listings. */
#pragma once
#include <windows.h>
#include "tk.h"

/* Growable wide string for paths. */
typedef struct {
    wchar_t *s;
    size_t len, cap;
} wbuf;

bool wbuf_reserve(wbuf *b, size_t chars);
bool wbuf_set(wbuf *b, const wchar_t *text);
bool wbuf_add(wbuf *b, const wchar_t *text, size_t n);
bool wbuf_join(wbuf *b, const wchar_t *name); /* adds a separator unless the path already ends in one */
void wbuf_cut(wbuf *b, size_t len);           /* shortens to len characters */
void wbuf_free(wbuf *b);

/* The form of a path to hand to Windows: absolute paths of 240 characters or more get the \\?\ prefix
   (normalised, since that prefix turns normalisation off). Free the result with tk_free. */
wchar_t *fs_native(const wchar_t *path);

/* Attributes of a path, or INVALID_FILE_ATTRIBUTES with the error in *error. */
DWORD fs_attributes(const wchar_t *path, DWORD *error);

/* A buffered, error-aware reader over a file or a standard handle. Standard input from a console is
   read as text and given as UTF-8, and ends at Ctrl+Z. */
typedef struct {
    HANDLE h;
    bool console, seekable, owns;
    bool sized;       /* a regular file: its size is watched for change */
    uint64_t size0, pos0; /* the size when the reader started (pos0 is always 0 for a file opened here) */
    DWORD error;     /* of the last failed call */
    uint64_t offset; /* bytes handed out so far */
    char stage[16384];
    size_t stage_pos, stage_len;
} reader;

bool reader_open(reader *r, const wchar_t *path); /* false with r->error set; a folder is ERROR_DIRECTORY */
void reader_stdin(reader *r);
void reader_close(reader *r);
/* Reads up to cap bytes; *got is 0 at the end. False on a read error (r->error). */
bool reader_read(reader *r, void *buf, size_t cap, size_t *got);
/* Moves forward to an absolute offset by seeking, or by reading when the source cannot seek. */
bool reader_skip_to(reader *r, uint64_t offset);
bool reader_size(const reader *r, uint64_t *size);
/* Every successful reader_read on a regular file opened by reader_open (not standard input: see reader_stdin) checks that
   the size is what it was at the start, and at the end of the file that exactly that many bytes came out.
   Otherwise it fails with TK_ERR_CHANGED. A tool that stops reading early calls reader_check before it
   trusts what it has seen. */
#define TK_ERR_CHANGED 0x20000001u
bool reader_check(reader *r);

typedef struct {
    wchar_t *name;
    uint64_t size;
    DWORD attrs, reparse_tag;
} entry;

typedef struct {
    entry *items;
    size_t count;
    wchar_t *pool;
} dirlist;

#define ENTRY_IS_DIR(e) (((e)->attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
/* Only symbolic links and junctions are links. Other reparse points (deduplicated or cloud files,
   for example) are ordinary files and folders with their real size. */
#define IS_LINK_TAG(tag) ((tag) == 0xA000000C || (tag) == 0xA0000003)
#define ENTRY_IS_LINK(e) ((((e)->attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) && IS_LINK_TAG((e)->reparse_tag))
#define ENTRY_IS_DIR_LINK(e) (ENTRY_IS_DIR(e) && ENTRY_IS_LINK(e))

/* What an operand on the command line is. Trailing slashes are ignored, and the entry's own attributes are
   used (a link is not looked through), the same as for an entry found in a folder listing. */
typedef struct {
    DWORD error;    /* 0 when the path exists */
    bool is_dir, is_link;
    DWORD tag;      /* the reparse tag of a link */
    uint64_t size;
} pathinfo;
bool fs_classify(const wchar_t *path, pathinfo *info);

/* Lists a folder, sorted by name, without "." and "..". False with *error set. */
bool dir_read(const wchar_t *path, dirlist *list, DWORD *error);
void dir_free(dirlist *list);

/* "symlink", "junction" or "reparse point". */
const char *reparse_kind(DWORD tag);
