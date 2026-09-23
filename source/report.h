// Failure reports: when this tool fails — an install or remove that stopped, a hosts or patch
// write that did not land, a crash — it keeps a small report on the SD card and asks before
// sending it to openpak.org. Nothing leaves the console without the user saying yes (once, or
// "always").
//
// This half is pure: the meta JSON, the multipart body, the on-SD queue and the consent
// setting. It runs on a PC in `make test`. The console half (collecting, sending) is crash.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define REPORT_URL       "https://openpak.org/api/v1/crash-reports"
#define REPORT_QUEUE_MAX 8          // oldest reports are dropped past this
#define REPORT_META_MAX  4096

typedef struct { const char *key, *value; } report_field;

// Builds the endpoint's `meta` JSON. version and os are required; title_id, error_code and
// message are left out when NULL or "". Returns false (out holds "") if it does not fit.
bool report_meta(char *out, size_t cap, const char *version, const char *os,
                 const char *title_id, const char *error_code, const char *message,
                 const report_field *fields, int nfields);

// JSON string body for s (no quotes). Returns false if it does not fit.
bool report_json_escape(char *out, size_t cap, const char *s);

// A multipart/form-data body: the `meta` part, plus an `attachment` part when att is non-NULL.
// Returns a malloc'd buffer (free it) and its length, or NULL.
char *report_multipart(const char *boundary, const char *meta,
                       const char *att_name, const void *att, size_t att_len, size_t *out_len);

// The queue: one file per report in dir ("r00000001.json", ...), oldest first.
// save creates dir if needed and trims to REPORT_QUEUE_MAX; returns false if nothing was written.
bool report_queue_save(const char *dir, const char *meta);
// Fills names (oldest first) and returns the count, at most max.
int  report_queue_list(const char *dir, char names[][32], int max);
// The report's text, malloc'd, or NULL.
char *report_queue_read(const char *dir, const char *name);
void report_queue_drop(const char *dir, const char *name);

typedef enum { REPORT_ASK, REPORT_ALWAYS, REPORT_NEVER } report_consent;
// "always" / "never" (any case, surrounding whitespace ignored); anything else asks.
report_consent report_consent_parse(const char *s);
report_consent report_consent_load(const char *path);   // missing file: ask
bool report_consent_save(const char *path, report_consent c);
