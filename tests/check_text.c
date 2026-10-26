/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text.h"
#include <assert.h>
#include <string.h>

typedef struct {
    char text[1024];
    size_t bytes, chunks, marks, positions[8];
    int cancel, result;
} Sink;
static int mark(void *context, const S3Mark *m) {
    Sink *s = context;
    assert(!strcmp(m->name, "index"));
    assert(s->marks < 8);
    s->positions[s->marks++] = s->bytes + m->offset;
    return s->result;
}
static int emit(void *context, const char *text, const S3Mark *marks, size_t n) {
    Sink *s = context;
    assert(n <= 240 && strlen(text) == n);
    for (; marks && marks->offset <= n; marks = marks->next)
        mark(context, marks);
    assert(s->bytes + n < sizeof(s->text));
    memcpy(s->text + s->bytes, text, n);
    s->bytes += n;
    s->text[s->bytes] = 0;
    s->chunks++;
    return s->result;
}
static int stop(void *context) { return ((Sink *)context)->cancel; }
static S3Text prepare(Sink *s) {
    return (S3Text){.context = s, .emit = emit, .emit_mark = mark, .stop = stop};
}
int main(void) {
    Sink s = {0};
    S3Text c = prepare(&s);
    assert(!s3_text_append(&c, "cœur d’été — "));
    char name[] = "index";
    assert(!s3_text_mark(&c, name));
    name[0] = 'X'; /* The interface must copy the name. */
    assert(!s3_text_append(&c, "fin"));
    assert(!s3_text_finish(&c));
    assert(!strcmp(s.text, "coeur d'\xe9t\xe9 - fin"));
    assert(s.chunks == 1 && s.marks == 1 && s.positions[0] == 14);
    s3_text_clear(&c);

    s = (Sink){0}; c = prepare(&s);
    char prefix[241];
    memset(prefix, 'a', 200); prefix[200] = ' ';
    memset(prefix + 201, 'b', 39); prefix[240] = 0;
    assert(!s3_text_append(&c, prefix));
    assert(!s3_text_mark(&c, "index"));
    assert(!s3_text_mark(&c, "index"));
    assert(!s3_text_append(&c, "c")); /* Flush at the last space; carry marks. */
    assert(!s3_text_finish(&c));
    assert(s.bytes == 241 && s.chunks == 2 && s.marks == 2);
    assert(s.positions[0] == 240 && s.positions[1] == 240);
    s3_text_clear(&c);

    s = (Sink){0}; c = prepare(&s);
    assert(!s3_text_append(&c, "  "));
    assert(!s3_text_mark(&c, "index"));
    assert(!s3_text_finish(&c));
    assert(s.chunks == 0 && s.marks == 1 && s.positions[0] == 2);
    s3_text_clear(&c);

    s = (Sink){.cancel = 1}; c = prepare(&s);
    assert(!s3_text_mark(&c, "index"));
    assert(s3_text_append(&c, "interrompu") == 1);
    assert(s3_text_finish(&c) == 1);
    assert(s.chunks == 0 && s.marks == 0);
    s3_text_clear(&c);
    assert(!c.marks && !c.last_mark);

    s = (Sink){.result = -1}; c = prepare(&s);
    assert(!s3_text_append(&c, "erreur"));
    assert(s3_text_finish(&c) == -1);
    assert(s3_text_append(&c, "ignoré") == -1);
    s3_text_clear(&c);
    return 0;
}
