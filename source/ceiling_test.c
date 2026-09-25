// Host-side check for the signed redirect ceiling (make test): Ed25519, the envelope rules,
// rollback protection, per-name filtering of the profile, and the change notice's digest.
// The envelopes below are signed with a throwaway test key (seed 00 01 .. 1f), never the
// real ceiling key.
#include "ceiling.h"
#include "ed25519.h"
#include "hosts.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void unhex(const char *h, uint8_t *out) {
    for (size_t i = 0; h[2 * i]; i++) sscanf(h + 2 * i, "%2hhx", &out[i]);
}

static const uint8_t test_pub[32] = {0x03,0xa1,0x07,0xbf,0xf3,0xce,0x10,0xbe,0x1d,0x70,0xdd,0x18,0xe7,0x4b,0xc0,0x99,0x67,0xe4,0xd6,0x30,0x9b,0xa5,0x0d,0x5f,0x1d,0xdc,0x86,0x64,0x12,0x55,0x31,0xb8};
static const char test_keyid[] = "56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c";
static const char ENV_V3[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjozLCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiLCIubmludGVuZG8uY29tIiwiLmRlbW9ud2FyZS5uZXQiXSwid2lpdSI6WyIubmludGVuZG8ubmV0IiwiLm5pbnRlbmRvd2lmaS5uZXQiXX19\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c\",\n"
    "      \"sig\": \"k1yCg4mMY40UFaQU0owhUSNolnZL5A8PqFBcoDKZrqaZGH925bvvAFfPbUkpNJ7s1K5aY/+OixxbI4SBVIqrAQ==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;
static const char ENV_V2[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjoyLCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiXX19\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c\",\n"
    "      \"sig\": \"h2FQC0xWi5kmHpUMCCOZoJHAzn3GxFjGrHQU8Le8GqviwTfFUPA5tuzs9E2Zv7HN/xcmUPAPYyqh4Yt1gUSnAA==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;
static const char ENV_V4_BAD_FAMILY[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjo0LCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiXSwid2lpdSI6WyIuY29tIl19fQ==\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c\",\n"
    "      \"sig\": \"VwJk1lSjMtjuzTg5Jn0rsBOJZC5Egj4j+YL6TrCFEmGI0Ht+PSW23b64Qwm4J5NJIOZPxgmeDsldUKa5r0pYAQ==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;
static const char ENV_V5_WIIU_ONLY[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjo1LCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiLCIubmludGVuZG8uY29tIiwiLmRlbW9ud2FyZS5uZXQiXSwid2lpdSI6WyIubmludGVuZG8ubmV0IiwiLm9wZW5wYWsub3JnIl19LCJleHRyYSI6Imlnbm9yZWQifQ==\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c\",\n"
    "      \"sig\": \"IdgyXnH/v4qgtEvdwCNaRkMJJvGbMV6UjwaGHVJ0VJCEoJRfi9p3TnwPArGj6Uz7T3eXy9N1Q6WnsCJp8wqLBQ==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;
static const char ENV_V3_TAMPERED[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjozLCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiLCIubmludGVuZG8uY29tIiwiLmV2aWx3YXJlLm5ldCJdLCJ3aWl1IjpbIi5uaW50ZW5kby5uZXQiLCIubmludGVuZG93aWZpLm5ldCJdfX0=\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c\",\n"
    "      \"sig\": \"k1yCg4mMY40UFaQU0owhUSNolnZL5A8PqFBcoDKZrqaZGH925bvvAFfPbUkpNJ7s1K5aY/+OixxbI4SBVIqrAQ==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;
static const char ENV_V3_OTHER_KEY[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjozLCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiLCIubmludGVuZG8uY29tIiwiLmRlbW9ud2FyZS5uZXQiXSwid2lpdSI6WyIubmludGVuZG8ubmV0IiwiLm5pbnRlbmRvd2lmaS5uZXQiXX19\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c\",\n"
    "      \"sig\": \"0hOfJWNIsulbJ5YDQJRh9wimt+TioAyj1InZUJ1MuS/o4tglu/gDPiYLBVaXFODGdanhEt73YTyKWtVI183TDw==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;
static const char ENV_V3_WRONG_KEYID[] =
    "{\n"
    "  \"payload\": \"eyJ0eXBlIjoib3BlbnBhay1jZWlsaW5nIiwiaXNzdWVkIjoiMjAyNi0wOS0yNFQxMDozMDowMFoiLCJ2ZXJzaW9uIjozLCJwbGF0Zm9ybXMiOnsic3dpdGNoIjpbIi5uaW50ZW5kby5uZXQiLCIubmludGVuZG8uY29tIiwiLmRlbW9ud2FyZS5uZXQiXSwid2lpdSI6WyIubmludGVuZG8ubmV0IiwiLm5pbnRlbmRvd2lmaS5uZXQiXX19\",\n"
    "  \"signatures\": [\n"
    "    {\n"
    "      \"keyid\": \"0000000000000000000000000000000000000000000000000000000000000000\",\n"
    "      \"sig\": \"k1yCg4mMY40UFaQU0owhUSNolnZL5A8PqFBcoDKZrqaZGH925bvvAFfPbUkpNJ7s1K5aY/+OixxbI4SBVIqrAQ==\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
;

static const char PROFILE[] =
    "{\"version\":1,\"generated_at\":1790240666,\"platform\":\"switch\","
    "\"server\":{\"address\":\"198.51.100.7\",\"https_port\":443,\"http_port\":80},"
    "\"redirect\":{\"suffixes\":[\".nintendo.net\",\".newfamily.org\",\".ea.com\",\".com\"],"
    "\"exact\":[\"x.demonware.net\",\"evil.example.com\",\"bad name.nintendo.net\"],\"never\":[],"
    "\"overrides\":{\"nncs2-lp1.n.n.srv.nintendo.net\":\"203.0.113.9\",\"nat.example.com\":\"1.1.1.1\","
    "\"inject.nintendo.net\":\"1.2.3.4\\n5.6.7.8 x.com\"}},\"services\":[],\"recheck_after\":21600}";

static char root[] = "/tmp/openpak_ceiling_test_XXXXXX";

static void put(const char *rel, const char *text) {
    char path[320];
    snprintf(path, sizeof(path), "%s%s", root, rel);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static char *get(const char *rel) {
    char path[320];
    snprintf(path, sizeof(path), "%s%s", root, rel);
    size_t len;
    return openpak_read_file(path, 1 << 20, &len);
}

int main(void) {
    // RFC 8032 section 7.1, tests 1 and 2.
    {
        uint8_t pub[32], sig[64], msg = 0x72;
        unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pub);
        unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", sig);
        assert(openpak_ed25519_verify(sig, NULL, 0, pub));
        sig[10] ^= 1;
        assert(!openpak_ed25519_verify(sig, NULL, 0, pub));
        unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", pub);
        unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00", sig);
        assert(openpak_ed25519_verify(sig, &msg, 1, pub));
        msg = 0x73;
        assert(!openpak_ed25519_verify(sig, &msg, 1, pub));
        // S >= L is rejected even where the curve equation would hold.
        msg = 0x72;
        sig[63] |= 0xf0;
        assert(!openpak_ed25519_verify(sig, &msg, 1, pub));
    }

    // The contract's family rule.
    assert(openpak_family_valid(".ea.com"));
    assert(openpak_family_valid(".nintendo.net"));
    assert(!openpak_family_valid(".com"));
    assert(!openpak_family_valid("ea.com"));
    assert(!openpak_family_valid(".EA.com"));
    assert(!openpak_family_valid(".ea..com"));
    assert(!openpak_family_valid(".ea.com."));
    assert(!openpak_family_valid(".-ea.com"));

    openpak_pinned_key test_key = {test_keyid, {0}};
    memcpy(test_key.pub, test_pub, 32);
    openpak_ceiling c;
    char err[256];

    // The real pinned key does not vouch for the test envelopes.
    assert(!openpak_ceiling_verify(ENV_V3, strlen(ENV_V3), &c, err, sizeof(err)));
    openpak_ceiling_keys = &test_key;
    openpak_ceiling_key_count = 1;

    assert(openpak_ceiling_verify(ENV_V3, strlen(ENV_V3), &c, err, sizeof(err)));
    assert(c.version == 3 && c.count == 3);          // only this platform's families
    assert(openpak_ceiling_covers(&c, "ea.com") == false);
    assert(openpak_ceiling_covers(&c, "nintendo.net"));             // the apex
    assert(openpak_ceiling_covers(&c, "a.b.nintendo.net"));
    assert(openpak_ceiling_covers(&c, ".x.demonware.net"));         // a narrower family
    assert(!openpak_ceiling_covers(&c, "xnintendo.net"));
    assert(!openpak_ceiling_covers(&c, "nintendowifi.net"));        // wiiu's, not ours

    assert(!openpak_ceiling_verify(ENV_V3_TAMPERED, strlen(ENV_V3_TAMPERED), &c, err, sizeof(err)));
    assert(!openpak_ceiling_verify(ENV_V3_OTHER_KEY, strlen(ENV_V3_OTHER_KEY), &c, err, sizeof(err)));
    assert(!openpak_ceiling_verify(ENV_V3_WRONG_KEYID, strlen(ENV_V3_WRONG_KEYID), &c, err, sizeof(err)));
    // A malformed family on another platform rejects the whole file.
    assert(!openpak_ceiling_verify(ENV_V4_BAD_FAMILY, strlen(ENV_V4_BAD_FAMILY), &c, err, sizeof(err)));
    assert(strstr(err, "malformed"));

    assert(mkdtemp(root));
    openpak_root = root;
    char dir[320];
    snprintf(dir, sizeof(dir), "%s/atmosphere", root);       mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/atmosphere/hosts", root); mkdir(dir, 0755);

    // First boot: nothing cached, the compiled list's families are the ceiling.
    {
        assert(!openpak_ceiling_load_cached(&c, err, sizeof(err)) && err[0] == '\0');
        assert(openpak_ceiling_current()->version == 0);
        assert(openpak_ceiling_current()->count >= 14);
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);
    }

    // Accept, cache the bytes exactly, refuse a rollback.
    {
        assert(openpak_ceiling_accept(ENV_V3, strlen(ENV_V3), &c, err, sizeof(err)));
        char *cached = get(OPENPAK_CEILING_CACHE);
        assert(cached && strcmp(cached, ENV_V3) == 0);
        free(cached);
        char *ver = get(OPENPAK_CEILING_VERSION);
        assert(ver && atoi(ver) == 3);
        free(ver);

        assert(!openpak_ceiling_accept(ENV_V2, strlen(ENV_V2), &c, err, sizeof(err)));
        assert(strstr(err, "older"));
        assert(!openpak_ceiling_accept(ENV_V3_TAMPERED, strlen(ENV_V3_TAMPERED), &c, err, sizeof(err)));
        cached = get(OPENPAK_CEILING_CACHE);
        assert(cached && strcmp(cached, ENV_V3) == 0);   // the verified one stays
        free(cached);
        assert(openpak_ceiling_accept(ENV_V3, strlen(ENV_V3), &c, err, sizeof(err)));   // same version is fine

        assert(openpak_ceiling_load_cached(&c, err, sizeof(err)) && c.version == 3);
        // A validly signed older envelope swapped onto the card is not loaded either.
        put(OPENPAK_CEILING_CACHE, ENV_V2);
        assert(!openpak_ceiling_load_cached(&c, err, sizeof(err)) && strstr(err, "older"));
        put(OPENPAK_CEILING_CACHE, ENV_V3);
    }

    // The profile is filtered name by name, never discarded whole.
    {
        assert(openpak_ceiling_load_cached(&c, err, sizeof(err)));
        char dropped[256];
        openpak_policy *p = openpak_profile_policy(PROFILE, strlen(PROFILE), &c, OPENPAK_SOURCE_PROFILE,
                                                   dropped, sizeof(dropped), err, sizeof(err));
        assert(p);
        assert(p->count == 4);          // *.nintendo.net + apex, x.demonware.net, the NAT override
        assert(strcmp(p->rules[0].host, "*.nintendo.net") == 0 && p->rules[0].address[0] == '\0');
        assert(strcmp(p->rules[1].host, "nintendo.net") == 0);
        assert(strcmp(p->rules[2].host, "x.demonware.net") == 0);
        assert(strcmp(p->rules[3].host, "nncs2-lp1.n.n.srv.nintendo.net") == 0);
        assert(strcmp(p->rules[3].address, "203.0.113.9") == 0);
        assert(strcmp(p->address, "198.51.100.7") == 0);
        assert(strstr(dropped, ".newfamily.org") && strstr(dropped, ".ea.com") && strstr(dropped, ".com"));
        assert(strstr(dropped, "evil.example.com") && strstr(dropped, "nat.example.com"));
        assert(strstr(dropped, "bad name") && strstr(dropped, "inject.nintendo.net"));
        openpak_policy_free(p);

        const char *wiiu = "{\"version\":1,\"platform\":\"wiiu\",\"redirect\":{\"suffixes\":[\".nintendo.net\"]}}";
        assert(!openpak_profile_policy(wiiu, strlen(wiiu), &c, OPENPAK_SOURCE_PROFILE, dropped, sizeof(dropped), err, sizeof(err)));
        const char *v2 = "{\"version\":2,\"platform\":\"switch\",\"redirect\":{\"suffixes\":[\".nintendo.net\"]}}";
        assert(!openpak_profile_policy(v2, strlen(v2), &c, OPENPAK_SOURCE_PROFILE, dropped, sizeof(dropped), err, sizeof(err)));
    }

    // The saved profile wins over the bundles and the frozen list, and is what gets installed.
    char file[320];
    snprintf(file, sizeof(file), "%s/atmosphere/hosts/default.txt", root);
    {
        put(OPENPAK_PROFILE_CACHE, PROFILE);
        openpak_policy_reload();
        const openpak_policy *pol = openpak_active_policy();
        assert(pol->source == OPENPAK_SOURCE_PROFILE_SAVED);
        assert(openpak_ceiling_current()->version == 3);
        assert(strstr(openpak_policy_dropped(), ".newfamily.org"));
        openpak_policy_set_fresh(true);
        openpak_policy_reload();
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_PROFILE);

        char installed[65], pending[65], before[65];
        assert(!openpak_installed_digest(installed));          // nothing installed yet
        assert(openpak_enable("198.51.100.7", err, sizeof(err)));
        char *hosts = get("/atmosphere/hosts/default.txt");
        // ".nintendo.net" is written as its services, never as "*.nintendo.net", and the NAT-check
        // override is dropped: dns_mitm must not answer Pia's nncs lookups (hosts.c).
        assert(!strstr(hosts, "*.nintendo.net\n"));
        assert(strstr(hosts, "198.51.100.7 *.s.n.srv.nintendo.net\n") && strstr(hosts, "198.51.100.7 nintendo.net\n"));
        assert(!strstr(hosts, "nncs2-lp1"));
        assert(!strstr(hosts, "newfamily") && !strstr(hosts, "example.com") && !strstr(hosts, "5.6.7.8"));
        free(hosts);

        assert(openpak_installed_digest(installed));
        openpak_pending_digest("198.51.100.7", pending);
        assert(strcmp(installed, pending) == 0);                // nothing to tell the user
        memcpy(before, installed, sizeof(before));

        // An install from before the digest was recorded is read back from the hosts block.
        char rec[320];
        snprintf(rec, sizeof(rec), "%s%s", root, OPENPAK_DIGEST_RECORD);
        remove(rec);
        assert(openpak_installed_digest(installed) && strcmp(installed, before) == 0);
        assert(openpak_enable("198.51.100.7", err, sizeof(err)));

        // A new ceiling that only changes another platform's list changes nothing here.
        assert(openpak_ceiling_accept(ENV_V5_WIIU_ONLY, strlen(ENV_V5_WIIU_ONLY), &c, err, sizeof(err)));
        openpak_policy_reload();
        assert(openpak_ceiling_current()->version == 5);
        openpak_pending_digest("198.51.100.7", pending);
        assert(strcmp(pending, before) == 0);

        // A server-side family added inside the ceiling: the console is told, not changed.
        put(OPENPAK_PROFILE_CACHE,
            "{\"version\":1,\"platform\":\"switch\",\"redirect\":{\"suffixes\":[\".nintendo.net\",\".nintendo.com\"],"
            "\"exact\":[\"x.demonware.net\"],\"overrides\":{\"nncs2-lp1.n.n.srv.nintendo.net\":\"203.0.113.9\"}}}");
        openpak_policy_reload();
        openpak_pending_digest("198.51.100.7", pending);
        assert(openpak_installed_digest(installed) && strcmp(installed, pending) != 0);
        hosts = get("/atmosphere/hosts/default.txt");
        assert(!strstr(hosts, "nintendo.com"));                 // still what was installed
        free(hosts);
        // A different server address is a different effective set too.
        openpak_pending_digest("198.51.100.8", pending);
        assert(strcmp(pending, before) != 0);

        // Re-applying installs it and the notice goes away.
        assert(openpak_enable("198.51.100.7", err, sizeof(err)));
        openpak_pending_digest("198.51.100.7", pending);
        assert(openpak_installed_digest(installed) && strcmp(installed, pending) == 0);
        hosts = get("/atmosphere/hosts/default.txt");
        assert(strstr(hosts, "198.51.100.7 *.nintendo.com"));
        free(hosts);

        // Nintendo removes everything, the record included: no notice on a console not on OpenPak.
        assert(openpak_disable(err, sizeof(err)));
        assert(!openpak_installed_digest(installed));
        assert(get(OPENPAK_DIGEST_RECORD) == NULL);
    }

    // A saved profile with nothing inside the ceiling falls through to the next source.
    {
        put(OPENPAK_PROFILE_CACHE, "{\"version\":1,\"platform\":\"switch\",\"redirect\":{\"suffixes\":[\".elsewhere.org\"]}}");
        openpak_policy_reload();
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);
        assert(strstr(openpak_policy_problem(), "saved profile ignored"));
    }
    (void)file;
    printf("signed ceiling: all checks passed\n");
    return 0;
}
