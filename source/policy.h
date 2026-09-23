// The network policy this console applies, read from a signed v2 platform bundle
// (PRD emulators/prds/universal-tls-forwarding/01) instead of a table compiled into
// the NRO. SETUP-001: the hostnames live on the server that routes them, so adding a
// forwarder is a server deploy, not a new NRO on everybody's SD card.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define OPENPAK_HOST_MAX 160
#define OPENPAK_ADDR_MAX 46      // an IPv6 literal with room to spare
// A console does not need the schema's 10000. Anything near this is a bug upstream,
// and an unbounded array is a bundle deciding how much memory the NRO allocates.
#define OPENPAK_RULES_MAX 512

// One name the console redirects, already reduced to what dns_mitm reads:
// "*.nintendo.net" for a family, "accounts.nintendo.com" for a single name.
typedef struct {
    char host[OPENPAK_HOST_MAX];
    char address[OPENPAK_ADDR_MAX];  // "" = whatever server address the user configured
} openpak_rule;

// Where the rules in force came from, so the interface can say something true rather
// than implying the console is current when it is running the compiled fallback.
typedef enum {
    OPENPAK_SOURCE_BUILTIN,   // no bundle anywhere: the list compiled into this NRO
    OPENPAK_SOURCE_ROMFS,     // the bundle this NRO shipped with
    OPENPAK_SOURCE_CACHE,     // a bundle fetched from the network and cached on SD
} openpak_source;

typedef struct {
    openpak_rule  *rules;
    int            count;
    long long      sequence;   // 0 for the builtin fallback, which has no revision
    openpak_source source;
} openpak_policy;

const char *openpak_source_name(openpak_source s);

// Both return NULL and fill err on anything unexpected. Strict on purpose: a bundle
// this build cannot fully apply is refused, never applied in part.
openpak_policy *openpak_policy_parse(const char *json, size_t len, openpak_source src,
                                     char *err, int errlen);
openpak_policy *openpak_policy_load(const char *path, openpak_source src, char *err, int errlen);
void openpak_policy_free(openpak_policy *p);
