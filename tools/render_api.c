/* SPDX-License-Identifier: GPL-2.0-or-later
 * Offline DLL/shared-library harness. UTF-8 file -> common text layer -> API PCM.
 * Usage: render_api INPUT.txt OUTPUT.pcm RATE PITCH RANGE VOLUME
 * No audio device is opened. Numeric parameters use engine/API units.
 */
#include "api.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    FILE *output;
    S3Request *request;
    int32_t rate, pitch, range, volume;
    unsigned long long samples;
} Renderer;
static int emit(void *context, const char *text, const S3Mark *marks, size_t bytes) {
    Renderer *r = context;
    const int16_t *pcm = NULL;
    uint32_t count = 0;
    (void)marks;
    int32_t status = s3_api_render(r->request, text, (uint32_t)bytes,
        r->rate, r->pitch, r->range, r->volume, &pcm, &count);
    if (status) {
        fprintf(stderr, "render status %d\n", (int)status);
        return -1;
    }
    /* Canonical little-endian PCM, independent of host byte order. */
    unsigned char packet[2048];
    for (uint32_t i = 0; i < count;) {
        uint32_t n = count - i < 1024 ? count - i : 1024;
        for (uint32_t j = 0; j < n; j++) {
            uint16_t value = (uint16_t)pcm[i + j];
            packet[j * 2] = (unsigned char)value;
            packet[j * 2 + 1] = (unsigned char)(value >> 8);
        }
        if (fwrite(packet, 2, n, r->output) != n)
            return -1;
        i += n;
    }
    r->samples += count;
    return 0;
}
static int emit_mark(void *context, const S3Mark *mark) {
    (void)context; (void)mark;
    return 0;
}
static int argument(const char *text, int32_t *out) {
    char *end;
    long value = strtol(text, &end, 10);
    if (!*text || *end || value < -100 || value > 100)
        return -1;
    *out = (int32_t)value;
    return 0;
}
int main(int argc, char **argv) {
    Renderer r = {0};
    if (argc != 7 || argument(argv[3], &r.rate) || argument(argv[4], &r.pitch) ||
        argument(argv[5], &r.range) || argument(argv[6], &r.volume) ||
        r.rate < 0 || r.rate > 15 || r.pitch < 0 || r.pitch > 15) {
        fprintf(stderr, "Usage: render_api INPUT.txt OUTPUT.pcm RATE(0..15) PITCH(0..15) RANGE(-100..100) VOLUME(-100..100)\n");
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 2;
    /* Bound the test harness input, as in the Linux adapter. */
    char *text = malloc(1024 * 1024 + 1);
    if (!text) { fclose(input); return 2; }
    size_t bytes = fread(text, 1, 1024 * 1024 + 1, input);
    int invalid = ferror(input) || bytes > 1024 * 1024 || memchr(text, 0, bytes) != NULL;
    fclose(input);
    if (invalid) { free(text); return 2; }
    text[bytes] = 0;
    if (s3_api_version() != S3_ABI_VERSION || s3_api_open()) { free(text); return 2; }
    r.request = s3_api_request_create();
    r.output = fopen(argv[2], "wb");
    if (!r.request || !r.output) {
        if (r.output) fclose(r.output);
        s3_api_request_destroy(r.request);
        s3_api_close(); free(text); return 2;
    }
    S3Text prepared = {.context = &r, .emit = emit, .emit_mark = emit_mark};
    clock_t start = clock();
    int status = s3_text_append(&prepared, text);
    if (!status) status = s3_text_finish(&prepared);
    s3_text_clear(&prepared);
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (fclose(r.output)) status = -1;
    s3_api_request_destroy(r.request);
    if (s3_api_close()) status = -1;
    free(text);
    printf("samples=%llu render_clock_seconds=%.6f\n", r.samples, elapsed);
    return status ? 1 : 0;
}
