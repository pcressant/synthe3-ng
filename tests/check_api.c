/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define THREAD_RETURN DWORD WINAPI
#define THREAD_DONE return 0
static void wait_ms(unsigned ms) { Sleep(ms); }
#else
#include <pthread.h>
#include <time.h>
#define THREAD_RETURN void *
#define THREAD_DONE return NULL
static void wait_ms(unsigned ms) {
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}
#endif
static S3Request *running;
static atomic_int started;
static int32_t result;
static THREAD_RETURN render_thread(void *unused) {
    (void)unused;
    char text[241];
    memset(text, 'a', 240); text[240] = 0;
    const int16_t *pcm;
    uint32_t count;
    atomic_store(&started, 1);
    /* Keep rendering until the other thread cancels the shared token. */
    do {
        result = s3_api_render(running, text, 240, 0, 0, 100, 0, &pcm, &count);
    } while (result == S3_OK);
    assert(result == S3_CANCELLED && !pcm && !count);
    THREAD_DONE;
}
static char normalized[1024];
static uint32_t prepared_bytes, prepared_indices, prepared_offset, prepared_index;
static int32_t prepared_result;
static int32_t S3_CALL prepared(void *context, const char *text, uint32_t length,
    const S3PreparedMark *marks, uint32_t count) {
    (void)context;
    assert(prepared_bytes + length < sizeof(normalized));
    for (uint32_t i = 0; i < count; i++) {
        assert(marks[i].offset <= length);
        prepared_offset = prepared_bytes + marks[i].offset;
        prepared_index = marks[i].index;
        prepared_indices++;
    }
    memcpy(normalized + prepared_bytes, text, length);
    prepared_bytes += length;
    normalized[prepared_bytes] = 0;
    return prepared_result;
}
static void check_preparation(void) {
    S3Request *r = s3_api_request_create();
    assert(r);
    assert(!s3_api_text_create(NULL, prepared, NULL));
    S3TextStream *stream = s3_api_text_create(r, prepared, NULL);
    assert(stream);
    assert(!s3_api_text_append(stream, "cœur", (uint32_t)strlen("cœur")));
    assert(!s3_api_text_mark(stream, UINT32_MAX));
    assert(s3_api_text_spelling(stream, 1) == S3_STATE);
    assert(!s3_api_text_append(stream, " et sœur", (uint32_t)strlen(" et sœur")));
    assert(!s3_api_text_finish(stream));
    assert(!strcmp(normalized, "coeur et soeur"));
    assert(prepared_indices == 1 && prepared_offset == 5 && prepared_index == UINT32_MAX);
    prepared_bytes = prepared_indices = 0;
    assert(!s3_api_text_spelling(stream, 1));
    assert(!s3_api_text_append(stream, "a!", 2));
    assert(!s3_api_text_finish(stream));
    assert(!strcmp(normalized, "a, point d'exclamation"));
    s3_api_text_destroy(stream);
    stream = s3_api_text_create(r, prepared, NULL);
    assert(stream);
    s3_api_request_cancel(r);
    assert(s3_api_text_append(stream, "test", 4) == S3_CANCELLED);
    assert(s3_api_text_mark(stream, 1) == S3_CANCELLED);
    assert(s3_api_text_finish(stream) == S3_CANCELLED);
    s3_api_text_destroy(stream);
    s3_api_request_destroy(r);
    r = s3_api_request_create();
    stream = s3_api_text_create(r, prepared, NULL);
    assert(stream);
    /* Reject size before dereferencing this deliberately tiny input. */
    assert(s3_api_text_append(stream, "x", 1024 * 1024 + 1) == S3_INVALID);
    s3_api_text_destroy(stream);
    prepared_bytes = 0;
    prepared_result = S3_CANCELLED;
    stream = s3_api_text_create(r, prepared, NULL);
    assert(stream);
    assert(!s3_api_text_append(stream, "test", 4));
    assert(s3_api_text_finish(stream) == S3_CANCELLED);
    assert(s3_api_text_append(stream, "x", 1) == S3_CANCELLED);
    s3_api_text_destroy(stream);
    s3_api_request_destroy(r);
    s3_api_text_destroy(NULL);
}
int main(void) {
    assert(s3_api_version() == S3_ABI_VERSION);
    assert(s3_api_close() == S3_STATE);
    assert(s3_api_open() == S3_OK);
    assert(s3_api_open() == S3_BUSY);
    S3Request *r = s3_api_request_create();
    assert(r);
    const int16_t *pcm = NULL;
    uint32_t count = 0, pos;
    assert(s3_api_sample_at(0, &pos) == S3_INVALID);
    /* Explicit length: no trailing NUL is needed. */
    const char fragment[8] = {'B','o','n','j','o','u','r','.'};
    assert(s3_api_render(r, fragment, 8, 4, 5, 0, 0, &pcm, &count) == S3_OK);
    assert(pcm && count > 0);
    uint32_t expected = count;
    assert(s3_api_sample_at(8, &pos) == S3_OK && pos <= count);
    assert(s3_api_sample_at(9, &pos) == S3_INVALID);
    assert(s3_api_render(r, fragment, 8, 4, 5, 0, -100, &pcm, &count) == S3_OK);
    assert(count == expected);
    for (uint32_t i = 0; i < count; i++) assert(pcm[i] == 0);
    assert(s3_api_render(r, "a\0b", 3, 4, 5, 0, 0, &pcm, &count) == S3_INVALID);
    assert(!pcm && !count);
    assert(s3_api_sample_at(0, &pos) == S3_INVALID);
    assert(s3_api_render(r, fragment, 241, 4, 5, 0, 0, &pcm, &count) == S3_INVALID);
    assert(s3_api_render(r, fragment, 8, 16, 5, 0, 0, &pcm, &count) == S3_INVALID);
    assert(s3_api_render(r, fragment, 8, 4, 5, 101, 0, &pcm, &count) == S3_INVALID);
    assert(s3_api_render(r, fragment, 8, 4, 5, 0, 101, &pcm, &count) == S3_INVALID);
    assert(s3_api_render(NULL, fragment, 8, 4, 5, 0, 0, &pcm, &count) == S3_INVALID);
    assert(s3_api_render(r, NULL, 0, 4, 5, 0, 0, &pcm, &count) == S3_INVALID);
    assert(s3_api_render(r, fragment, 8, 4, 5, 0, 0, NULL, &count) == S3_INVALID);
    s3_api_request_cancel(r);
    assert(s3_api_render(r, fragment, 8, 4, 5, 100, 0, &pcm, &count) == S3_CANCELLED);
    assert(!pcm && !count);
    s3_api_request_destroy(r);
    running = s3_api_request_create();
    assert(running);
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, render_thread, NULL, 0, NULL);
    assert(thread);
#else
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, render_thread, NULL));
#endif
    while (!atomic_load(&started)) wait_ms(1);
    wait_ms(2);
    s3_api_request_cancel(running);
#ifdef _WIN32
    assert(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    CloseHandle(thread);
#else
    assert(!pthread_join(thread, NULL));
#endif
    assert(result == S3_CANCELLED);
    s3_api_request_destroy(running);
    r = s3_api_request_create();
    assert(r);
    assert(s3_api_render(r, fragment, 8, 4, 5, 0, 0, &pcm, &count) == S3_OK);
    assert(count == expected);
    assert(s3_api_render(r, "", 0, 4, 5, 0, 0, &pcm, &count) == S3_OK && count == 0);
    s3_api_request_destroy(r);
    assert(s3_api_close() == S3_OK);
    assert(s3_api_sample_at(0, &pos) == S3_STATE);
    assert(s3_api_open() == S3_OK);
    assert(s3_api_close() == S3_OK);
    check_preparation();
    puts("API lifecycle, preparation, validation, cancellation and reuse: OK");
    return 0;
}
