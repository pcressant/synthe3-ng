/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    unsigned code;
    const char *name;
} punctuation[] = {{'.', "point"},
                   {',', "virgule"},
                   {';', "point virgule"},
                   {':', "deux points"},
                   {'!', "point d'exclamation"},
                   {'?', "point d'interrogation"},
                   {'/', "barre oblique"},
                   {'\\', "barre oblique inverse"},
                   {'-', "tiret"},
                   {'_', "souligné"},
                   {'+', "plus"},
                   {'=', "égal"},
                   {'*', "astérisque"},
                   {'#', "dièse"},
                   {'@', "arobase"},
                   {'&', "et commercial"},
                   {'%', "pour cent"},
                   {'(', "parenthèse ouvrante"},
                   {')', "parenthèse fermante"},
                   {'[', "crochet ouvrant"},
                   {']', "crochet fermant"},
                   {'{', "accolade ouvrante"},
                   {'}', "accolade fermante"},
                   {'<', "inférieur"},
                   {'>', "supérieur"},
                   {'"', "guillemet"},
                   {'\'', "apostrophe"},
                   {'|', "barre verticale"},
                   {'~', "tilde"},
                   {'^', "accent circonflexe"},
                   {'`', "accent grave"},
                   {'$', "dollar"},
                   {0x20ac, "euro"}};
const char *s3_punct_name(unsigned c) {
    for (size_t i = 0; i < sizeof(punctuation) / sizeof(*punctuation); i++)
        if (punctuation[i].code == c)
            return punctuation[i].name;
    return NULL;
}
void s3_text_clear(S3Text *c) {
    while (c->marks) {
        S3Mark *mark = c->marks;
        c->marks = mark->next;
        free(mark->name);
        free(mark);
    }
    c->last_mark = NULL;
}
static void flush(S3Text *c, size_t n) {
    char save = c->text[n];
    c->text[n] = 0;
    int nonspace = 0;
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)c->text[i]))
            nonspace = 1;
    if (nonspace && !c->status)
        c->status = c->emit(c->context, c->text, c->marks, n);
    while (c->marks && c->marks->offset <= n) {
        S3Mark *mark = c->marks;
        if (!nonspace && !c->status) {
            c->status = c->emit_mark(c->context, mark);
        }
        c->marks = mark->next;
        free(mark->name);
        free(mark);
    }
    if (!c->marks)
        c->last_mark = NULL;
    for (S3Mark *mark = c->marks; mark; mark = mark->next)
        mark->offset -= n;
    c->text[n] = save;
    memmove(c->text, c->text + n, c->n - n);
    c->n -= n;
}
static void byte(S3Text *c, char ch) {
    if (c->status)
        return;
    if (c->n == 240) {
        size_t end = 240;
        for (size_t i = 240; i > 0; i--)
            if (isspace((unsigned char)c->text[i - 1])) {
                end = i;
                break;
            }
        flush(c, end);
    }
    c->text[c->n++] = ch;
}
unsigned s3_text_decode(const unsigned char **p) {
    unsigned c = *(*p)++;
    if (c < 128)
        return c;
    int n = c >= 0xf0 && c <= 0xf4 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 ? 1 : 0;
    if (!n)
        return '?';
    unsigned code = c & ((1u << (6 - n)) - 1);
    for (int i = 0; i < n; i++) {
        if ((**p & 0xc0) != 0x80)
            return '?';
        code = (code << 6) | (*(*p)++ & 63);
    }
    return code;
}
static void normalized(S3Text *c, unsigned code) {
    switch (code) {
    case 0x153:
        byte(c, 'o');
        byte(c, 'e');
        return;
    case 0x152:
        byte(c, 'O');
        byte(c, 'E');
        return;
    case 0x2019:
        code = '\'';
        break;
    case 0x201c:
    case 0x201d:
        code = '"';
        break;
    case 0x2013:
    case 0x2014:
        code = '-';
        break;
    case 0:
        code = ' ';
        break;
    }
    byte(c, code <= 255 ? (char)code : '?');
}
static void literal(S3Text *c, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p && !c->status)
        normalized(c, s3_text_decode(&p));
}
int s3_text_append(S3Text *chunk, const char *s) {
    S3Text *c = chunk;
    const unsigned char *p = (const unsigned char *)s;
    int first = 1;
    unsigned checkpoint = 0;
    while (*p && !c->status) {
        if (!(checkpoint++ & 1023) && (c->stop && c->stop(c->context))) {
            c->status = 1;
            break;
        }
        unsigned code = s3_text_decode(&p);
        const char *name = s3_punct_name(code);
        if (c->spelling) {
            if (!first)
                literal(c, ", ");
            if (name)
                literal(c, name);
            else if (code <= 255 && isspace((unsigned char)code))
                literal(c, "espace");
            else
                normalized(c, code);
        } else if (name && (c->punctuation == 2 || (c->punctuation == 1 && code < 128 &&
                                                         strchr(".,;:!?", (int)code)))) {
            byte(c, ' ');
            literal(c, name);
            byte(c, ' ');
        } else
            normalized(c, code);
        first = 0;
    }
    return c->status;
}

int s3_text_mark(S3Text *c, const char *name) {
    if (c->status)
        return c->status;
    S3Mark *mark = calloc(1, sizeof(*mark));
    if (!mark)
        return c->status = -1;
    size_t length = strlen(name) + 1;
    mark->name = malloc(length);
    if (!mark->name) {
        free(mark);
        return c->status = -1;
    }
    memcpy(mark->name, name, length);
    mark->offset = c->n;
    if (c->last_mark)
        c->last_mark->next = mark;
    else
        c->marks = mark;
    c->last_mark = mark;
    return 0;
}
int s3_text_finish(S3Text *c) {
    flush(c, c->n);
    return c->status;
}
