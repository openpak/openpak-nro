// Hosts-file half of the toggle: Atmosphère's dns_mitm reads /atmosphere/hosts/default.txt
// (sysMMC) and /atmosphere/hosts/emummc.txt (emuMMC), one "<ip> <hostname>" per line.
#pragma once
#include <stdbool.h>

#define OPENPAK_BEGIN "# >>> openpak >>>"
#define OPENPAK_END   "# <<< openpak <<<"
// Prefix stamped on someone else's redirect for a hostname we manage.
#define OPENPAK_DISABLED "#openpak-off# "

// Every name the OpenPak Switch adapter answers for. Wildcards are dns_mitm syntax.
extern const char *const openpak_hosts[];
extern const int openpak_hosts_count;

// Path prefix in front of /atmosphere/... — "" on the console, a temp dir in the host test.
extern const char *openpak_root;

bool openpak_enabled(void);                 // is our block present in any hosts file?
bool openpak_enable(const char *ip, char *err, int errlen);
bool openpak_disable(char *err, int errlen);
