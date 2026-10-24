#include "ca.h"
#include "hosts.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Atmosphère patch sets shipped with this NRO and installed alongside the host rules:
//
//   nro_patches/disable_browser_ca_verification  — the web applet, so the link page loads
//   exefs_patches/disable_ca_verification        — the ssl sysmodule, for system services
//
//   exefs_patches/openpak_bcat_key               — BCAT verifies News containers with OpenPak's key
//   exefs_patches/openpak_news_no_dauth          — News memory download skips the Nintendo edge token
//   exefs_patches/openpak_news_list_no_dauth     — the News list and titles/topics fetches skip it too
//   exefs_patches/openpak_ctr_key                — Crash Team Racing trusts OpenPak's Demonware key
//   exefs_patches/openpak_d2r_trust              — Diablo II: Resurrected trusts OpenPak's CA on its game server
//
// The first two are community patches (upstream exefs_patches repositories); the News set
// comes from news/tools/news-patch. The key and memory-download patches were proven on
// hardware by the News probe (v0.19); the list patch skips the same edge-token helper before
// the /v1/list/ and titles/<tid>/topics requests, which otherwise abort against OpenPak.
// The two game patches are the documented exceptions to "no game patches", both data-only:
// the CTR key (servers/demonware tools/ctr-key-patch) swaps the key CTR checks its login
// replies against, and the D2R trust (platforms/switch/games/battlenet tools/d2r-trust)
// replaces the CA bundle the title's own OpenSSL trusts for its game server. Both checks live in
// the game's own code, so no system-level fix reaches them.
// All are keyed by build id: a console uses whichever file matches the build it is running,
// so the whole set ships and the console picks. Without the browser one, no certificate we
// install is ever consulted; without the News pair, News stays silent on OpenPak.
static const char *const patch_sets[][2] = {
    {"romfs:/patches/nro_patches/disable_browser_ca_verification",
     "/atmosphere/nro_patches/disable_browser_ca_verification"},
    {"romfs:/patches/exefs_patches/disable_ca_verification",
     "/atmosphere/exefs_patches/disable_ca_verification"},
    {"romfs:/patches/exefs_patches/openpak_bcat_key",
     "/atmosphere/exefs_patches/openpak_bcat_key"},
    {"romfs:/patches/exefs_patches/openpak_news_no_dauth",
     "/atmosphere/exefs_patches/openpak_news_no_dauth"},
    {"romfs:/patches/exefs_patches/openpak_news_list_no_dauth",
     "/atmosphere/exefs_patches/openpak_news_list_no_dauth"},
    {"romfs:/patches/exefs_patches/openpak_ctr_key",
     "/atmosphere/exefs_patches/openpak_ctr_key"},
    {"romfs:/patches/exefs_patches/openpak_d2r_trust",
     "/atmosphere/exefs_patches/openpak_d2r_trust"},
};
static const int patch_set_count = (int)(sizeof(patch_sets) / sizeof(patch_sets[0]));

// Files an earlier hand install left under another directory name. Atmosphère applies every
// matching IPS it finds, so a stale copy would keep patching with an old CA after an update.
// Both install and remove delete them, by name; the directory goes once it is empty.
static const char *const superseded_dir = "/atmosphere/exefs_patches/openpak-d2r";
static const char *const superseded_files[] = {
    "4DE22400C997ED2373F0643E9FE9B079DB0E8D72.ips",
    "DB0507F36B84200AC05F75D76ED5026F903E6C9.ips",
};

static int remove_superseded(void) {
    int removed = 0;
    char path[512];
    for (size_t i = 0; i < sizeof(superseded_files) / sizeof(superseded_files[0]); i++) {
        snprintf(path, sizeof(path), "%s%s/%s", openpak_root, superseded_dir, superseded_files[i]);
        if (remove(path) == 0) removed++;
    }
    snprintf(path, sizeof(path), "%s%s", openpak_root, superseded_dir);
    rmdir(path); // only succeeds when nothing else is in it
    return removed;
}

static void make_dirs(const char *full) {
    char buf[320];
    snprintf(buf, sizeof(buf), "%s", full);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(buf, 0777);
        *p = '/';
    }
    mkdir(buf, 0777);
}

static bool copy_one(const char *from, const char *to) {
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

int openpak_patches_install(void) {
    remove_superseded();
    int written = 0;
    for (int i = 0; i < patch_set_count; i++) {
        char dest_dir[320];
        snprintf(dest_dir, sizeof(dest_dir), "%s%s", openpak_root, patch_sets[i][1]);
        make_dirs(dest_dir);

        DIR *d = opendir(patch_sets[i][0]);
        if (!d) continue;
        for (struct dirent *e; (e = readdir(d)) != NULL;) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".ips") != 0) continue;
            char from[512], to[512];
            snprintf(from, sizeof(from), "%s/%s", patch_sets[i][0], e->d_name);
            snprintf(to, sizeof(to), "%s/%s", dest_dir, e->d_name);
            if (copy_one(from, to)) written++;
        }
        closedir(d);
    }
    return written;
}

// Removes only the files this tool installed, by name, so a patch the user put there by hand
// or one belonging to another tool is left alone.
int openpak_patches_remove(void) {
    int removed = remove_superseded();
    for (int i = 0; i < patch_set_count; i++) {
        DIR *d = opendir(patch_sets[i][0]);
        if (!d) continue;
        for (struct dirent *e; (e = readdir(d)) != NULL;) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".ips") != 0) continue;
            char to[512];
            snprintf(to, sizeof(to), "%s%s/%s", openpak_root, patch_sets[i][1], e->d_name);
            if (remove(to) == 0) removed++;
        }
        closedir(d);
    }
    return removed;
}
