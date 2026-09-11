// Hosts-file half of the toggle: Atmosphère's dns_mitm reads /atmosphere/hosts/default.txt
// (sysMMC) and /atmosphere/hosts/emummc.txt (emuMMC), one "<ip> <hostname>" per line.
#pragma once
#include <stdbool.h>

#define OPENPAK_BEGIN "# >>> openpak >>>"
#define OPENPAK_END   "# <<< openpak <<<"
// Prefix stamped on someone else's redirect for a hostname we manage.
#define OPENPAK_DISABLED "#openpak-off# "
// Header above the inert research entries. They sit inside the block, so uncommenting one
// lasts until the next enable, which regenerates it. Make it permanent in hosts.c instead.
#define OPENPAK_RESEARCH_NOTE \
    "# researched, not served -- uncomment only once something answers on the other side:"

// One managed hostname. Wildcards are dns_mitm syntax.
//
// redirect=false is a name we have researched but do not serve. It is written into the block
// commented out: documented and one edit from live, but inert, because nothing of ours answers
// on those addresses yet.
//
// Not because the title works otherwise. OpenPak's audience is banned, jailbroken and emulated
// consoles,
// and every third party that validates a Nintendo-issued token upstream (Epic certainly,
// Demonware probably) refuses an OpenPak console anyway -- our identity is not Nintendo's, and
// a banned console cannot get Nintendo's. Those titles are already offline for the people who
// run this. Flipping one of these on costs nothing and gains nothing until a server exists.
typedef struct {
    const char *host;
    bool redirect;
    const char *address;   // NULL = the server address; set when a name must live elsewhere
} openpak_host;

extern const openpak_host openpak_hosts[];
extern const int openpak_hosts_count;   // every managed name, redirected or not
int openpak_hosts_active(void);         // only those actually pointed at our server

// Path prefix in front of /atmosphere/... — "" on the console, a temp dir in the host test.
extern const char *openpak_root;

bool openpak_enabled(void);                 // is our block present in any hosts file?
bool openpak_enable(const char *ip, char *err, int errlen);
bool openpak_disable(char *err, int errlen);
