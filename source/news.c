#include "news.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <switch.h>

// OpenPak's channels, as served by the topics host. Subscribing is per topic;
// the player can still unfollow any of them in the News applet.
static const char *const openpak_topics[] = {
    "openpak_news", "openpak_games", "openpak_workshop", "openpak_help",
};
#define TOPIC_COUNT ((int)(sizeof(openpak_topics)/sizeof(*openpak_topics)))

// INewsService takes a NUL-terminated filter that is a bare topic id
// ([A-Za-z0-9_]{1,31}), not a SQL-style where clause. The two `topic_id='…'`
// spellings this once probed always returned 0x47d against 22.5.0's module, so
// only the bare id is used now. Still probed read-only before anything is written.
static const char *const filter_formats[] = {
    "%s",
};
#define FORMAT_COUNT ((int)(sizeof(filter_formats)/sizeof(*filter_formats)))

// SetSubscriptionStatus values (22.5.0 News module): 0 = forget the topic,
// 1 = unsubscribed (listed but not followed), 2 = subscribed, 3 = auto. The
// module only fetches and stores records for topics at status 2, so following
// one of our channels must send 2 — sending 1 leaves it listed but unfetched
// and the module refuses to store its records (0xd47d).
#define NEWS_STATUS_FORGET      0
#define NEWS_STATUS_SUBSCRIBED  2

// A filter that does not name one of our own topics must never be written:
// "1" or an empty string would subscribe the console to every topic there is.
static bool ours(const char *filter) {
    return strstr(filter, "openpak_") != NULL;
}

// Returns the format string the service accepts, or NULL. Read-only.
static const char *probe_format(void) {
    for (int i = 0; i < FORMAT_COUNT; i++) {
        char filter[96];
        snprintf(filter, sizeof(filter), filter_formats[i], openpak_topics[0]);
        u32 status = 0;
        if (ours(filter) && R_SUCCEEDED(newsGetSubscriptionStatus(filter, &status)))
            return filter_formats[i];
    }
    return NULL;
}

// ---- one-time reset of what Nintendo left in the News module ---------------------------------
//
// A console that followed Nintendo's channels keeps their records and subscriptions, and once the
// network is OpenPak's it can no longer list those channels to unfollow them. The reset backs the
// module's database up to the SD card first (a private archive, never uploaded), and only if that
// worked clears the stored news and every subscription. It runs once; the marker is the proof.
#define NEWS_DIR     "/switch/openpak"
#define NEWS_BACKUPS "/switch/openpak/news-backup"
#define NEWS_MARKER  "/switch/openpak/news-reset-done"
#define NEWS_DUMP_CAP (8u << 20)

int openpak_news_reset_once(char *err, size_t errsz) {
    FILE *m = fopen(NEWS_MARKER, "rb");
    if (m) { fclose(m); return 0; }
    if (R_FAILED(newsInitialize(NewsServiceType_Administrator))) {
        snprintf(err, errsz, "News service unavailable, old news left alone");
        return -1;
    }
    int rc = -1;
    void *buf = malloc(NEWS_DUMP_CAP);
    if (!buf) {
        snprintf(err, errsz, "No memory for the news backup, old news left alone");
        newsExit();
        return -1;
    }
    u64 out = 0;
    Result r = newsGetNewsDatabaseDump(buf, NEWS_DUMP_CAP, &out);
    if (R_FAILED(r) || out >= NEWS_DUMP_CAP) {
        snprintf(err, errsz, "Could not back up the News database (0x%x), old news left alone", r);
    } else {
        char path[160];
        mkdir(NEWS_DIR, 0777);
        mkdir(NEWS_BACKUPS, 0777);
        snprintf(path, sizeof(path), NEWS_BACKUPS "/news-%lld.bin", (long long)time(NULL));
        FILE *f = fopen(path, "wb");
        bool saved = f && fwrite(buf, 1, (size_t)out, f) == (size_t)out;
        if (f && fclose(f) != 0) saved = false;
        if (!saved) {
            snprintf(err, errsz, "Could not write the News backup, old news left alone");
        } else if (R_FAILED(newsClearStorage())) {
            snprintf(err, errsz, "Backed up, but the News module refused to clear its storage");
        } else if (R_FAILED(newsClearSubscriptionStatusAll())) {
            snprintf(err, errsz, "Backed up and cleared, but old subscriptions could not be reset");
            rc = 1;
        } else {
            rc = 1;
        }
        if (rc == 1) {
            FILE *mk = fopen(NEWS_MARKER, "wb");
            if (mk) { fprintf(mk, "%s\n", path); fclose(mk); }
        }
    }
    free(buf);
    newsExit();
    return rc;
}

