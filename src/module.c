/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "dsp.h"
#include "engine.h"
#include "text.h"
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
static void report_mark(const S3Mark *mark) {
    if (stopped)
        return;
    module_report_index_mark((char *)mark->name);
    if (paused && !strncmp((char *)mark->name, "__spd_", 6))
        stopped = 1;
}
static int render(const char *text, const S3Mark *mark, size_t bytes) {
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
/* Transport callbacks keep the shared text layer independent of Speech Dispatcher. */
static int emit_text(void *context, const char *value, const S3Mark *marks, size_t bytes) {
    (void)context;
    return render(value, marks, bytes);
}
static int emit_mark(void *context, const S3Mark *mark) {
    (void)context;
    report_mark(mark);
    return stopped ? 1 : 0;
}
static int text_stop(void *context) {
    (void)context;
    return poll_input();
}
static S3Text new_text(void) {
    return (S3Text){.punctuation = current.punctuation, .spelling = current.spelling,
                    .emit = emit_text, .emit_mark = emit_mark, .stop = text_stop};
}
static int text(const char *s) {
    S3Text c = new_text();
    s3_text_append(&c, s);
    s3_text_finish(&c);
    s3_text_clear(&c);
    return c.status;
}
static int walk(xmlNode *node, S3Text *chunk) {
    for (; node && !poll_input(); node = node->next) {
        int status = 0;
        if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE)
            status = s3_text_append(chunk, (char *)node->content);
        else if (node->type == XML_ELEMENT_NODE) {
            if (!xmlStrcmp(node->name, BAD_CAST "mark")) {
                xmlChar *name = xmlGetProp(node, BAD_CAST "name");
                if (name && *name && !strpbrk((char *)name, "\r\n")) {
                    status = s3_text_mark(chunk, (char *)name);
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
        unsigned code = s3_text_decode(&p);
        const char *name = !*p ? s3_punct_name(code) : NULL;
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
            S3Text chunk = new_text();
            status = walk(xmlDocGetRootElement(doc)->children, &chunk);
            if (!status)
                s3_text_finish(&chunk);
            if (!status)
                status = chunk.status;
            s3_text_clear(&chunk);
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
