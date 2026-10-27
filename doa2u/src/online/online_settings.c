/* Online settings (see online_settings.h). */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "online_settings.h"

const char *doa2u_settings_ini_path(void);   /* ui/video_settings.c */

static unsigned g_port = ONLINE_PORT_DEFAULT;
static int  g_delay_auto = 1;
static int  g_delay = ONLINE_DELAY_DEFAULT;
static char g_last_join[ONLINE_LAST_JOIN_MAX];
static int  g_diag = 0;
static int  g_show_stats = 0;
static int  g_loaded = 0;
static char g_test_path[MAX_PATH];

static const char *ini(void)
{
    return g_test_path[0] ? g_test_path : doa2u_settings_ini_path();
}

static unsigned clamp_port(unsigned p)
{
    return (p >= ONLINE_PORT_DEFAULT && p < ONLINE_PORT_DEFAULT + ONLINE_PORT_COUNT) ? p : ONLINE_PORT_DEFAULT;
}

static int clamp_delay(int d)
{
    return (d >= 1 && d <= 8) ? d : ONLINE_DELAY_DEFAULT;
}

static void ensure(void)
{
    if (!g_loaded) online_settings_load();
}

void online_settings_set_ini_path(const char *path)
{
    if (path) strncpy(g_test_path, path, sizeof(g_test_path) - 1);
    else g_test_path[0] = 0;
    g_test_path[sizeof(g_test_path) - 1] = 0;
    g_loaded = 0;
}

int online_settings_load(void)
{
    const char *p = ini();
    g_port = clamp_port(GetPrivateProfileIntA("Online", "Port", ONLINE_PORT_DEFAULT, p));
    g_delay_auto = GetPrivateProfileIntA("Online", "DelayAuto", 1, p) ? 1 : 0;
    g_delay = clamp_delay((int)GetPrivateProfileIntA("Online", "Delay", ONLINE_DELAY_DEFAULT, p));
    GetPrivateProfileStringA("Online", "LastJoin", "", g_last_join, sizeof(g_last_join), p);
    g_diag = GetPrivateProfileIntA("Online", "Diag", 0, p) ? 1 : 0;
    g_show_stats = GetPrivateProfileIntA("Online", "ShowStats", 0, p) ? 1 : 0;
    g_loaded = 1;
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

int online_settings_save(void)
{
    const char *p = ini();
    char v[16];
    int ok;
    ensure();
    snprintf(v, sizeof v, "%u", g_port);
    ok = WritePrivateProfileStringA("Online", "Port", v, p);
    ok = WritePrivateProfileStringA("Online", "DelayAuto", g_delay_auto ? "1" : "0", p) && ok;
    snprintf(v, sizeof v, "%d", g_delay);
    ok = WritePrivateProfileStringA("Online", "Delay", v, p) && ok;
    ok = WritePrivateProfileStringA("Online", "LastJoin", g_last_join, p) && ok;
    ok = WritePrivateProfileStringA("Online", "Diag", g_diag ? "1" : "0", p) && ok;
    return ok;
}

unsigned online_get_port(void) { ensure(); return g_port; }
void online_set_port(unsigned port) { ensure(); g_port = clamp_port(port); }
int online_get_delay_auto(void) { ensure(); return g_delay_auto; }
int online_get_delay(void) { ensure(); return g_delay; }

void online_set_delay(int automatic, int manual)
{
    ensure();
    g_delay_auto = automatic ? 1 : 0;
    g_delay = manual < 1 ? 1 : manual > 8 ? 8 : manual;
}

const char *online_get_last_join(void) { ensure(); return g_last_join; }

void online_set_last_join(const char *code)
{
    ensure();
    strncpy(g_last_join, code ? code : "", sizeof(g_last_join) - 1);
    g_last_join[sizeof(g_last_join) - 1] = 0;
}

int online_get_diag(void) { ensure(); return g_diag; }
int online_get_show_stats(void) { ensure(); return g_show_stats; }

void online_set_show_stats(int on)
{
    ensure();
    g_show_stats = on ? 1 : 0;
    WritePrivateProfileStringA("Online", "ShowStats", g_show_stats ? "1" : "0", ini());
}
void online_set_diag(int on) { ensure(); g_diag = on ? 1 : 0; }
