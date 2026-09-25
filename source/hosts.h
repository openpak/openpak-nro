// Hosts-file half of the toggle: Atmosphère's dns_mitm reads /atmosphere/hosts/default.txt
// (sysMMC) and /atmosphere/hosts/emummc.txt (emuMMC), one "<ip> <hostname>" per line.
//
// Which names go in there is no longer written down here. It comes from the signed
// platform bundle (see policy.h): the SD card's fetched copy if there is one, otherwise
// the bundle this NRO shipped with, otherwise the short list compiled in below as a
// last resort. Adding a forwarder is a server deploy, not a new NRO.
#pragma once
#include <stdbool.h>
#include "policy.h"
#include "ceiling.h"

#define OPENPAK_BEGIN "# >>> openpak >>>"
#define OPENPAK_END   "# <<< openpak <<<"
// Prefix stamped on someone else's redirect for a hostname we manage.
#define OPENPAK_DISABLED "#openpak-off# "

// Path prefix in front of /atmosphere/... — "" on the console, a temp dir in the host test.
extern const char *openpak_root;

// The rules in force, resolved on first use and cached. Never NULL: the compiled
// fallback always answers.
const openpak_policy *openpak_active_policy(void);
// Why a better source was passed over, or "" when there was nothing wrong. A bundle
// that is simply absent is not a problem and reports nothing.
const char *openpak_policy_problem(void);
// Drops the resolved policy so the next call picks up a newly fetched bundle.
void openpak_policy_reload(void);
// Profile names the signed ceiling left out (comma separated), "" when none were.
const char *openpak_policy_dropped(void);
// The ceiling in force: the cached verified one, else the frozen list's families
// (version 0).
const openpak_ceiling *openpak_ceiling_current(void);
// Set by the online check: the saved profile was fetched this launch.
void openpak_policy_set_fresh(bool fresh);

// The effective-set digest (ceiling.h) of what selecting OpenPak would install now, and of
// what is installed (recorded at install, else read back from the hosts block). The second
// returns false when OpenPak is not installed.
void openpak_pending_digest(const char *ip, char out[65]);
bool openpak_installed_digest(char out[65]);

bool openpak_glob(const char *pattern, const char *string);  // dns_mitm's wildcard match
bool openpak_enabled(void);                 // is our block present in any hosts file?
bool openpak_enable(const char *ip, char *err, int errlen);
bool openpak_disable(char *err, int errlen);
