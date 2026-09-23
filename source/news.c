#include "news.h"

#include <stdio.h>
#include <string.h>
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

int openpak_news_subscribe(void) {
    // news:a carries every permission bit; the restricted services split
    // subscription and reception between them.
    if (R_FAILED(newsInitialize(NewsServiceType_Administrator)))
        return 0;
    int subscribed = 0;
    const char *format = probe_format();
    if (format) {
        for (int i = 0; i < TOPIC_COUNT; i++) {
            char filter[96];
            snprintf(filter, sizeof(filter), format, openpak_topics[i]);
            if (!ours(filter))
                continue;
            if (R_SUCCEEDED(newsSetSubscriptionStatus(filter, NEWS_STATUS_SUBSCRIBED)))
                subscribed++;
            // Ask for delivery now rather than waiting for the module's own
            // schedule. It fails harmlessly before the reboot that makes the
            // host rules live; the schedule still catches up afterwards.
            newsRequestImmediateReception(filter);
        }
    }
    newsExit();
    return subscribed;
}

void openpak_news_unsubscribe(void) {
    if (R_FAILED(newsInitialize(NewsServiceType_Administrator)))
        return;
    // Never ClearSubscriptionStatusAll: it would drop Nintendo's channels too.
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
