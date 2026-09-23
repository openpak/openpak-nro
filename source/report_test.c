// Host-side check of the failure-report logic — no console needed:
//   cc -o /tmp/report_test source/report.c source/report_test.c && /tmp/report_test
#include "report.h"
#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *get(struct json_object *o, const char *key) {
    struct json_object *v;
    return json_object_object_get_ex(o, key, &v) ? json_object_get_string(v) : NULL;
}

int main(void) {
    // Escaping: quotes, backslashes, newlines and control bytes; UTF-8 untouched.
    char esc[64];
    assert(report_json_escape(esc, sizeof(esc), "a\"b\\c\nd\x01" "é"));
    assert(strcmp(esc, "a\\\"b\\\\c\\nd\\u0001é") == 0);
    assert(!report_json_escape(esc, 4, "abcdef") && esc[0] == '\0');

    // Meta: required keys always, optional ones only when given, fields as an object.
    char meta[REPORT_META_MAX];
    report_field f[] = {{"action", "install"}, {"ui", "console \"text\""}};
    assert(report_meta(meta, sizeof(meta), "0.3.7", "Horizon 22.5.0, Atmosphere 1.11.2",
                       NULL, "0x2", "Could not write\n/atmosphere/hosts/default.txt", f, 2));
    struct json_object *o = json_tokener_parse(meta);
    assert(o);
    assert(strcmp(get(o, "source"), "nro") == 0);
    assert(strcmp(get(o, "version"), "0.3.7") == 0);
    assert(strcmp(get(o, "os"), "Horizon 22.5.0, Atmosphere 1.11.2") == 0);
    assert(strcmp(get(o, "error_code"), "0x2") == 0);
    assert(strcmp(get(o, "message"), "Could not write\n/atmosphere/hosts/default.txt") == 0);
    assert(!get(o, "title_id"));
    struct json_object *fields;
    assert(json_object_object_get_ex(o, "fields", &fields));
    assert(strcmp(get(fields, "action"), "install") == 0);
    assert(strcmp(get(fields, "ui"), "console \"text\"") == 0);
    json_object_put(o);

    assert(report_meta(meta, sizeof(meta), "1", "Horizon", "", "", "", NULL, 0));
    assert(strcmp(meta, "{\"source\":\"nro\",\"version\":\"1\",\"os\":\"Horizon\"}") == 0);
    assert(!report_meta(meta, 20, "1", "Horizon", NULL, NULL, NULL, NULL, 0) && meta[0] == '\0');

    // Multipart: meta part, optional attachment, closing boundary.
    size_t len;
    char *body = report_multipart("BND", "{}", NULL, NULL, 0, &len);
    const char *want = "--BND\r\nContent-Disposition: form-data; name=\"meta\"\r\n"
                       "Content-Type: application/json\r\n\r\n{}\r\n--BND--\r\n";
    assert(body && len == strlen(want) && memcmp(body, want, len) == 0);
    free(body);
    body = report_multipart("BND", "{}", "crash.log", "a\0b", 3, &len);
    assert(body && strstr(body, "name=\"attachment\"; filename=\"crash.log\"\r\n"));
    assert(memcmp(body + len - 3 - strlen("\r\n--BND--\r\n"), "a\0b", 3) == 0);
    assert(memcmp(body + len - strlen("\r\n--BND--\r\n"), "\r\n--BND--\r\n", 11) == 0);
    free(body);

    // Queue: created on demand, oldest first, capped, readable, droppable.
    char root[] = "/tmp/openpak_report_XXXXXX";
    assert(mkdtemp(root));
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/switch/openpak/reports", root);
    char names[32][32];
    assert(report_queue_list(dir, names, 32) == 0);
    for (int i = 0; i < REPORT_QUEUE_MAX + 3; i++) {
        char m[32];
        snprintf(m, sizeof(m), "{\"n\":%d}", i);
        assert(report_queue_save(dir, m));
    }
    int n = report_queue_list(dir, names, 32);
    assert(n == REPORT_QUEUE_MAX);
    char *first = report_queue_read(dir, names[0]);
    assert(first && strcmp(first, "{\"n\":3}") == 0);        // the three oldest were dropped
    free(first);
    char *last = report_queue_read(dir, names[n - 1]);
    assert(last && strcmp(last, "{\"n\":10}") == 0);
    free(last);
    report_queue_drop(dir, names[0]);
    assert(report_queue_list(dir, names, 32) == REPORT_QUEUE_MAX - 1);
    char stray[160];                                            // other files are not reports
    snprintf(stray, sizeof(stray), "%s/notes.txt", dir);
    fclose(fopen(stray, "wb"));
    assert(report_queue_list(dir, names, 32) == REPORT_QUEUE_MAX - 1);

    // Consent: parse, and a round trip through the settings file.
    assert(report_consent_parse("always") == REPORT_ALWAYS);
    assert(report_consent_parse("  NEVER\r\n") == REPORT_NEVER);
    assert(report_consent_parse("ask") == REPORT_ASK);
    assert(report_consent_parse("alwaysx") == REPORT_ASK);
    assert(report_consent_parse("") == REPORT_ASK);
    assert(report_consent_parse(NULL) == REPORT_ASK);
    char path[160];
    snprintf(path, sizeof(path), "%s/switch/openpak/reports.txt", root);
    assert(report_consent_load(path) == REPORT_ASK);
    assert(report_consent_save(path, REPORT_NEVER) && report_consent_load(path) == REPORT_NEVER);
    assert(report_consent_save(path, REPORT_ALWAYS) && report_consent_load(path) == REPORT_ALWAYS);

    printf("reports: all checks passed\n");
    return 0;
}
