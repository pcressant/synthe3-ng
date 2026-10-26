/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef S3_API_H
#define S3_API_H
#include <stdint.h>
#ifdef _WIN32
#define S3_CALL __cdecl
#ifdef S3_API_BUILD
#define S3_API __declspec(dllexport)
#else
#define S3_API __declspec(dllimport)
#endif
#else
#define S3_CALL
#define S3_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define S3_ABI_VERSION 1u
#define S3_OK 0
#define S3_CANCELLED 1
#define S3_INVALID -1
#define S3_STATE -2
#define S3_BUSY -3
#define S3_NOMEM -4
#define S3_RENDER_ERROR -5
/* One engine per loaded library. open/close/render/sample_at are serialized by
 * the caller (normally one persistent worker). A concurrent operation returns
 * BUSY. Do not mix this API with direct engine/DSP calls in the same process.
 * Requests are separate cancellation tokens: create before publishing work,
 * cancel from any thread, destroy only after all users have finished. A token
 * cannot be reset; it may span several fragments of the same utterance. */
typedef struct S3Request S3Request;
S3_API uint32_t S3_CALL s3_api_version(void);
S3_API int32_t S3_CALL s3_api_open(void);
S3_API int32_t S3_CALL s3_api_close(void);
S3_API S3Request *S3_CALL s3_api_request_create(void);
S3_API void S3_CALL s3_api_request_cancel(S3Request *);
S3_API void S3_CALL s3_api_request_destroy(S3Request *);
/* Input is <=240 Latin-1 bytes, length explicit, without embedded NUL. Rate and
 * pitch are engine levels 0..15, range -100..100 (<=0 disables extra contours),
 * volume -100..100 (Speech Dispatcher units: -100 silence, 0 unity, 100 x2).
 * Output is borrowed mono signed int16 PCM at 22050 Hz. Copy/consume before any
 * next render or close. Error/cancellation return NULL and zero output.
 * No audio devices, Python callbacks, files or threads are created here. */
S3_API int32_t S3_CALL s3_api_render(S3Request *, const char *, uint32_t length,
    int32_t rate, int32_t pitch, int32_t range, int32_t volume,
    const int16_t **pcm, uint32_t *samples);
/* Approximate preceding word-boundary position in the last successful render,
 * converted to output samples and clamped. Invalidated by the next render/close.
 * Offset counts Latin-1 bytes in that fragment, not Unicode code points. */
S3_API int32_t S3_CALL s3_api_sample_at(uint32_t byte_offset, uint32_t *sample);
#ifdef __cplusplus
}
#endif
#endif
