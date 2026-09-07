#include "ca.h"
#include "hosts.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// The web applet's title, and the two bundles it reads: firmware picks one or the other
// depending on version, so both are written. romfs_metadata.bin caches the layout of that
// romfs — leaving a stale one behind means the console never sees the new file.
#define BROWSER_DIR   "/atmosphere/contents/0100000000000803"
#define METADATA_PATH BROWSER_DIR "/romfs_metadata.bin"

// The current browser reads its roots from romfs/browser and romfs/0/browser (firmware 22 uses
// the numbered one); openssl_peer/cacerts.pem is the older layout and is written too so the
// same NRO works on an older console. Both bundle names are replaced: RootCaEtc is the general
// store and RootCaSdkAdditional the one the SDK consults.
static const char *const bundles[] = {
    BROWSER_DIR "/romfs/0/browser/RootCaEtc.pem",
    BROWSER_DIR "/romfs/0/browser/RootCaSdkAdditional.pem",
    BROWSER_DIR "/romfs/browser/RootCaEtc.pem",
    BROWSER_DIR "/romfs/browser/RootCaSdkAdditional.pem",
    BROWSER_DIR "/romfs/openssl_peer/cacerts.pem",
    BROWSER_DIR "/romfs/nro/netfront/openssl_peer/cacerts.pem",
};
static const int bundle_count = (int)(sizeof(bundles) / sizeof(bundles[0]));

// romfs:/ca.pem, shipped inside the NRO: the console cannot fetch it from the server before it
// trusts the server.
#ifdef OPENPAK_HOST_TEST
#define CA_SOURCE "romfs/ca.pem"      // running on a PC, straight from the repo
#else
#define CA_SOURCE "romfs:/ca.pem"
#endif

static void path_under_root(char *out, int outlen, const char *path) {
    snprintf(out, outlen, "%s%s", openpak_root, path);
}

// mkdir -p for the directories above a bundle file.
static void make_parents(const char *full) {
    char buf[320];
    snprintf(buf, sizeof(buf), "%s", full);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(buf, 0777);
        *p = '/';
    }
}

static bool copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    fclose(out);
    if (!ok) remove(to);
    return ok;
}

int openpak_ca_install(char *err, int errlen) {
    int written = 0;
    for (int i = 0; i < bundle_count; i++) {
        char full[320];
        path_under_root(full, sizeof(full), bundles[i]);
        make_parents(full);
        if (copy_file(CA_SOURCE, full)) written++;
    }
    if (written == 0) {
        snprintf(err, errlen, "could not write the browser CA bundle");
        return 0;
    }
    char meta[320];
    path_under_root(meta, sizeof(meta), METADATA_PATH);
    remove(meta);   // stale layout cache: the browser would not see the new file
    return written;
}

int openpak_ca_remove(char *err, int errlen) {
    (void)err; (void)errlen;
    int removed = 0;
    for (int i = 0; i < bundle_count; i++) {
        char full[320];
        path_under_root(full, sizeof(full), bundles[i]);
        if (remove(full) == 0) removed++;
    }
    char meta[320];
    path_under_root(meta, sizeof(meta), METADATA_PATH);
    remove(meta);
    return removed;
}

bool openpak_ca_installed(void) {
    for (int i = 0; i < bundle_count; i++) {
        char full[320];
        path_under_root(full, sizeof(full), bundles[i]);
        FILE *f = fopen(full, "rb");
        if (f) { fclose(f); return true; }
    }
    return false;
}
