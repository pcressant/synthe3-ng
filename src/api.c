/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "api.h"
#include "engine.h"
#include "dsp.h"
#include "text.h"
#include <stdio.h>
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

struct S3TextStream {
    S3Text text;
    S3Request *request;
    S3PreparedCallback callback;
    void *context;
    S3PreparedMark *marks;
    size_t capacity;
    uint32_t bytes, mark_count;
};
_Static_assert(sizeof(S3PreparedMark) == 8, "Prepared mark ABI");
static int text_cancelled(void *context) {
    S3TextStream *stream = context;
    return atomic_load_explicit(&stream->request->cancelled, memory_order_relaxed);
}
static int prepared_emit(void *context, const char *text, const S3Mark *mark, size_t bytes) {
    S3TextStream *stream = context;
    if (text_cancelled(stream)) return S3_CANCELLED;
    size_t n = 0;
    for (const S3Mark *m = mark; m && m->offset <= bytes; m = m->next) n++;
    if (n > stream->capacity) {
        void *p = realloc(stream->marks, n * sizeof(*stream->marks));
        if (!p) return S3_NOMEM;
        stream->marks = p;
        stream->capacity = n;
    }
    for (size_t i = 0; i < n; i++, mark = mark->next) {
        stream->marks[i].offset = (uint32_t)mark->offset;
        stream->marks[i].index = (uint32_t)strtoul(mark->name, NULL, 10);
    }
    return stream->callback(stream->context, text, (uint32_t)bytes, stream->marks, (uint32_t)n);
}
static int prepared_mark(void *context, const S3Mark *mark) {
    S3TextStream *stream = context;
    if (text_cancelled(stream)) return S3_CANCELLED;
    S3PreparedMark prepared = {0, (uint32_t)strtoul(mark->name, NULL, 10)};
    return stream->callback(stream->context, "", 0, &prepared, 1);
}
S3TextStream *S3_CALL s3_api_text_create(S3Request *request, S3PreparedCallback callback, void *context) {
    if (!request || !callback) return NULL;
    S3TextStream *stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->request = request;
    stream->callback = callback;
    stream->context = context;
    stream->text.context = stream;
    stream->text.emit = prepared_emit;
    stream->text.emit_mark = prepared_mark;
    stream->text.stop = text_cancelled;
    return stream;
}
int32_t S3_CALL s3_api_text_append(S3TextStream *stream, const char *text, uint32_t length) {
    if (!stream || !text) return S3_INVALID;
    if (text_cancelled(stream)) return stream->text.status = S3_CANCELLED;
    if (stream->text.status) return stream->text.status;
    if (length > 1024 * 1024 - stream->bytes || memchr(text, 0, length))
        return stream->text.status = S3_INVALID;
    char *copy = malloc((size_t)length + 1);
    if (!copy) return stream->text.status = S3_NOMEM;
    memcpy(copy, text, length);
    copy[length] = 0;
    stream->bytes += length;
    int32_t status = s3_text_append(&stream->text, copy);
    free(copy);
    return status;
}
int32_t S3_CALL s3_api_text_mark(S3TextStream *stream, uint32_t index) {
    if (!stream) return S3_INVALID;
    if (text_cancelled(stream)) return stream->text.status = S3_CANCELLED;
    if (stream->text.status) return stream->text.status;
    if (stream->mark_count >= 65536) return stream->text.status = S3_INVALID;
    char name[16];
    snprintf(name, sizeof(name), "%lu", (unsigned long)index);
    stream->mark_count++;
    return s3_text_mark(&stream->text, name);
}
int32_t S3_CALL s3_api_text_finish(S3TextStream *stream) {
    if (!stream) return S3_INVALID;
    if (text_cancelled(stream)) return stream->text.status = S3_CANCELLED;
    return s3_text_finish(&stream->text);
}
int32_t S3_CALL s3_api_text_spelling(S3TextStream *stream, int32_t enabled) {
    if (!stream || (enabled != 0 && enabled != 1)) return S3_INVALID;
    if (stream->text.status) return stream->text.status;
    if (stream->text.n || stream->text.marks) return S3_STATE;
    stream->text.spelling = enabled;
    return S3_OK;
}
void S3_CALL s3_api_text_destroy(S3TextStream *stream) {
    if (!stream) return;
    s3_text_clear(&stream->text);
    free(stream->marks);
    free(stream);
}
