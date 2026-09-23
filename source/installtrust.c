#include "installtrust.h"
#include "hosts.h"        // openpak_root

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifndef OPENPAK_HOST_TEST
#include <switch.h>
#endif

bool openpak_firmware_is_supported(const char *display_version) {
    return display_version && strcmp(display_version, OPENPAK_FIRMWARE) == 0;
}

bool openpak_firmware_supported(char *ver, int verlen) {
#ifdef OPENPAK_HOST_TEST
    // Host build has no setsys; a test injects the version through ver.
    return openpak_firmware_is_supported(ver);
#else
    char display[0x40] = "";
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            snprintf(display, sizeof(display), "%s", fw.display_version);
        setsysExit();
    }
    if (ver) snprintf(ver, verlen, "%s", display[0] ? display : "unknown");
    return openpak_firmware_is_supported(display);
#endif
}

// Payload shipped inside the NRO, and where it lands on the SD card. The FS patch
// is a KIP patch: Atmosphere's fusee applies /atmosphere/kip_patches/<name>/<hash>.ips
// to the stock FS KIP at boot. This console boots hekate with pkg3=atmosphere/package3
// (fusee), so the kip_patches path is the one that runs here; a hekate-native KIP-load
// boot would instead need the patch listed in hekate's own kip config (see docs).
//
// No exefs_patches entry is needed: there is no ES/ticket patch (store titles carry no
// rights ID) and no loader patch (Atmosphere's own loader does not enforce ACID).
#ifdef OPENPAK_HOST_TEST
#define FS_PATCH_SRC "romfs/patches/kip_patches/openpak_fs_no_ncasig"
#else
#define FS_PATCH_SRC "romfs:/patches/kip_patches/openpak_fs_no_ncasig"
#endif
#define FS_PATCH_DST "/atmosphere/kip_patches/openpak_fs_no_ncasig"

static bool has_ips(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && strcmp(dot, ".ips") == 0) { found = true; break; }
    }
    closedir(d);
    return found;
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

bool openpak_store_fs_verified(void) {
    return has_ips(FS_PATCH_SRC);
}

bool openpak_store_installed(void) {
    char dst[320];
    snprintf(dst, sizeof(dst), "%s%s", openpak_root, FS_PATCH_DST);
    return has_ips(dst);
}

int openpak_store_install(char *msg, int msglen) {
    if (!openpak_store_fs_verified()) {
        snprintf(msg, msglen, "FS patch pending hardware verification — nothing installed");
        return 0;
    }
    char dst_dir[320];
    snprintf(dst_dir, sizeof(dst_dir), "%s%s", openpak_root, FS_PATCH_DST);
    make_dirs(dst_dir);

    int written = 0;
    DIR *d = opendir(FS_PATCH_SRC);
    if (d) {
        for (struct dirent *e; (e = readdir(d)) != NULL;) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".ips") != 0) continue;
            char from[512], to[512];
            snprintf(from, sizeof(from), "%s/%s", FS_PATCH_SRC, e->d_name);
            snprintf(to, sizeof(to), "%s/%s", dst_dir, e->d_name);
            if (copy_one(from, to)) written++;
        }
        closedir(d);
    }
    if (written)
        snprintf(msg, msglen, "Store installs on — reboot to apply the FS patch");
    else
        snprintf(msg, msglen, "Could not write the FS patch");
    return written;
}

// Deletes only files whose names match the payload, so a patch the user placed
// there by hand or one from another tool is left alone.
int openpak_store_remove(void) {
    char dst_dir[320];
    snprintf(dst_dir, sizeof(dst_dir), "%s%s", openpak_root, FS_PATCH_DST);
    int removed = 0;
    DIR *d = opendir(FS_PATCH_SRC);
    if (d) {
        for (struct dirent *e; (e = readdir(d)) != NULL;) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".ips") != 0) continue;
            char to[512];
            snprintf(to, sizeof(to), "%s/%s", dst_dir, e->d_name);
            if (remove(to) == 0) removed++;
        }
        closedir(d);
    }
    return removed;
}
