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
