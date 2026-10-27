/* online_settings.c: defaults, the ini round trip, out-of-range fallbacks. */
#include "online/online_settings.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* online_settings.c falls back to this when no test path is set; the test
   never lets it be used, but the symbol must exist. */
const char *doa2u_settings_ini_path(void) { return "unused.ini"; }

int main(void)
{
    char path[MAX_PATH], buf[128];
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, MAX_PATH, "doa2u_online_test.ini");
    DeleteFileA(path);
    online_settings_set_ini_path(path);

    /* defaults on a missing file */
    CHECK(online_settings_load() == 0);
    CHECK(online_get_port() == 7000 && online_get_delay_auto() == 1 && online_get_delay() == 2);
    CHECK(online_get_last_join()[0] == 0 && online_get_diag() == 0);

    /* setters clamp */
    online_set_port(7015); CHECK(online_get_port() == 7015);
    online_set_port(7016); CHECK(online_get_port() == 7000);
    online_set_port(6999); CHECK(online_get_port() == 7000);
    online_set_delay(0, 0);  CHECK(online_get_delay_auto() == 0 && online_get_delay() == 1);
    online_set_delay(1, 99); CHECK(online_get_delay_auto() == 1 && online_get_delay() == 8);

    /* save writes every key; load reads them back */
    online_set_port(7004);
    online_set_delay(0, 5);
    online_set_last_join("ywBxBz");
    online_set_diag(1);
    CHECK(online_settings_save() == 1);
    GetPrivateProfileStringA("Online", "Port", "", buf, sizeof buf, path);      CHECK(strcmp(buf, "7004") == 0);
    GetPrivateProfileStringA("Online", "DelayAuto", "", buf, sizeof buf, path); CHECK(strcmp(buf, "0") == 0);
    GetPrivateProfileStringA("Online", "Delay", "", buf, sizeof buf, path);     CHECK(strcmp(buf, "5") == 0);
    GetPrivateProfileStringA("Online", "LastJoin", "", buf, sizeof buf, path);  CHECK(strcmp(buf, "ywBxBz") == 0);
    GetPrivateProfileStringA("Online", "Diag", "", buf, sizeof buf, path);      CHECK(strcmp(buf, "1") == 0);
    online_set_port(7000); online_set_delay(1, 2); online_set_last_join(""); online_set_diag(0);
    CHECK(online_settings_load() == 1);
    CHECK(online_get_port() == 7004 && online_get_delay_auto() == 0 && online_get_delay() == 5);
    CHECK(strcmp(online_get_last_join(), "ywBxBz") == 0 && online_get_diag() == 1);

    /* the connection stats box (F9): off by default, and a toggle is written
     * at once, alone, so it lasts without pressing Save */
    CHECK(online_get_show_stats() == 0);
    online_set_port(7009);                                   /* an unsaved edit */
    online_set_show_stats(1);
    GetPrivateProfileStringA("Online", "ShowStats", "", buf, sizeof buf, path); CHECK(strcmp(buf, "1") == 0);
    GetPrivateProfileStringA("Online", "Port", "", buf, sizeof buf, path);      CHECK(strcmp(buf, "7004") == 0);
    online_set_show_stats(0);
    CHECK(online_settings_load() == 1 && online_get_show_stats() == 0);
    online_set_show_stats(1);
    CHECK(online_settings_load() == 1 && online_get_show_stats() == 1);
    online_set_show_stats(0);

    /* a hand-edited ini out of range reads as the defaults */
    WritePrivateProfileStringA("Online", "Port", "8000", path);
    WritePrivateProfileStringA("Online", "Delay", "12", path);
    CHECK(online_settings_load() == 1);
    CHECK(online_get_port() == 7000 && online_get_delay() == 2);
    WritePrivateProfileStringA("Online", "Delay", "0", path);
    CHECK(online_settings_load() == 1 && online_get_delay() == 2);

    /* LastJoin keeps 63 characters */
    memset(buf, 'x', 100); buf[100] = 0;
    online_set_last_join(buf);
    CHECK(strlen(online_get_last_join()) == 63);

    DeleteFileA(path);
    printf("online_settings_test: %d failure(s)\n", failures);
    return failures;
}
