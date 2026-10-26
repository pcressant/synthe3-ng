/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "api.h"
#include "engine.h"
#include "dsp.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Cancellation requires lock-free int atomics");
struct S3Request { atomic_int cancelled; };
/* 0 closed, 1 ready, 2 in use (including initialization/destruction). */
static atomic_int state;
static const S3Request *active_request;
static uint32_t last_length, last_samples;
static int last_valid;
static int cancelled(void) {
    return atomic_load_explicit(&active_request->cancelled, memory_order_relaxed);
}
static int32_t acquire(void) {
    int expected = 1;
    if (atomic_compare_exchange_strong(&state, &expected, 2))
        return S3_OK;
    return expected == 0 ? S3_STATE : S3_BUSY;
}
uint32_t S3_CALL s3_api_version(void) { return S3_ABI_VERSION; }
int32_t S3_CALL s3_api_open(void) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&state, &expected, 2))
        return S3_BUSY;
    s3_init();
    s3_dsp_init();
    last_valid = 0;
    atomic_store(&state, 1);
    return S3_OK;
}
int32_t S3_CALL s3_api_close(void) {
    int32_t status = acquire();
    if (status)
        return status;
    last_valid = 0;
    s3_close();
    s3_dsp_close();
    atomic_store(&state, 0);
    return S3_OK;
}
S3Request *S3_CALL s3_api_request_create(void) {
    S3Request *r = malloc(sizeof(*r));
    if (r)
        atomic_init(&r->cancelled, 0);
    return r;
}
void S3_CALL s3_api_request_cancel(S3Request *r) {
    if (r)
        atomic_store_explicit(&r->cancelled, 1, memory_order_relaxed);
}
void S3_CALL s3_api_request_destroy(S3Request *r) { free(r); }
int32_t S3_CALL s3_api_render(S3Request *request, const char *text, uint32_t length,
    int32_t rate, int32_t pitch, int32_t range, int32_t volume,
    const int16_t **pcm, uint32_t *samples) {
    if (pcm) *pcm = NULL;
    if (samples) *samples = 0;
    int32_t status = acquire();
    if (status)
        return status;
    last_valid = 0;
    if (!request || !text || !pcm || !samples || length > 240 ||
        rate < 0 || rate > 15 || pitch < 0 || pitch > 15 ||
        range < -100 || range > 100 || volume < -100 || volume > 100 ||
        memchr(text, 0, length)) {
        atomic_store(&state, 1);
        return S3_INVALID;
    }
    char fragment[241];
    memcpy(fragment, text, length);
    fragment[length] = 0;
    active_request = request;
    const int16_t *raw = NULL, *output = NULL;
    size_t count = 0, output_count = 0;
    if (cancelled())
        status = S3_CANCELLED;
    else {
        int result = s3_render_modulated(fragment, rate, pitch, range, cancelled, &raw, &count);
        if (cancelled())
            status = S3_CANCELLED;
        else if (result)
            status = S3_RENDER_ERROR;
        else {
            result = s3_process(raw, count, volume, cancelled, &output, &output_count);
            status = cancelled() || result == 1 ? S3_CANCELLED :
                     result ? S3_RENDER_ERROR : S3_OK;
        }
    }
    if (!status) {
        *pcm = output;
        *samples = (uint32_t)output_count;
        last_length = length;
        last_samples = *samples;
        last_valid = 1;
    }
    active_request = NULL;
    atomic_store(&state, 1);
    return status;
}
int32_t S3_CALL s3_api_sample_at(uint32_t offset, uint32_t *sample) {
    if (sample) *sample = 0;
    int32_t status = acquire();
    if (status)
        return status;
    if (!sample || !last_valid || offset > last_length)
        status = S3_INVALID;
    else {
        size_t pos = (size_t)nearbyint((double)s3_sample_at(offset) * 22050 / 10000);
        *sample = pos > last_samples ? last_samples : (uint32_t)pos;
    }
    atomic_store(&state, 1);
    return status;
}
