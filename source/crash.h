// Console half of failure reports (see report.h): collect them, keep them on the SD card,
// and send them — only ever after the user agreed, once or "always".
#pragma once
#include "report.h"

// Call once at launch: installs the crash hook and notes the firmware/Atmosphère versions.
// ui is "sdl" or "console".
void openpak_report_init(const char *ui);

// Saves a report of one of this tool's own failures ("install", "remove", "startup", ...),
// unless the user chose "never". Nothing is sent here.
void openpak_report_failure(const char *action, const char *error_code, const char *message);

int  openpak_report_pending(void);                 // reports waiting on the SD card
report_consent openpak_report_consent(void);
void openpak_report_set_consent(report_consent c);
// Sends every pending report, dropping each one the server took. Fills msg for the status
// line and returns the number sent. Reports that did not go stay for next time.
int  openpak_report_send_all(char *msg, int msglen);
void openpak_report_discard_all(void);

// After a failure (or at launch): if reports are waiting, applies a remembered "always" or
// "never", otherwise puts lead + a question in status and returns true — the UI then shows
// Send / Don't send / Always / Never. lead may be status itself.
bool openpak_report_offer(char *status, int len, const char *lead);
