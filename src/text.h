/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef S3_TEXT_H
#define S3_TEXT_H
#include <stddef.h>
/* Marks own their copied names; offsets count normalized Latin-1 bytes.
 * Emit callbacks borrow text/marks only for their duration. The list may also
 * contain marks beyond the emitted length: process only offsets <= that length.
 * Whitespace-only fragments use emit_mark instead of emit. Callbacks must return
 * 0 on success, 1 on cancellation, or -1 on error. Ordered marks do not split
 * a fragment. No audio, XML or platform dependencies are required. */
typedef struct S3Mark {
    char *name;
    size_t offset;
    struct S3Mark *next;
} S3Mark;
typedef struct {
    char text[241];
    size_t n;
    int status;
    S3Mark *marks, *last_mark;
    int punctuation, spelling;
    void *context;
    int (*emit)(void *, const char *, const S3Mark *, size_t);
    int (*emit_mark)(void *, const S3Mark *);
    int (*stop)(void *);
} S3Text;
/* Zero-initialize, then set emit/emit_mark and optional stop/context/settings.
 * Append NUL-terminated UTF-8. finish flushes the last fragment; clear releases
 * remaining marks after success, error or cancellation. Objects are independent;
 * callbacks must not recursively modify the same object. */
int s3_text_append(S3Text *, const char *);
int s3_text_mark(S3Text *, const char *);
int s3_text_finish(S3Text *);
void s3_text_clear(S3Text *);
/* Legacy decoder and character names shared with the Linux CHAR/KEY adapter. */
unsigned s3_text_decode(const unsigned char **);
const char *s3_punct_name(unsigned);
#endif
