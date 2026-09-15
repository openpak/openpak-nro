// SPDX-License-Identifier: AGPL-3.0-only
#include <switch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

static FILE *report;
static void line(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (report) {
        va_start(args, format);
        vfprintf(report, format, args);
        va_end(args);
        fflush(report);
    }
    consoleUpdate(NULL);
}

static void inspect(u32 id) {
    u32 size = 0, total = 0;
    Result rc = sslGetCertificateBufSize(&id, 1, &size);
    line("ID %lu: size result %08lX, bytes %lu\n",
         (unsigned long)id, (unsigned long)rc, (unsigned long)size);
    if (R_FAILED(rc) || size < sizeof(SslBuiltInCertificateInfo) || size > 1024 * 1024)
        return;
    void *buffer = calloc(1, size);
    if (!buffer) { line("Allocation failed\n"); return; }
    rc = sslGetCertificates(buffer, size, &id, 1, &total);
    line("  read result %08lX, entries %lu\n", (unsigned long)rc, (unsigned long)total);
    if (R_SUCCEEDED(rc) && total == 1) {
        SslBuiltInCertificateInfo *info = buffer;
        uintptr_t start = (uintptr_t)buffer, data = (uintptr_t)info->cert_data;
        if (data >= start && data - start <= size && info->cert_size <= size - (data - start)) {
            u8 digest[32];
            sha256CalculateHash(digest, info->cert_data, info->cert_size);
            char hex[65];
            for (unsigned i = 0; i < sizeof(digest); ++i)
                snprintf(hex + i * 2, 3, "%02x", digest[i]);
            line("  returned ID %lu, status %lu, DER bytes %llu\n",
                 (unsigned long)info->cert_id, (unsigned long)info->status,
                 (unsigned long long)info->cert_size);
            line("  SHA256 %s\n", hex);
            if (id == 1033)
                line("  OpenPak CA: %s\n", strcmp(hex,
                    "dfda1e4a95307e042ef371c5f6850856e26a77ad935c71ddde4d27461567920a")
                    == 0 ? "YES" : "NO");
        } else line("  Invalid certificate buffer bounds\n");
    }
    free(buffer);
}

int main(void) {
    consoleInit(NULL);
    mkdir("sdmc:/switch/openpak", 0777);
    report = fopen("sdmc:/switch/openpak/cert-probe.txt", "w");
    line("OpenPak certificate probe (read-only SSL queries)\n");
    if (!report) line("Report file unavailable; screen output only\n");
    Result rc = sslInitialize(1);
    line("sslInitialize: %08lX\n", (unsigned long)rc);
    if (R_SUCCEEDED(rc)) {
        const u32 ids[] = {1, 2, 1000, 1011, 1012, 1013, 1033};
        for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) inspect(ids[i]);
        sslExit();
    }
    if (report) { fclose(report); report = NULL; }
    line("Done. Report: /switch/openpak/cert-probe.txt\nPress + to exit.\n");
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
