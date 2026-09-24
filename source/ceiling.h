// The signed redirect ceiling (docs/signed-ceiling.md, platform "switch"): the domain
// families this console may redirect at all, signed with an Ed25519 key that never touches
// the server. The live network profile says what to redirect now; every name in it is
// checked against the verified ceiling and dropped on its own if it lies outside, so a new
// family is a server change and a compromised box still cannot widen what a console sends
// to OpenPak.
//
// Pure file and JSON work: this half runs in `make test`. Fetching is netfetch.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "policy.h"

#define OPENPAK_CEILING_MAX 64
#define OPENPAK_FAMILY_MAX  128

// Cache files, under openpak_root (see hosts.h).
#define OPENPAK_CEILING_CACHE   "/switch/openpak/ceiling.json"     // envelope, bytes as received
#define OPENPAK_CEILING_VERSION "/switch/openpak/ceiling.version"  // highest version ever accepted
#define OPENPAK_PROFILE_CACHE   "/switch/openpak/profile.json"     // last profile that parsed
#define OPENPAK_DIGEST_RECORD   "/switch/openpak/redirects.digest" // effective set last installed

typedef struct {
    long long version;        // 0 = the compiled fallback, not a signed file
    int       count;
    char      families[OPENPAK_CEILING_MAX][OPENPAK_FAMILY_MAX];   // ".nintendo.net", ...
} openpak_ceiling;

typedef struct {
    const char *keyid;        // hex sha256 of the raw key
    uint8_t     pub[32];
} openpak_pinned_key;

// The keys a ceiling is accepted under. A list so a successor can be pinned before the
// current key is retired. The host test points these at its own key.
extern const openpak_pinned_key *openpak_ceiling_keys;
extern int openpak_ceiling_key_count;

// The contract's family rule: leading dot, lower case, at least two labels.
bool openpak_family_valid(const char *family);
// A name ("x.ea.com", "ea.com") or a family (".x.ea.com") inside the ceiling.
bool openpak_ceiling_covers(const openpak_ceiling *c, const char *name);

// Verifies an envelope and fills out with this console's families. Any malformed family on
// any platform rejects the whole file.
bool openpak_ceiling_verify(const char *envelope, size_t len, openpak_ceiling *out,
                            char *err, int errlen);
// The cached envelope, re-verified. err is "" when there simply is none.
bool openpak_ceiling_load_cached(openpak_ceiling *out, char *err, int errlen);
// Offers freshly fetched bytes: accepted only if they verify and version >= the highest
// ever accepted (rollback protection); then cached atomically, bytes as received.
bool openpak_ceiling_accept(const char *envelope, size_t len, openpak_ceiling *out,
                            char *err, int errlen);

// The profile (GET /api/v1/network/profile?platform=switch) filtered through c. Names
// outside the ceiling are left out one by one and listed in dropped; only a profile that
// is not a switch profile at all is refused (NULL, err filled).
openpak_policy *openpak_profile_policy(const char *json, size_t len, const openpak_ceiling *c,
                                       openpak_source src, char *dropped, int droppedlen,
                                       char *err, int errlen);

// The contract's effective-set digest: sha256, lower-case hex, over the sorted
// "family:", "exact:", "override:name=ip" and "address:ip" entries, one per line.
void openpak_policy_digest(const openpak_policy *p, const char *ip, char out[65]);

// Temp file + rename, 0600. Creates /switch/openpak if needed.
bool openpak_write_file_atomic(const char *path, const void *data, size_t len,
                               char *err, int errlen);
// Whole file, NUL-terminated, at most max bytes; NULL if absent or too large.
char *openpak_read_file(const char *path, size_t max, size_t *len_out);
