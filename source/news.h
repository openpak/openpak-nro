// News half of the toggle: the console's own News module is told to follow
// OpenPak's channels, so the stock News applet fills itself.
//
// Selecting OpenPak already installs the two News patches and redirects the
// topics host; without a subscription the module still never fetches them.
#pragma once
#include <stdbool.h>

// Subscribe the News module to OpenPak's channels and ask it to receive now.
// Returns the number of channels subscribed; 0 means the service refused
// every filter spelling, which is not fatal to the rest of the setup.
int openpak_news_subscribe(void);

// Drop only OpenPak's subscriptions, never the console's Nintendo channels.
void openpak_news_unsubscribe(void);

// One-time reset of the News module: back its database up to
// /switch/openpak/news-backup/, then clear the stored news and every subscription (Nintendo's
// channels can no longer be listed to unfollow once the network is OpenPak's). Nothing is cleared
// unless the backup was written. Returns 1 when it ran, 0 when it already had (marker file), and
// -1 with a message in err when it did nothing; callers then continue with openpak_news_subscribe.
#include <stddef.h>
int openpak_news_reset_once(char *err, size_t errsz);
