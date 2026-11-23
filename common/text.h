/* Text conversion and numbers, with no operating system calls. */
#pragma once
#include "tk.h"

/* UTF-16 to UTF-8. A lone surrogate becomes U+FFFD. With out NULL it only counts. */
size_t utf16_to_utf8(const wchar_t *s, size_t n, char *out);

/* UTF-8 to UTF-16, returning the count of wchar_t. Strict mode returns SIZE_MAX on malformed input;
   lossy mode turns each malformed byte into U+FFFD. With out NULL it only counts. */
size_t utf8_to_utf16(const char *s, size_t n, wchar_t *out, bool lossy);

/* How many bytes at the end of s start a character that is not complete yet (0 to 3). */
size_t utf8_partial_tail(const unsigned char *s, size_t n);

/* Decimal, 0x hex, with an optional k, m, g or t suffix (powers of 1024). False on junk or overflow. */
bool parse_u64(const wchar_t *s, uint64_t *value);

/* Formats into out (at least 21 bytes) and returns the length. */
size_t format_u64(uint64_t value, char *out);

/* Exact bytes with thousands separators, or "1.5 MiB" style. out needs 32 bytes. */
size_t format_size(uint64_t bytes, bool human, char *out);

/* Folds ASCII upper case to lower case only. */
static inline unsigned char ascii_lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c; }
static inline wchar_t wascii_lower(wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c; }

/* Orders names by UTF-16 code unit after ASCII folding (0 means the same name on Windows), then exactly. */
int wname_fold_compare(const wchar_t *a, const wchar_t *b);
int wname_compare(const wchar_t *a, const wchar_t *b);
