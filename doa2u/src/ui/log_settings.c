/**
 * Log settings (see log_settings.h).
 */
#include <windows.h>
#include <stdio.h>

#include "log_settings.h"
#include "video_settings.h"
#include "../kernel/kernel.h"
#include "../kernel/perf.h"

static int g_enabled = 0;
static int g_own_stderr = 0;   /* stderr was a console or absent, so it is ours to route */

static void route_stderr(void)
{
    if (g_own_stderr) {
        freopen(g_enabled ? "doa2u_log.txt" : "NUL", "w", stderr);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    xbox_log_set_enabled(g_enabled);
}

void doa2u_log_init(void)
{
    /* GetConsoleMode only succeeds on a console handle, so a redirected or
     * piped stderr is left exactly as the caller set it. Double-clicked debug
     * builds own a real console, and unbuffered console writes starve the
     * guest badly enough that boot never completes; release builds have no
     * stderr at all. Both get routed here. */
    HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD cmode;
    g_own_stderr = herr == NULL || herr == INVALID_HANDLE_VALUE ||
                   GetConsoleMode(herr, &cmode);
    g_enabled = GetPrivateProfileIntA("General", "Logging", 1,   /* on during bring-up */
                                      doa2u_settings_ini_path()) != 0;
    route_stderr();
}

int doa2u_log_enabled(void) { return g_enabled; }

void doa2u_log_set_enabled(int on)
{
    on = on ? 1 : 0;
    if (on == g_enabled) return;
    g_enabled = on;
    route_stderr();
}

int doa2u_log_save(void)
{
    /* Logging: 0 = off, 1 = write doa2u_log.txt and xbox_kernel.log. */
    return WritePrivateProfileStringA("General", "Logging", g_enabled ? "1" : "0",
                                      doa2u_settings_ini_path());
}

static int g_movies = -1;   /* -1 = not read from the ini yet */

int doa2u_movies_enabled(void)
{
    if (g_movies < 0)
        g_movies = GetPrivateProfileIntA("General", "Movies", 1,
                                         doa2u_settings_ini_path()) != 0;
    return g_movies;
}

void doa2u_movies_set_enabled(int on) { g_movies = on ? 1 : 0; }

int doa2u_movies_save(void)
{
    /* Movies: 0 = skip every FMV, 1 = play them. */
    return WritePrivateProfileStringA("General", "Movies",
                                      doa2u_movies_enabled() ? "1" : "0",
                                      doa2u_settings_ini_path());
}

static int g_surround = -1, g_surround_boot = -1;   /* -1 = not read from the ini yet */

int doa2u_surround_enabled(void)
{
    if (g_surround < 0)
        g_surround = GetPrivateProfileIntA("General", "Surround", 0, doa2u_settings_ini_path()) != 0;
    return g_surround;
}

void doa2u_surround_set_enabled(int on) { g_surround = on ? 1 : 0; }

int doa2u_surround_save(void)
{
    /* Surround: 0 = stereo, 1 = 5.1 (applies at the next start). */
    return WritePrivateProfileStringA("General", "Surround", doa2u_surround_enabled() ? "1" : "0",
                                      doa2u_settings_ini_path());
}

int doa2u_surround_active(void)
{
    if (g_surround_boot < 0) g_surround_boot = doa2u_surround_enabled();
    return g_surround_boot;
}

static int g_skip_intro = -1;   /* -1 = not read from the ini yet */

int doa2u_skip_intro_enabled(void)
{
    if (g_skip_intro < 0)
        g_skip_intro = GetPrivateProfileIntA("General", "SkipIntro", 0, doa2u_settings_ini_path()) != 0;
    return g_skip_intro;
}

void doa2u_skip_intro_set_enabled(int on) { g_skip_intro = on ? 1 : 0; }

int doa2u_skip_intro_save(void)
{
    /* SkipIntro: 1 = skip the boot logo and opening movies. */
    return WritePrivateProfileStringA("General", "SkipIntro", doa2u_skip_intro_enabled() ? "1" : "0",
                                      doa2u_settings_ini_path());
}

static int g_reverb = -1;   /* -1 = not read from the ini yet */

int doa2u_reverb_enabled(void)
{
    if (g_reverb < 0)
        g_reverb = GetPrivateProfileIntA("General", "Reverb", 1, doa2u_settings_ini_path()) != 0;
    return g_reverb;
}

void doa2u_reverb_set_enabled(int on) { g_reverb = on ? 1 : 0; }

int doa2u_reverb_save(void)
{
    /* Reverb: 0 = off, 1 = the game's room reverb on the host. */
    return WritePrivateProfileStringA("General", "Reverb", doa2u_reverb_enabled() ? "1" : "0",
                                      doa2u_settings_ini_path());
}

static int g_fps = -1, g_fps_corner = -1;   /* -1 = not read from the ini yet */

int doa2u_fps_enabled(void)
{
    if (g_fps < 0)
        g_fps = GetPrivateProfileIntA("General", "FpsCounter", 0, doa2u_settings_ini_path()) != 0;
    return g_fps;
}

void doa2u_fps_set_enabled(int on) { g_fps = on ? 1 : 0; }

int doa2u_fps_corner(void)
{
    if (g_fps_corner < 0) {
        g_fps_corner = (int)GetPrivateProfileIntA("General", "FpsCorner", FPS_TOP_LEFT,
                                                  doa2u_settings_ini_path());
        if (g_fps_corner < FPS_TOP_LEFT || g_fps_corner > FPS_BOTTOM_RIGHT)
            g_fps_corner = FPS_TOP_LEFT;
    }
    return g_fps_corner;
}

void doa2u_fps_set_corner(int corner)
{
    if (corner >= FPS_TOP_LEFT && corner <= FPS_BOTTOM_RIGHT) g_fps_corner = corner;
}

int doa2u_fps_save(void)
{
    /* FpsCounter: 0 = off, 1 = on. FpsCorner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right. */
    char corner[2] = { (char)('0' + doa2u_fps_corner()), 0 };
    return WritePrivateProfileStringA("General", "FpsCounter", doa2u_fps_enabled() ? "1" : "0",
                                      doa2u_settings_ini_path()) &&
           WritePrivateProfileStringA("General", "FpsCorner", corner, doa2u_settings_ini_path());
}

static int g_perfmon = -1;   /* -1 = not read from the ini yet */

int doa2u_perfmon_enabled(void)
{
    if (g_perfmon < 0)
        g_perfmon = GetPrivateProfileIntA("General", "PerfMonitor", 0, doa2u_settings_ini_path()) != 0;
    return g_perfmon;
}

void doa2u_perfmon_set_enabled(int on)
{
    g_perfmon = on ? 1 : 0;
    perf_set_threshold_ms(doa2u_perfmon_threshold());
    perf_set_enabled(g_perfmon);
}

static int g_perf_threshold = -1;   /* -1 = not read from the ini yet */

int doa2u_perfmon_threshold(void)
{
    if (g_perf_threshold < 0) {
        g_perf_threshold = (int)GetPrivateProfileIntA("General", "PerfThreshold", 20,
                                                      doa2u_settings_ini_path());
        if (g_perf_threshold < 17) g_perf_threshold = 17;
        if (g_perf_threshold > 200) g_perf_threshold = 200;
    }
    return g_perf_threshold;
}

void doa2u_perfmon_set_threshold(int ms)
{
    if (ms < 17) ms = 17;
    if (ms > 200) ms = 200;
    g_perf_threshold = ms;
    perf_set_threshold_ms(ms);
}

int doa2u_perfmon_save(void)
{
    /* PerfMonitor: 0 = off, 1 = measure, graph in the Esc menu, write doa2u_perf.log.
     * PerfThreshold: frames longer than this many ms are logged. */
    char ms[8];
    _snprintf_s(ms, sizeof(ms), _TRUNCATE, "%d", doa2u_perfmon_threshold());
    return WritePrivateProfileStringA("General", "PerfMonitor", doa2u_perfmon_enabled() ? "1" : "0",
                                      doa2u_settings_ini_path()) &&
           WritePrivateProfileStringA("General", "PerfThreshold", ms, doa2u_settings_ini_path());
}
