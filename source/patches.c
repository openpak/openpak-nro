#include "ca.h"
#include "hosts.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Atmosphère patch sets shipped with this NRO and installed alongside the host rules:
//
//   nro_patches/disable_browser_ca_verification  — the web applet, so the link page loads
//   exefs_patches/disable_ca_verification        — the ssl sysmodule, for system services
//
// Both are community patches (upstream exefs_patches repositories), keyed by build id: a
// console uses whichever file matches the build it is running, so the whole set ships and the
// console picks. Without the browser one, no certificate we install is ever consulted.
static const char *const patch_sets[][2] = {
    {"romfs:/patches/nro_patches/disable_browser_ca_verification",
     "/atmosphere/nro_patches/disable_browser_ca_verification"},
    {"romfs:/patches/exefs_patches/disable_ca_verification",
     "/atmosphere/exefs_patches/disable_ca_verification"},
};
static const int patch_set_count = (int)(sizeof(patch_sets) / sizeof(patch_sets[0]));

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
    int removed = 0;
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