// One line per News call on the SD card, so a channel that never arrives can be told apart from
// a call the module refused. Best effort: a failed write never changes the outcome.
static void news_log(const char *fmt, ...) {
    mkdir(NEWS_DIR, 0777);
    FILE *f = fopen(NEWS_DIR "/news.log", "ab");
    if (!f) return;
    fprintf(f, "%lld ", (long long)time(NULL));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// The module appears to take one immediate reception at a time: the first topic fetched and the
// other three never did when all four were asked back to back. Ask one at a time, give it a
// moment to start, and ask again if it refused.
#define RECEIVE_ATTEMPTS 3
#define RECEIVE_PAUSE_NS 2000000000ull

static void request_reception(const char *filter) {
    for (int attempt = 1; attempt <= RECEIVE_ATTEMPTS; attempt++) {
        Result r = newsRequestImmediateReception(filter);
        news_log("receive %s attempt %d -> 0x%x", filter, attempt, r);
        svcSleepThread(RECEIVE_PAUSE_NS);
        if (R_SUCCEEDED(r))
            return;
    }
}

int openpak_news_subscribe(void) {
    // news:a carries every permission bit; the restricted services split
    // subscription and reception between them.
    Result init = newsInitialize(NewsServiceType_Administrator);
    if (R_FAILED(init)) {
        news_log("news:a initialize -> 0x%x", init);
        return 0;
    }
    int subscribed = 0;
    const char *format = probe_format();
    if (!format)
        news_log("subscription filter probe failed; nothing subscribed");
    if (format) {
        for (int i = 0; i < TOPIC_COUNT; i++) {
            char filter[96];
            snprintf(filter, sizeof(filter), format, openpak_topics[i]);
            if (!ours(filter))
                continue;
            Result r = newsSetSubscriptionStatus(filter, NEWS_STATUS_SUBSCRIBED);
            u32 status = 0;
            Result g = newsGetSubscriptionStatus(filter, &status);
            news_log("subscribe %s -> 0x%x, status read back 0x%x = %u", filter, r, g, status);
            if (R_SUCCEEDED(r))
                subscribed++;
        }
        // Ask for delivery now rather than waiting for the module's own schedule. It fails
        // harmlessly before the reboot that makes the host rules live; the schedule catches up.
        for (int i = 0; i < TOPIC_COUNT; i++) {
            char filter[96];
            snprintf(filter, sizeof(filter), format, openpak_topics[i]);
            if (ours(filter))
                request_reception(filter);
        }
    }
    newsExit();
    return subscribed;
}

void openpak_news_unsubscribe(void) {
    if (R_FAILED(newsInitialize(NewsServiceType_Administrator)))
        return;
    // Never ClearSubscriptionStatusAll here: it would drop Nintendo's channels too. The one-time
    // openpak_news_reset_once is the exception, and a player who goes back to Nintendo re-follows
    // their channels in the News applet (the backup is an archive, not something this tool restores).
    // Forget (0) rather than unsubscribe (1): selecting Nintendo is a full revert,
    // so our topics should leave the module's list entirely rather than linger as
    // listed-but-unfollowed entries the player would still see in the News applet.
    for (int i = 0; i < FORMAT_COUNT; i++) {
        for (int t = 0; t < TOPIC_COUNT; t++) {
            char filter[96];
            snprintf(filter, sizeof(filter), filter_formats[i], openpak_topics[t]);
            if (ours(filter))
                newsSetSubscriptionStatus(filter, NEWS_STATUS_FORGET);
        }
    }
    newsExit();
}
