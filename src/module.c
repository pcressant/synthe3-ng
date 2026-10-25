/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "dsp.h"
#include "engine.h"
#include <ctype.h>
#include <libxml/parser.h>
#include <math.h>
#include <spd_module_main.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int rate, pitch, pitch_range, volume, punctuation, spelling;
} Settings;
static Settings settings, current;
static int active, stopped, paused, audio_server, closing;
/* Wall-clock budget, independent of pitch, rate and DSP callback frequency.
 * clock_gettime is normally a vDSO read; only actual protocol polls use select.
 * No timer, wake-up or polling when idle: module_loop blocks on the input pipe.
 */
#define INPUT_BUDGET_NS 2000000L
static struct timespec next_input;
static SPDVoice voice = {"Synthé3-ng", "fr", "none"};
static SPDVoice *voices[] = {&voice, NULL};
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
static const char *punct_name(unsigned c) {
    for (size_t i = 0; i < sizeof(punctuation) / sizeof(*punctuation); i++)
        if (punctuation[i].code == c)
            return punctuation[i].name;
    return NULL;
}
static int process_input(void) {
    if (!module_process(STDIN_FILENO, 0))
        closing = stopped = 1;
    return stopped;
}
static int poll_input(void) {
    if (stopped)
        return 1;
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec > next_input.tv_sec ||
        (now.tv_sec == next_input.tv_sec && now.tv_nsec >= next_input.tv_nsec)) {
        process_input();
        next_input = now;
        next_input.tv_nsec += INPUT_BUDGET_NS;
        if (next_input.tv_nsec >= 1000000000L) {
            next_input.tv_sec++;
            next_input.tv_nsec -= 1000000000L;
        }
    }
    return stopped;
}
#ifndef S3_PITCH_BASE
#define S3_PITCH_BASE 5
#endif
static int setting(int value, int base) {
    return (int)nearbyint(base + value * (value < 0 ? base : 15 - base) / 100.0);
}
static int rate_setting(int value) {
    if (value <= 0)
        return setting(value, 4);
    double calibrated = value <= 54 ? value * 80.0 / 54 : 80 + (value - 54) * 20.0 / 46;
    return (int)nearbyint(4 + calibrated * 11 / 100);
}
typedef struct Mark {
    xmlChar *name;
    size_t offset;
    struct Mark *next;
} Mark;
static void report_mark(const Mark *mark) {
    if (stopped)
        return;
    module_report_index_mark((char *)mark->name);
    if (paused && !strncmp((char *)mark->name, "__spd_", 6))
        stopped = 1;
}
static int render(const char *text, Mark *mark, size_t bytes) {
    const int16_t *raw, *pcm;
    size_t n, count;
    if (stopped)
        return 1;
    if (s3_render_modulated(text, rate_setting(current.rate), setting(current.pitch, S3_PITCH_BASE),
                            current.pitch_range, poll_input, &raw,
                  &n))
        return -1;
    if (poll_input())
        return 1;
    int status = s3_process(raw, n, current.volume, poll_input, &pcm, &count);
    if (status)
        return status;
    if (process_input())
        return 1;
    const uint16_t endian = 1;
    size_t i = 0;
    while (!stopped) {
        size_t boundary = count;
        if (mark && mark->offset <= bytes) {
            boundary = (size_t)nearbyint((double)s3_sample_at(mark->offset) * 22050 / 10000);
            if (boundary > count)
                boundary = count;
            if (boundary <= i) {
                report_mark(mark);
                mark = mark->next;
                continue;
            }
        }
        if (i == count)
            break;
        size_t packet = boundary - i < 1024 ? boundary - i : 1024;
        AudioTrack track = {16, 1, 22050, (int)packet, (short *)(pcm + i)};
        module_tts_output_server(&track,
                                 *(const unsigned char *)&endian ? SPD_AUDIO_LE : SPD_AUDIO_BE);
        i += packet;
    }
    return stopped ? 1 : 0;
}
/* Incremental Latin-1 normalization, bounded by the historical 240-byte chunks. */
typedef struct {
    char text[241];
    size_t n;
    int status;
    Mark *marks, *last_mark;
} Chunk;
static void clear_marks(Chunk *c) {
    while (c->marks) {
        Mark *mark = c->marks;
        c->marks = mark->next;
        xmlFree(mark->name);
        free(mark);
    }
    c->last_mark = NULL;
}
static void flush(Chunk *c, size_t n) {
    char save = c->text[n];
    c->text[n] = 0;
    int nonspace = 0;
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)c->text[i]))
            nonspace = 1;
    if (nonspace && !c->status)
        c->status = render(c->text, c->marks, n);
    while (c->marks && c->marks->offset <= n) {
        Mark *mark = c->marks;
        if (!nonspace && !c->status) {
            report_mark(mark);
            if (stopped)
                c->status = 1;
        }
        c->marks = mark->next;
        xmlFree(mark->name);
        free(mark);
    }
    if (!c->marks)
        c->last_mark = NULL;
    for (Mark *mark = c->marks; mark; mark = mark->next)
        mark->offset -= n;
    c->text[n] = save;
    memmove(c->text, c->text + n, c->n - n);
    c->n -= n;
}
static void byte(Chunk *c, char ch) {
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
static unsigned decode(const unsigned char **p) {
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
static void normalized(Chunk *c, unsigned code) {
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
static void literal(Chunk *c, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p && !c->status)
        normalized(c, decode(&p));
}
static int append_text(Chunk *chunk, const char *s) {
    Chunk *c = chunk;
    const unsigned char *p = (const unsigned char *)s;
    int first = 1;
    unsigned checkpoint = 0;
    while (*p && !c->status) {
        if (!(checkpoint++ & 1023) && poll_input()) {
            c->status = 1;
            break;
        }
        unsigned code = decode(&p);
        const char *name = punct_name(code);
        if (current.spelling) {
            if (!first)
                literal(c, ", ");
            if (name)
                literal(c, name);
            else if (code <= 255 && isspace((unsigned char)code))
                literal(c, "espace");
            else
                normalized(c, code);
        } else if (name && (current.punctuation == 2 || (current.punctuation == 1 && code < 128 &&
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
static int text(const char *s) {
    Chunk c = {0};
    append_text(&c, s);
    flush(&c, c.n);
    clear_marks(&c);
    return c.status;
}
static int walk(xmlNode *node, Chunk *chunk) {
    for (; node && !poll_input(); node = node->next) {
        int status = 0;
        if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE)
            status = append_text(chunk, (char *)node->content);
        else if (node->type == XML_ELEMENT_NODE) {
            if (!xmlStrcmp(node->name, BAD_CAST "mark")) {
                xmlChar *name = xmlGetProp(node, BAD_CAST "name");
                if (name && *name && !strpbrk((char *)name, "\r\n")) {
                    Mark *mark = calloc(1, sizeof(*mark));
                    if (!mark) {
                        xmlFree(name);
                        return chunk->status = -1;
                    }
                    mark->name = name;
                    mark->offset = chunk->n;
                    if (chunk->last_mark)
                        chunk->last_mark->next = mark;
                    else
                        chunk->marks = mark;
                    chunk->last_mark = mark;
                    name = NULL;
                }
                xmlFree(name);
            } else
                status = walk(node->children, chunk);
        }
        if (status)
            return status;
    }
    return stopped ? 1 : 0;
}
int module_config(const char *file) {
    /* No runtime resources or configurable subprocesses. Reject obsolete options. */
    if (!file)
        return 0;
    FILE *f = fopen(file, "r");
    if (!f)
        return -1;
    char line[1024];
    int result = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;
        if (*p && *p != '#') {
            result = -1;
            break;
        }
    }
    fclose(f);
    return result;
}
int module_init(char **msg) {
    s3_init();
    s3_dsp_init();
    xmlInitParser();
    module_audio_set_server();
    *msg = strdup("Synthé3-ng C, server audio");
    return 0;
}
SPDVoice **module_list_voices(void) { return voices; }
int module_loop(void) { return module_process(STDIN_FILENO, 1); }
int module_set(const char *var, const char *val) {
    int *dest = NULL;
    if (!strcmp(var, "rate"))
        dest = &settings.rate;
    else if (!strcmp(var, "pitch"))
        dest = &settings.pitch;
    else if (!strcmp(var, "pitch_range"))
        dest = &settings.pitch_range;
    else if (!strcmp(var, "volume"))
        dest = &settings.volume;
    if (dest) {
        char *end;
        long v = strtol(val, &end, 10);
        if (!*val || *end || v < -100 || v > 100)
            return -1;
        *dest = (int)v;
    } else if (!strcmp(var, "punctuation_mode")) {
        if (!strcmp(val, "none"))
            settings.punctuation = 0;
        else if (!strcmp(val, "some"))
            settings.punctuation = 1;
        else if (!strcmp(val, "all") || !strcmp(val, "most"))
            settings.punctuation = 2;
        else
            return -1;
    } else if (!strcmp(var, "spelling_mode")) {
        if (strcmp(val, "on") && strcmp(val, "off"))
            return -1;
        settings.spelling = !strcmp(val, "on");
    }
    return 0;
}
int module_audio_set(const char *var, const char *val) {
    if (!strcmp(var, "audio_output_method")) {
        if (strcmp(val, "server"))
            return -1;
        audio_server = 1;
    }
    return 0;
}
int module_audio_init(char **msg) {
    *msg = strdup("server audio");
    return audio_server ? 0 : -1;
}
int module_loglevel_set(const char *var, const char *val) {
    (void)var;
    (void)val;
    return 0;
}
int module_debug(int enable, const char *file) {
    (void)enable;
    (void)file;
    return 0;
}
int module_stop(void) {
    stopped = 1;
    paused = 0;
    return 0;
}
size_t module_pause(void) {
    /* A late PAUSE must not turn an already accepted STOP into a pause. */
    if (active && !stopped)
        paused = 1;
    return 0;
}
int module_close(void) {
    if (active) {
        /* libspeechd_module may dispatch QUIT inside a synthesis callback.
         * Defer destruction until the engine and DSP have unwound. */
        closing = stopped = 1;
        return 0;
    }
    s3_close();
    s3_dsp_close();
    xmlCleanupParser();
    return 0;
}
void module_speak_sync(const char *data, size_t bytes, SPDMessageType type) {
    if (active || !bytes || bytes > 1024 * 1024 || memchr(data, 0, bytes)) {
        module_speak_error();
        return;
    }
    active = 1;
    stopped = paused = 0;
    next_input = (struct timespec){0};
    current = settings;
    module_speak_ok();
    module_report_event_begin();
    int status = 0;
    if (type == SPD_MSGTYPE_CHAR || type == SPD_MSGTYPE_KEY) {
        static const char *keys[][2] = {{"space", "espace"},
                                        {"Return", "entrée"},
                                        {"Tab", "tabulation"},
                                        {"Escape", "échappement"},
                                        {"BackSpace", "retour arrière"}};
        const char *value = data;
        for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); i++)
            if (!strcmp(data, keys[i][0]))
                value = keys[i][1];
        const unsigned char *p = (const unsigned char *)value;
        unsigned code = decode(&p);
        const char *name = !*p ? punct_name(code) : NULL;
        status = text(name ? name : value);
    } else if (!memchr(data, '<', bytes) && !memchr(data, '&', bytes)) {
        status = text(data);
    } else {
        /* Build the same XML tree incrementally so long Orca messages can be
         * cancelled during parsing, before producing any audio or marks. */
        xmlParserCtxt *parser = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
        xmlDoc *doc = NULL;
        int error = !parser;
        if (parser) {
            xmlCtxtUseOptions(parser, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
            error = xmlParseChunk(parser, "<root>", 6, 0);
            for (size_t i = 0; i < bytes && !error && !poll_input(); i += 4096) {
                size_t n = bytes - i < 4096 ? bytes - i : 4096;
                error = xmlParseChunk(parser, data + i, (int)n, 0);
            }
            if (!error && !poll_input())
                error = xmlParseChunk(parser, "</root>", 7, 1);
            doc = parser->myDoc;
            parser->myDoc = NULL;
            xmlFreeParserCtxt(parser);
        }
        if (stopped)
            status = 1;
        else if (!error && doc && !doc->intSubset && !doc->extSubset) {
            Chunk chunk = {0};
            status = walk(xmlDocGetRootElement(doc)->children, &chunk);
            if (!status)
                flush(&chunk, chunk.n);
            if (!status)
                status = chunk.status;
            clear_marks(&chunk);
        }
        else {
            /* Malformed SSML: strip tags; retain ordinary text. */
            char *plain = malloc(bytes + 1);
            if (!plain)
                status = -1;
            else {
                size_t n = 0;
                int tag = 0;
                for (size_t i = 0; i < bytes; i++) {
                    if (!(i & 4095) && poll_input())
                        break;
                    if (data[i] == '<')
                        tag = 1;
                    else if (data[i] == '>' && tag)
                        tag = 0;
                    else if (!tag)
                        plain[n++] = data[i];
                }
                plain[n] = 0;
                status = stopped ? 1 : text(plain);
                free(plain);
            }
        }
        xmlFreeDoc(doc);
    }
    if (closing) {
        active = 0;
        module_close();
        exit(0); /* QUIT was already acknowledged by the native library. */
    }
    if (stopped && paused)
        module_report_event_pause();
    else if (stopped || status)
        module_report_event_stop();
    else
        module_report_event_end();
    active = 0;
}
