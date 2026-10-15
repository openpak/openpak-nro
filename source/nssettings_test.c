// Host-side check for the system_settings.ini edit (make test): the pure text step and the
// file step with a temp SD root. The temp root lives under $TMPDIR (never the /tmp RAM disk
// on the workspace).
#include "hosts.h"
#include "nssettings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAMPLE_ORIG "scratch/ns-npns-20261015/system_settings.ini.orig"
#define SAMPLE_NEW  "scratch/ns-npns-20261015/system_settings.ini.new2"

static char *xform(const char *in, size_t *outlen) {
    size_t n = in ? strlen(in) : 0, cap = n + NSSETTINGS_GROWTH;
    char *out = malloc(cap + 1);
    *outlen = openpak_nssettings_transform(in, n, out, cap);
    assert(*outlen != (size_t)-1);
    out[*outlen] = '\0';
    return out;
}

static void expect(const char *in, const char *want) {
    size_t n;
    char *got = xform(in, &n);
    if (strcmp(got, want) != 0) fprintf(stderr, "--- got\n%s\n--- want\n%s\n", got, want);
    assert(strcmp(got, want) == 0);
    // Idempotent: a second pass changes nothing.
    size_t n2;
    char *again = xform(got, &n2);
    assert(n2 == n && memcmp(again, got, n) == 0);
    free(again);
    free(got);
}

static char *slurp(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    *len = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[*len] = '\0';
    return b;
}

static void put(const char *p, const char *s) {
    FILE *f = fopen(p, "wb");
    assert(f);
    fputs(s, f);
    fclose(f);
}

