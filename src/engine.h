/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef S3_ENGINE_H
#define S3_ENGINE_H
#include <stddef.h>
#include <stdint.h>
/* Single instance; all calls belong to the synthesis thread. Input: <=240 Latin-1 bytes. */
void s3_init(void);
int s3_render(const char *, int, int, int (*)(void), const int16_t **, size_t *);
/* range is Speech Dispatcher pitch_range (-100..100): <=0 preserves PCM.
 * Positive values enable a cancellable timing pass and phrase contours. */
int s3_render_modulated(const char *, int, int, int, int (*)(void), const int16_t **, size_t *);
/* Approximate diphone position of the preceding word boundary in the last
 * render. Does not insert markers or alter PCM. */
size_t s3_sample_at(size_t byte_offset);
void s3_close(void);
#endif
