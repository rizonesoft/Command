/* Shared by every Command tool. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TK_VERSION "0.1.0"

/* Exit codes: 1 is a negative answer (not found, different, mismatch), 2 is an error. */
enum { TK_OK = 0, TK_NO = 1, TK_ERROR = 2 };

/* Memory for the tools: zeroed on allocation; NULL when there is none. */
void *tk_alloc(size_t bytes);
void *tk_realloc(void *memory, size_t bytes); /* new bytes are not zeroed */
void tk_free(void *memory);

size_t tk_wlen(const wchar_t *s);
size_t tk_slen(const char *s);