int main(void) {
    // key present with 0 -> 1, neighbours and the other off switches stay
    expect("[ns.notification]\nenable_download_task_list = u8!0x0\nenable_version_list = u8!0x0\nretry = u32!0x1\n",
           "[ns.notification]\nenable_download_task_list = u8!0x0\nenable_version_list = u8!0x1\n"
           "debug_version_list_waiting_limit = u32!0x0\nretry = u32!0x1\n");
    // both present and wrong: replaced in place, nothing added
    expect("[ns.notification]\ndebug_version_list_waiting_limit = u32!0x5\nenable_version_list=u8!0x0\n",
           "[ns.notification]\ndebug_version_list_waiting_limit = u32!0x0\nenable_version_list=u8!0x1\n");
    // keys missing: appended after the section's last key line, before the next section
    expect("[a]\nx = 1\n\n[ns.notification]\nenable_download_task_list = u8!0x0\n; note\n\n[b]\ny = 2\n",
           "[a]\nx = 1\n\n[ns.notification]\nenable_download_task_list = u8!0x0\n"
           "enable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n; note\n\n[b]\ny = 2\n");
    // empty section: after the header
    expect("[ns.notification]\n[b]\n",
           "[ns.notification]\nenable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n[b]\n");
    // section missing: created at the end, blank line first
    expect("[a]\nx = 1\n",
           "[a]\nx = 1\n\n[ns.notification]\nenable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n");
    // no trailing newline
    expect("[a]\nx = 1",
           "[a]\nx = 1\n\n[ns.notification]\nenable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n");
    expect("[ns.notification]\nenable_version_list = u8!0x0",
           "[ns.notification]\nenable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n");
    // file missing
    expect(NULL, "[ns.notification]\nenable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n");
    // CRLF file keeps CRLF, in replacement and in additions
    expect("[a]\r\nx = 1\r\n[ns.notification]\r\nenable_version_list = u8!0x0\r\n",
           "[a]\r\nx = 1\r\n[ns.notification]\r\nenable_version_list = u8!0x1\r\n"
           "debug_version_list_waiting_limit = u32!0x0\r\n");
    expect("[a]\r\nx = 1\r\n",
           "[a]\r\nx = 1\r\n\r\n[ns.notification]\r\nenable_version_list = u8!0x1\r\n"
           "debug_version_list_waiting_limit = u32!0x0\r\n");
    // already correct: byte-identical, with comments, odd spacing and a commented-out key
    {
        const char *ok = "# top\r\n[x]\r\n; enable_version_list = u8!0x0\r\n[ns.notification]\r\n"
                         "  enable_version_list\t=  u8!0x1 \r\ndebug_version_list_waiting_limit = u32!0x0\r\n";
        size_t n;
        char *got = xform(ok, &n);
        assert(n == strlen(ok) && memcmp(got, ok, n) == 0);
        free(got);
    }
    // same key names in another section are not ours
    expect("[other]\nenable_version_list = u8!0x0\n[ns.notification]\nenable_version_list = u8!0x0\n",
           "[other]\nenable_version_list = u8!0x0\n[ns.notification]\nenable_version_list = u8!0x1\n"
           "debug_version_list_waiting_limit = u32!0x0\n");
    // too small a buffer reports failure instead of overflowing
    {
        char tiny[8];
        assert(openpak_nssettings_transform("[a]\n", 4, tiny, sizeof(tiny)) == (size_t)-1);
    }

    // the real file from the console
    {
        const char *home = getenv("HOME");
        char op[512], np[512];
        snprintf(op, sizeof(op), "%s/REPOS/Openpak/" SAMPLE_ORIG, home);
        snprintf(np, sizeof(np), "%s/REPOS/Openpak/" SAMPLE_NEW, home);
        size_t ol, nl;
        char *orig = slurp(op, &ol), *want = slurp(np, &nl);
        if (orig && want) {
            char *out = malloc(ol + NSSETTINGS_GROWTH);
            size_t n = openpak_nssettings_transform(orig, ol, out, ol + NSSETTINGS_GROWTH);
            assert(n == nl && memcmp(out, want, nl) == 0);
            free(out);
        } else {
            fprintf(stderr, "nssettings_test: real-world sample not present, skipped\n");
        }
        free(orig);
        free(want);
    }

    // the file step, on a temp SD root
    {
        const char *t = getenv("TMPDIR");
        char root[300], path[400], buf[500], err[160] = "";
        snprintf(root, sizeof(root), "%s/openpak_ns_XXXXXX", t ? t : ".");
        assert(mkdtemp(root));
        openpak_root = root;
        snprintf(path, sizeof(path), "%s/atmosphere/config/system_settings.ini", root);
        size_t n;

        // missing file: created, no backup to take
        assert(openpak_nssettings_ensure(err, sizeof(err)) == 1);
        char *got = slurp(path, &n);
        assert(got && strstr(got, "enable_version_list = u8!0x1"));
        free(got);
        assert(openpak_nssettings_ensure(err, sizeof(err)) == 0);

        // existing file, first change: original backed up once; later changes do not add backups
        put(path, "[ns.notification]\nenable_version_list = u8!0x0\n");
        assert(openpak_nssettings_ensure(err, sizeof(err)) == 1);
        snprintf(buf, sizeof(buf), "%s/switch/openpak/nssettings-backup-done", root);
        assert(slurp(buf, &n));
        snprintf(buf, sizeof(buf), "%s/switch/openpak/nssettings-backup", root);
        char cmd[900];
        snprintf(cmd, sizeof(cmd), "grep -q 'enable_version_list = u8!0x0' %s/*.ini 2>/dev/null", buf);
        // marker already existed from the created file, so no backup this time
        assert(system(cmd) != 0);
        got = slurp(path, &n);
        assert(strcmp(got, "[ns.notification]\nenable_version_list = u8!0x1\ndebug_version_list_waiting_limit = u32!0x0\n") == 0);
        free(got);
        snprintf(buf, sizeof(buf), "%s/switch/openpak/nssettings.log", root);
        assert(slurp(buf, &n));

        // fresh root with an existing file: the backup holds the original bytes
        char root2[300];
        snprintf(root2, sizeof(root2), "%s/openpak_ns_XXXXXX", t ? t : ".");
        assert(mkdtemp(root2));
        openpak_root = root2;
        snprintf(buf, sizeof(buf), "%s/atmosphere", root2); mkdir(buf, 0755);
        snprintf(buf, sizeof(buf), "%s/atmosphere/config", root2); mkdir(buf, 0755);
        snprintf(path, sizeof(path), "%s/atmosphere/config/system_settings.ini", root2);
        put(path, "[ns.notification]\r\nenable_version_list = u8!0x0\r\n");
        assert(openpak_nssettings_ensure(err, sizeof(err)) == 1);
        snprintf(cmd, sizeof(cmd), "grep -q 'enable_version_list = u8!0x0' %s/switch/openpak/nssettings-backup/*.ini", root2);
        assert(system(cmd) == 0);
        snprintf(cmd, sizeof(cmd), "rm -rf %s %s", root, root2);
        assert(system(cmd) == 0);
    }
    puts("nssettings_test: ok");
    return 0;
}
