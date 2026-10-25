/* SPDX-License-Identifier: GPL-2.0-or-later
 * Historical ng DSP: three peaking biquads at 10 kHz, peak headroom,
 * 2 ms edge fades, then 48-tap Blackman sinc interpolation to 22050 Hz.
 * Keep double precision and ties-to-even rounding; final voice gain is 0 dB.
 */
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#define PI 3.14159265358979323846
#ifndef S3_OUTPUT_GAIN_DB
#define S3_OUTPUT_GAIN_DB 0.0
#endif
static double weights[441][48];
static struct biquad {
    double b0, b1, b2, a1, a2;
} filters[3];
static double *work, *resampled;
static int16_t *output;
static size_t capacity, out_capacity;

void s3_dsp_init(void) {
    const double hz[] = {750, 2800, 3600}, db[] = {1, -1, 3}, q[] = {.8, .8, .7};
    for (int i = 0; i < 3; i++) {
        double a = pow(10, db[i] / 40), omega = 2 * PI * hz[i] / 10000;
        double alpha = sin(omega) / (2 * q[i]), c = cos(omega), a0 = 1 + alpha / a;
        filters[i] = (struct biquad){(1 + alpha * a) / a0, -2 * c / a0, (1 - alpha * a) / a0,
                                     -2 * c / a0, (1 - alpha / a) / a0};
    }
    for (int p = 0; p < 441; p++) {
        double total = 0, fraction = (double)p / 441;
        for (int t = 0; t < 48; t++) {
            double distance = t - 23 - fraction, arg = PI * .94 * distance;
            double sinc = arg ? sin(arg) / arg : 1;
            weights[p][t] =
                .94 * sinc *
                (.42 + .5 * cos(PI * distance / 24) + .08 * cos(2 * PI * distance / 24));
            total += weights[p][t];
        }
        for (int t = 0; t < 48; t++)
            weights[p][t] /= total;
    }
}

int s3_process(const int16_t *pcm, size_t n, int volume, int (*stop)(void), const int16_t **result,
               size_t *count) {
    if (n > capacity) {
        void *p = realloc(work, n * sizeof(*work));
        if (!p)
            return -1;
        work = p;
        capacity = n;
    }
    size_t m = (size_t)nearbyint((double)n * 22050 / 10000);
    if (m > out_capacity) {
        void *p = realloc(resampled, m * sizeof(*resampled));
        if (!p)
            return -1;
        resampled = p;
        p = realloc(output, m * sizeof(*output));
        if (!p)
            return -1;
        output = p;
        out_capacity = m;
    }
    for (size_t i = 0; i < n; i++) {
        if (!(i & 1023) && stop && stop())
            return 1;
        work[i] = pcm[i];
    }
    for (int f = 0; f < 3; f++) {
        struct biquad b = filters[f];
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t i = 0; i < n; i++) {
            if (!(i & 1023) && stop && stop())
                return 1;
            double x = work[i], y = b.b0 * x + b.b1 * x1 + b.b2 * x2 - b.a1 * y1 - b.a2 * y2;
            work[i] = y;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
        }
    }
    double peak = 0;
    for (size_t i = 0; i < n; i++) {
        if (!(i & 1023) && stop && stop())
            return 1;
        if (fabs(work[i]) > peak)
            peak = fabs(work[i]);
    }
    double gain = peak > 32760 ? 32760 / peak : 1;
    size_t fade = n / 2 < 20 ? n / 2 : 20;
    for (size_t i = 0; i < fade; i++) {
        double w = (double)i / fade;
        work[i] *= w;
        work[n - 1 - i] *= w;
    }
    for (size_t i = 0; i < n; i++) {
        if (!(i & 1023) && stop && stop())
            return 1;
        work[i] = nearbyint(work[i] * gain);
    }
    peak = 0;
    for (size_t i = 0; i < m; i++) {
        if (!(i & 1023) && stop && stop())
            return 1;
        size_t pos = i * 200 / 441, phase = (i * 200) % 441;
        double value = 0;
        for (int t = 0; t < 48; t++) {
            ptrdiff_t point = (ptrdiff_t)pos + t - 23;
            if (point >= 0 && (size_t)point < n)
                value += work[point] * weights[phase][t];
        }
        resampled[i] = value;
        if (fabs(value) > peak)
            peak = fabs(value);
    }
    gain = peak > 32760 ? 32760 / peak : 1;
    for (size_t i = 0; i < m; i++) {
        if (!(i & 1023) && stop && stop())
            return 1;
        /* Fixed voice gain precedes the unchanged Speech Dispatcher volume. */
        double value = nearbyint(resampled[i] * gain * pow(10.0, S3_OUTPUT_GAIN_DB / 20.0));
        if (volume)
            value = nearbyint(value * ((volume + 100) / 100.0));
        output[i] = (int16_t)fmax(-32768, fmin(32767, value));
    }
    *result = output;
    *count = m;
    return 0;
}
void s3_dsp_close(void) {
    free(work);
    free(resampled);
    free(output);
    work = resampled = NULL;
    output = NULL;
    capacity = out_capacity = 0;
}
