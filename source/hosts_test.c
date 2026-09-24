// Host-side check for the toggle logic — build and run on a PC, no console needed:
//   cc -DOPENPAK_TEST -o /tmp/hosts_test source/hosts.c source/hosts_test.c && /tmp/hosts_test
#include "ca.h"
#include "hosts.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *read_all(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

int main(void) {
    char root[] = "/tmp/openpak_test_XXXXXX";
    assert(mkdtemp(root));
    char dir[256], file[256], err[128];
    snprintf(dir, sizeof(dir), "%s/atmosphere", root);   mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/atmosphere/hosts", root); mkdir(dir, 0755);
    snprintf(file, sizeof(file), "%s/atmosphere/hosts/default.txt", root);
    openpak_root = root;

    // A hosts file the user already had must survive enable and disable.
    FILE *f = fopen(file, "wb");
    fputs("192.168.1.50 example.invalid\n", f);
    fclose(f);

    assert(!openpak_enabled());
    assert(openpak_enable("10.0.0.7", err, sizeof(err)));
    assert(openpak_enabled());

    char *after = read_all(file);
    assert(strstr(after, "10.0.0.7 *.nintendo.net"));
    assert(strstr(after, "192.168.1.50 example.invalid"));     // pre-existing line kept
    assert(strstr(after, "10.0.0.7 *.nintendo.com"));          // our entries written
    assert(strstr(after, "10.0.0.7 nintendo.net"));            // the apex too
    assert(strstr(after, OPENPAK_BEGIN) && strstr(after, OPENPAK_END));

    // Enabling twice must replace the block, not stack two copies.
    assert(openpak_enable("10.0.0.8", err, sizeof(err)));
    after = read_all(file);
    const char *first = strstr(after, OPENPAK_BEGIN);
    assert(first && !strstr(first + 1, OPENPAK_BEGIN));
    assert(strstr(after, "10.0.0.8 *.nintendo.com") && !strstr(after, "10.0.0.7 "));

    assert(openpak_disable(err, sizeof(err)));
    assert(!openpak_enabled());
    after = read_all(file);
    assert(strstr(after, "192.168.1.50 example.invalid"));     // console back to how it was
    assert(!strstr(after, "nintendo.com"));

    // Someone else's redirect for the same names must not survive either choice: otherwise
    // "Nintendo" would still send the console to whatever that other tool points at.
    f = fopen(file, "wb");
    fputs("192.168.1.50 example.invalid\n"
          "10.9.9.9 accounts.nintendo.com\n"
          "10.9.9.9 fro-3.hac.lp1.penne.srv.nintendo.net\n", f);
    fclose(f);

    assert(openpak_enable("10.0.0.7", err, sizeof(err)));
    after = read_all(file);
    assert(strstr(after, OPENPAK_DISABLED "10.9.9.9 accounts.nintendo.com"));   // exact name
    assert(strstr(after, OPENPAK_DISABLED "10.9.9.9 fro-3.hac"));               // wildcard match
    assert(strstr(after, "192.168.1.50 example.invalid"));                      // unrelated line kept
    assert(strstr(after, "10.0.0.7 *.nintendo.com"));

    // Going back to Nintendo reverts everything we did: our block gone, their lines live again.
    assert(openpak_disable(err, sizeof(err)));
    after = read_all(file);
    assert(!strstr(after, OPENPAK_DISABLED));                                   // no trace of us
    assert(!strstr(after, OPENPAK_BEGIN));
    assert(strstr(after, "10.9.9.9 accounts.nintendo.com"));                    // restored verbatim
    assert(strstr(after, "10.9.9.9 fro-3.hac.lp1.penne.srv.nintendo.net"));
    assert(strstr(after, "192.168.1.50 example.invalid"));

    f = fopen(file, "wb");
    fputs("10.9.9.9 lavender-switch-auth3.prod.demonware.net\n", f);
    fclose(f);

    assert(openpak_enable("10.0.0.7", err, sizeof(err)));
    after = read_all(file);
    // Third parties are claimed families now: the console reaches OpenPak for them too,
    // and someone else's redirect for one of their names is set aside like any other.
    assert(strstr(after, "10.0.0.7 *.demonware.net"));
    assert(strstr(after, "10.0.0.7 *.epicgames.dev"));
    assert(strstr(after, OPENPAK_DISABLED "10.9.9.9 lavender-switch-auth3.prod.demonware.net"));

    assert(openpak_disable(err, sizeof(err)));
    after = read_all(file);
    assert(strstr(after, "10.9.9.9 lavender-switch-auth3"));    // restored verbatim on Nintendo
    assert(!strstr(after, OPENPAK_DISABLED));

    // A bundle on the SD card replaces the compiled list outright. This is the whole
    // point: adding a forwarder becomes a server deploy, not a new NRO on every SD card.
    {
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);

        snprintf(dir, sizeof(dir), "%s/switch", root);         mkdir(dir, 0755);
        snprintf(dir, sizeof(dir), "%s/switch/openpak", root); mkdir(dir, 0755);
        char bundle[320];
        snprintf(bundle, sizeof(bundle), "%s/switch/openpak/policy.json", root);

        f = fopen(bundle, "wb");
        fputs("{\"schema_version\":2,\"platform\":\"switch\",\"sequence\":7,"
              "\"required_capabilities\":[\"dns.suffix.v1\",\"dns.exact.v1\",\"dns.override.v1\"],"
              "\"rules\":["
              "{\"action\":\"redirect\",\"match\":{\"suffix\":\".newgame.net\"}},"
              "{\"action\":\"redirect\",\"match\":{\"exact\":\"one.newgame.net\"}},"
              "{\"action\":\"redirect\",\"match\":{\"suffix\":\".apex.net\",\"include_apex\":true}},"
              "{\"action\":\"redirect\",\"match\":{\"exact\":\"nat2.newgame.net\"},"
              "\"destination\":{\"ipv4\":\"203.0.113.9\"}},"
              "{\"action\":\"passthrough\",\"match\":{\"exact\":\"conntest.elsewhere.net\"}}]}", f);
        fclose(f);
        openpak_policy_reload();

        const openpak_policy *pol = openpak_active_policy();
        assert(pol->source == OPENPAK_SOURCE_CACHE);
        assert(pol->sequence == 7);
        assert(pol->count == 5);            // the apex rule emits two lines, passthrough none

        remove(file);
        assert(openpak_enable("10.0.0.7", err, sizeof(err)));
        after = read_all(file);
        assert(strstr(after, "10.0.0.7 *.newgame.net"));        // a family becomes a wildcard
        assert(strstr(after, "10.0.0.7 one.newgame.net"));
        assert(strstr(after, "10.0.0.7 *.apex.net"));
        assert(strstr(after, "10.0.0.7 apex.net"));             // include_apex names the apex too
        assert(strstr(after, "203.0.113.9 nat2.newgame.net"));  // an override keeps its address
        // ...and comes after the wildcard that also matches it: dns_mitm takes the last match.
        assert(strstr(after, "203.0.113.9 nat2.newgame.net") > strstr(after, "10.0.0.7 *.newgame.net"));
        assert(!strstr(after, "conntest"));                     // passthrough is never written
        assert(!strstr(after, "nintendo"));                     // the compiled list is not in use
        assert(openpak_disable(err, sizeof(err)));

        // A bundle needing something this build cannot do is refused whole and reported,
        // rather than applied in part.
        f = fopen(bundle, "wb");
        fputs("{\"schema_version\":2,\"platform\":\"switch\","
              "\"required_capabilities\":[\"service.url.v1\"],"
              "\"rules\":[{\"action\":\"redirect\",\"match\":{\"exact\":\"x.nintendo.net\"}}]}", f);
        fclose(f);
        openpak_policy_reload();
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);
        assert(strstr(openpak_policy_problem(), "service.url.v1"));

        // So is another console's projection, and a passthrough a redirect would swallow.
        f = fopen(bundle, "wb");
        fputs("{\"schema_version\":2,\"platform\":\"wiiu\",\"rules\":"
              "[{\"action\":\"redirect\",\"match\":{\"exact\":\"x.nintendo.net\"}}]}", f);
        fclose(f);
        openpak_policy_reload();
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);

        f = fopen(bundle, "wb");
        fputs("{\"schema_version\":2,\"platform\":\"switch\",\"rules\":["
              "{\"action\":\"redirect\",\"match\":{\"suffix\":\".nintendo.net\"}},"
              "{\"action\":\"passthrough\",\"match\":{\"exact\":\"conntest.nintendo.net\"}}]}", f);
        fclose(f);
        openpak_policy_reload();
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);
        assert(strstr(openpak_policy_problem(), "conntest.nintendo.net"));

        // A bundle large enough to push the hosts file past what Atmosphere loads is
        // refused before any write: ams_mitm would otherwise abort at boot.
        {
            FILE *h = fopen(file, "wb"); fputs("192.168.1.50 example.invalid\n", h); fclose(h);
            f = fopen(bundle, "wb");
            fputs("{\"schema_version\":2,\"platform\":\"switch\",\"rules\":[", f);
            for (int i = 0; i < 450; i++)
                fprintf(f, "%s{\"action\":\"redirect\",\"match\":{\"exact\":"
                        "\"host-%03d.a-very-long-label-to-fill-the-file.another-long-label.nintendo.net\"}}",
                        i ? "," : "", i);
            fputs("]}", f);
            fclose(f);
            openpak_policy_reload();
            assert(openpak_active_policy()->source == OPENPAK_SOURCE_CACHE);
            assert(!openpak_enable("10.0.0.7", err, sizeof(err)));
            assert(strstr(err, "Atmosphere refuses"));
            after = read_all(file);
            assert(!strstr(after, OPENPAK_BEGIN));                          // nothing written
            assert(strstr(after, "192.168.1.50 example.invalid"));
        }

        // A bundle that is simply absent is the ordinary state, not a problem to report.
        remove(bundle);
        openpak_policy_reload();
        assert(openpak_active_policy()->source == OPENPAK_SOURCE_BUILTIN);
        assert(openpak_policy_problem()[0] == '\0');
    }

    // A console that had no hosts file before gets none back afterwards.
    remove(file);
    remove(file);
    assert(openpak_enable("10.0.0.7", err, sizeof(err)));
    assert(read_all(file) != NULL);
    assert(openpak_disable(err, sizeof(err)));
    assert(read_all(file) == NULL);

    // The browser keeps its own CA bundle; installing and removing it must be as reversible
    // as the host rules, and must clear the stale romfs layout cache both ways.
    {
        char meta[320];
        snprintf(meta, sizeof(meta), "%s/atmosphere/contents/0100000000000803/romfs_metadata.bin", root);
        char dir[320];
        snprintf(dir, sizeof(dir), "%s/atmosphere/contents", root);            mkdir(dir, 0755);
        snprintf(dir, sizeof(dir), "%s/atmosphere/contents/0100000000000803", root); mkdir(dir, 0755);
        FILE *m = fopen(meta, "wb"); fputs("stale", m); fclose(m);

        assert(!openpak_ca_installed());
        assert(openpak_ca_install(err, sizeof(err)) == 6);      // every bundle path the browser may read
        assert(openpak_ca_installed());
        assert(read_all(meta) == NULL);                         // stale cache cleared

        m = fopen(meta, "wb"); fputs("stale", m); fclose(m);
        assert(openpak_ca_remove(err, sizeof(err)) == 6);
        assert(!openpak_ca_installed());
        assert(read_all(meta) == NULL);
    }

    printf("hosts toggle: all checks passed\n");
    return 0;
}
