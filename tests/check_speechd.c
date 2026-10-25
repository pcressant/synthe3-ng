/* SPDX-License-Identifier: GPL-2.0-or-later
 * Manual integration test against an isolated real Speech Dispatcher server.
 * Uses its native client library and server-side audio at volume -100.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <libspeechd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RECORDS 1024
static struct {
    size_t id;
    unsigned events;
    double at[6];
} records[RECORDS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static double now(void) {
    struct timespec ts;
    assert(!clock_gettime(CLOCK_MONOTONIC, &ts));
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void event(size_t id, size_t client, SPDNotificationType type) {
    (void)client;
    assert(type >= SPD_EVENT_BEGIN && type <= SPD_EVENT_RESUME);
    pthread_mutex_lock(&mutex);
    size_t n = id % RECORDS;
    if (records[n].id != id) {
        memset(&records[n], 0, sizeof(records[n]));
        records[n].id = id;
    }
    records[n].events |= 1u << type;
    records[n].at[type] = now();
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&mutex);
}
static double wait_event(int id, SPDNotificationType type) {
    assert(id > 0);
    struct timespec deadline;
    assert(!clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += 5;
    pthread_mutex_lock(&mutex);
    size_t n = (unsigned)id % RECORDS;
    while (records[n].id != (unsigned)id || !(records[n].events & (1u << type))) {
        int err = pthread_cond_timedwait(&changed, &mutex, &deadline);
        if (err) {
            fprintf(stderr, "Missing event %d for message %d (events=%u)\n", type, id,
                    records[n].events);
            abort();
        }
    }
    double t = records[n].at[type];
    pthread_mutex_unlock(&mutex);
    return t;
}
int main(void) {
    assert(getenv("SPEECHD_ADDRESS")); /* no autospawn or user configuration changes */
    char *error = NULL;
    SPDConnectionAddress *address = spd_get_default_address(&error);
    assert(address);
    SPDConnection *c =
        spd_open2("synthe3-interruptions", "test", NULL, SPD_MODE_THREADED, address, 0, &error);
    SPDConnectionAddress__free(address);
    if (!c) {
        fprintf(stderr, "%s\n", error ? error : "Connection failed");
        return 1;
    }
    free(error);
    c->callback_begin = c->callback_end = c->callback_cancel = event;
    c->callback_pause = c->callback_resume = event;
    assert(!spd_set_notification_on(c, SPD_ALL));
    assert(!spd_set_output_module(c, "synthe3_ng"));
    assert(!spd_set_language(c, "fr"));
    assert(!spd_set_volume(c, -100));
    char long_text[40000] = "";
    for (int i = 0; i < 1000; i++)
        strcat(long_text, "Les oiseaux chantent dans les arbres. ");
    double max_cancel = 0, max_restart = 0;
    for (int i = 0; i < 30; i++) {
        assert(!spd_set_voice_rate(c, (int[]){-100, 0, 100}[i % 3]));
        int id = spd_say(c, SPD_TEXT, long_text);
        wait_event(id, SPD_EVENT_BEGIN);
        /* Let the server queue/consume some audio; interrupt at different phases. */
        struct timespec delay = {0, (i % 5) * 3000000L};
        nanosleep(&delay, NULL);
        double start = now();
        assert(!(i % 2 ? spd_cancel(c) : spd_stop(c)));
        double latency = wait_event(id, SPD_EVENT_CANCEL) - start;
        if (latency > max_cancel)
            max_cancel = latency;
        assert(latency < .5);
        assert(!spd_set_voice_rate(c, 100));
        start = now();
        id = spd_say(c, SPD_MESSAGE, "a");
        latency = wait_event(id, SPD_EVENT_BEGIN) - start;
        if (latency > max_restart)
            max_restart = latency;
        assert(latency < .5);
        wait_event(id, SPD_EVENT_END);
    }
    /* A higher-priority message must also preempt an ongoing reading. */
    double max_priority = 0;
    for (int i = 0; i < 5; i++) {
        int reading = spd_say(c, SPD_TEXT, long_text);
        wait_event(reading, SPD_EVENT_BEGIN);
        double start = now();
        int replacement = spd_say(c, SPD_MESSAGE, "a");
        wait_event(reading, SPD_EVENT_CANCEL);
        double latency = wait_event(replacement, SPD_EVENT_BEGIN) - start;
        if (latency > max_priority) max_priority = latency;
        assert(latency < .5);
        wait_event(replacement, SPD_EVENT_END);
    }
    /* Orca say-all pauses on server-generated marks and resumes the same message. */
    for (int i = 0; i < 3; i++) {
        int id = spd_say(c, SPD_TEXT, long_text);
        wait_event(id, SPD_EVENT_BEGIN);
        assert(!spd_pause(c));
        wait_event(id, SPD_EVENT_PAUSE);
        assert(!spd_resume(c));
        wait_event(id, SPD_EVENT_RESUME);
        assert(!spd_cancel(c));
        wait_event(id, SPD_EVENT_CANCEL);
    }
    printf("Speech Dispatcher, 30 interruptions : arrêt max %.1f ms, reprise max %.1f ms\n",
           max_cancel * 1000, max_restart * 1000);
    printf("Priorités : reprise max %.1f ms ; 3 pauses/reprises sur marqueurs réussies\n",
           max_priority * 1000);
    spd_close(c);
    return 0;
}
