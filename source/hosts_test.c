// Host-side check for the toggle logic — build and run on a PC, no console needed:
//   cc -DOPENPAK_TEST -o /tmp/hosts_test source/hosts.c source/hosts_test.c && /tmp/hosts_test
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
    assert(strstr(after, "192.168.1.50 example.invalid"));     // pre-existing line kept
    assert(strstr(after, "10.0.0.7 accounts.nintendo.com"));   // our entries written
    assert(strstr(after, OPENPAK_BEGIN) && strstr(after, OPENPAK_END));

    // Enabling twice must replace the block, not stack two copies.
    assert(openpak_enable("10.0.0.8", err, sizeof(err)));
    after = read_all(file);
    const char *first = strstr(after, OPENPAK_BEGIN);
    assert(first && !strstr(first + 1, OPENPAK_BEGIN));
    assert(strstr(after, "10.0.0.8 accounts.nintendo.com") && !strstr(after, "10.0.0.7 "));

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
    assert(strstr(after, "10.0.0.7 accounts.nintendo.com"));

    // Going back to Nintendo reverts everything we did: our block gone, their lines live again.
    assert(openpak_disable(err, sizeof(err)));
    after = read_all(file);
    assert(!strstr(after, OPENPAK_DISABLED));                                   // no trace of us
    assert(!strstr(after, OPENPAK_BEGIN));
    assert(strstr(after, "10.9.9.9 accounts.nintendo.com"));                    // restored verbatim
    assert(strstr(after, "10.9.9.9 fro-3.hac.lp1.penne.srv.nintendo.net"));
    assert(strstr(after, "192.168.1.50 example.invalid"));

    // A console that had no hosts file before gets none back afterwards.
    remove(file);
    assert(openpak_enable("10.0.0.7", err, sizeof(err)));
    assert(read_all(file) != NULL);
    assert(openpak_disable(err, sizeof(err)));
    assert(read_all(file) == NULL);

    printf("hosts toggle: all checks passed\n");
    return 0;
}
