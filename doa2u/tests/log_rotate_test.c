/* log_rotate.c: each start keeps the previous logs as name.1.txt (newest)
 * through name.N.txt (oldest), so the game's own restarts into and out of an
 * online session do not wipe the log of the session. */
#include "ui/log_rotate.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int is(const char *path, const char *text)
{
    char buf[64] = {0};
    FILE *f = fopen(path, "r");
    if (!f) return text == NULL;
    fgets(buf, sizeof buf, f);
    fclose(f);
    return text && strcmp(buf, text) == 0;
}

int main(void)
{
    char dir[MAX_PATH], base[MAX_PATH], p[MAX_PATH];
    int i;
    GetTempPathA(MAX_PATH, dir);
    snprintf(base, sizeof base, "%sdoa2u_logrot_%lu", dir, GetCurrentProcessId());
    CreateDirectoryA(base, NULL);
    snprintf(p, sizeof p, "%s/doa2u_log.txt", base);

    /* nothing yet: nothing to keep, no error */
    log_rotate(p, 3);
    snprintf(p, sizeof p, "%s/doa2u_log.1.txt", base); CHECK(is(p, NULL));

    /* five starts: the newest three earlier logs are kept, newest at .1 */
    for (i = 1; i <= 5; i++) {
        char text[16];
        snprintf(p, sizeof p, "%s/doa2u_log.txt", base);
        log_rotate(p, 3);
        snprintf(text, sizeof text, "run %d", i);
        put(p, text);
    }
    snprintf(p, sizeof p, "%s/doa2u_log.txt", base);   CHECK(is(p, "run 5"));
    snprintf(p, sizeof p, "%s/doa2u_log.1.txt", base); CHECK(is(p, "run 4"));
    snprintf(p, sizeof p, "%s/doa2u_log.2.txt", base); CHECK(is(p, "run 3"));
    snprintf(p, sizeof p, "%s/doa2u_log.3.txt", base); CHECK(is(p, "run 2"));
    snprintf(p, sizeof p, "%s/doa2u_log.4.txt", base); CHECK(is(p, NULL));

    /* a name without an extension still rotates */
    snprintf(p, sizeof p, "%s/plainlog", base);
    put(p, "a");
    log_rotate(p, 2);
    snprintf(p, sizeof p, "%s/plainlog.1", base); CHECK(is(p, "a"));

    for (i = 0; i <= 4; i++) {
        snprintf(p, sizeof p, i ? "%s/doa2u_log.%d.txt" : "%s/doa2u_log.txt", base, i);
        DeleteFileA(p);
    }
    snprintf(p, sizeof p, "%s/plainlog.1", base); DeleteFileA(p);
    RemoveDirectoryA(base);
    printf("log_rotate_test: %d failure(s)\n", failures);
    return failures;
}
