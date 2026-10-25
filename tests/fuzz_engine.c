/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Deterministic malformed Latin-1 and control-marker inputs. Run with ASan/UBSan.
 * In particular, legacy rule backtracking must not read past a text terminator.
 */
#include "dsp.h"
#include "engine.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static unsigned remaining;
static int cancel(void) { return 1; }
static int cancel_after(void) { return !--remaining; }
static void check_dsp_cancellation(void) {
    int16_t source[32768];
    for (size_t i = 0; i < 32768; i++)
        source[i] = (int16_t)(i * 43);
    const int16_t *out;
    size_t count;
    assert(!s3_process(source, 32768, 0, NULL, &out, &count));
    int16_t *reference = malloc(count * sizeof(*reference));
    assert(reference);
    memcpy(reference, out, count * sizeof(*reference));
    for (unsigned checkpoint = 1;; checkpoint++) {
        remaining = checkpoint;
        size_t actual = 0;
        int status = s3_process(source, 32768, 0, cancel_after, &out, &actual);
        if (!status) {
            assert(actual == count);
            assert(!memcmp(reference, out, count * sizeof(*reference)));
            break;
        }
        assert(status == 1 && actual == 0);
        assert(checkpoint < 1000);
    }
    free(reference);
}
int main(void) {
    s3_init();
    s3_dsp_init();
    check_dsp_cancellation();
    for (int k = 0; k < 3000; k++) {
        char text[241];
        int n = rand() % 241;
        for (int i = 0; i < n; i++)
            text[i] = (char)(1 + rand() % 255);
        text[n] = 0;
        const int16_t *pcm, *out;
        size_t count, m;
        assert(!s3_render(text, rand() % 16, rand() % 16, NULL, &pcm, &count));
        if (k < 2)
            assert(!s3_process(pcm, count, 0, NULL, &out, &m));
    }
    /* Exercise optional contours at extreme pitch/rate and after cancellation. */
    for (int pitch = 0; pitch <= 15; pitch += 15) {
        const int16_t *out;
        size_t count;
        assert(!s3_render_modulated("Bonjour, tu viens ? Oui !", 15, pitch, 100,
                                    cancel, &out, &count));
        assert(count == 0);
        assert(!s3_render_modulated("Bonjour, tu viens ? Oui !", 15, pitch, 100,
                                    NULL, &out, &count));
        assert(count > 0);
    }
    const int16_t *pcm;
    size_t n;
    assert(!s3_render("Bonjour.", 4, 6, cancel, &pcm, &n));
    assert(n == 0);
    assert(!s3_render("Bonjour.", 4, 6, NULL, &pcm, &n));
    assert(n == 8755);
    s3_close();
    s3_dsp_close();
    return 0;
}
