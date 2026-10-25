/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef S3_DSP_H
#define S3_DSP_H
#include <stddef.h>
#include <stdint.h>
void s3_dsp_init(void);
int s3_process(const int16_t *, size_t, int, int (*)(void), const int16_t **, size_t *);
void s3_dsp_close(void);
#endif
