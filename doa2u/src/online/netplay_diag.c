/* netplay_diag.c - see netplay_diag.h. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netplay_diag.h"
#include "netplay_dump.h"
#include "netplay_game.h"
#include "online_settings.h"
#include "netplay_session.h"
#include "game/recomp/recomp_types.h"
#include "kernel/xbox_memory_layout.h"

float d3d8_GetGameFPS(void);   /* d3d/d3d8_device.c */

static uint32_t      s_builder_calls;
static int           s_mirror;
static volatile LONG s_dump_req;
static char          s_dump_tag[NP_DUMP_TAG + 1];
static char          s_last_dump[MAX_PATH];
#define DUMP_AT_MAX 8
#define DUMP_AT_FIGHT 0x80000000u            /* bit: relative to the first fight frame; low bits = offset + 1 (0 = f-1) */
#define DUMP_AT_CALL  0x40000000u            /* bit: at pad-builder call N since boot, session or not ("cN") */
static uint32_t      s_dump_at[DUMP_AT_MAX];
static int           s_dirty_on, s_dirty_file, s_dirty_phase;
static uint8_t      *s_snap, *s_cur;
static char          s_dirty_report[200];
static int           s_dump_at_n;
static uint8_t      *s_prev_ram;                 /* rolling copy for "f-1" */
/* Scripted pad (netplay_pad.on): lines "FRAMES BUTTONS A B X Y LX LY" (BUTTONS hex,
 * the rest decimal), each held for FRAMES pad-builder calls, played in order from
 * the first call after the file appears; the file is taken (deleted) when read.
 * Nothing reaches the keyboard or the window, so a test batch runs while the PC
 * is in use, and the input is exact per frame. */
#define PADSCRIPT_MAX 512
static struct { uint32_t frames; np_pad pad; } s_script[PADSCRIPT_MAX];
static int      s_script_n, s_script_i;
static uint32_t s_script_left;
static uint32_t      s_prev_frame = NP_NO_FRAME;
static uint32_t      s_fight_frame = NP_NO_FRAME;  /* first fight frame of the running session */
static int           s_screens_forced;          /* ctl file: 1 on, -1 off, 0 = follow netplay_screens.on */
static int           s_p2_neutral;              /* ctl 'p2neutral 1': the mirrored pad 1 stays connected but idle */
static char          s_dump_at_tag[NP_DUMP_TAG + 1];
static int           s_screens;

static void exe_path(const char *name, char *out, size_t cap)
{
    char *slash;
    if (!GetModuleFileNameA(NULL, out, (DWORD)cap)) { out[0] = 0; }
    slash = strrchr(out, '\\');
    if (slash) slash[1] = 0; else out[0] = 0;
    strncat(out, name, cap - strlen(out) - 1);
}

static int exe_file_exists(const char *name)
{
    char p[MAX_PATH];
    exe_path(name, p, sizeof p);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

/* Reads the first line of a request file and deletes it. 1 if it existed. */
static int exe_file_take(const char *name, char *buf, size_t cap)
{
    char p[MAX_PATH];
    FILE *f;
    exe_path(name, p, sizeof p);
    f = fopen(p, "r");
    if (!f) return 0;
    buf[0] = 0;
    if (fgets(buf, (int)cap, f)) buf[strcspn(buf, "\r\n")] = 0;
    fclose(f);
    DeleteFileA(p);
    return 1;
}

const char *netplay_diag_dump_dir(void)
{
    static char d[MAX_PATH];
    if (!d[0]) {
        DWORD n = GetEnvironmentVariableA("DOA2U_DUMP_DIR", d, MAX_PATH);
        if (!n || n >= MAX_PATH) {
            GetTempPathA(MAX_PATH, d);
            strncat(d, "doa2u_dumps", MAX_PATH - strlen(d) - 1);
        }
        CreateDirectoryA(d, NULL);
    }
    return d;
}

const char *netplay_diag_last_dump(void) { return s_last_dump; }
uint32_t netplay_diag_builder_calls(void) { return s_builder_calls; }

void netplay_diag_request_dump(const char *tag)
{
    size_t i;
    strncpy(s_dump_tag, tag ? tag : "", NP_DUMP_TAG);
    s_dump_tag[NP_DUMP_TAG] = 0;
    for (i = 0; s_dump_tag[i]; i++) {                       /* the tag is part of a file name */
        char c = s_dump_tag[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) s_dump_tag[i] = '_';
    }
    InterlockedExchange(&s_dump_req, 1);
}

void netplay_diag_dump_at(uint32_t frame, const char *tag)
{
    strncpy(s_dump_at_tag, tag ? tag : "", NP_DUMP_TAG);
    s_dump_at_tag[NP_DUMP_TAG] = 0;
    s_dump_at_n = 0;
    s_dump_at[s_dump_at_n++] = frame;
}

static uint32_t parse_frame_spec(const char *s)
{
    if (*s == 'c' || *s == 'C')                       /* cN: pad-builder call N since boot */
        return DUMP_AT_CALL | ((uint32_t)strtoul(s + 1, NULL, 10) & 0x3FFFFFFFu);
    if (*s == 'f' || *s == 'F') {                     /* f+N or f-1 */
        long n = strtol(s + 1, NULL, 10);
        if (n < -1) n = -1;
        return DUMP_AT_FIGHT | (uint32_t)(n + 1);
    }
    return (uint32_t)strtoul(s, NULL, 10);
}

static void dump_at_list(const char *list, const char *tag)
{
    netplay_diag_dump_at(parse_frame_spec(list), tag);
    while ((list = strchr(list, ',')) != NULL && s_dump_at_n < DUMP_AT_MAX)
        s_dump_at[s_dump_at_n++] = parse_frame_spec(++list);
    s_fight_frame = NP_NO_FRAME;
}

static void load_pad_script(void)
{
    char p[MAX_PATH], line[160];
    FILE *f;
    exe_path("netplay_pad.on", p, sizeof p);
    f = fopen(p, "r");
    if (!f) return;
    s_script_n = 0; s_script_i = 0; s_script_left = 0;
    while (fgets(line, sizeof line, f) && s_script_n < PADSCRIPT_MAX) {
        unsigned fr, bt, a, b, x, y; int lx, ly;
        if (sscanf(line, "%u %x %u %u %u %u %d %d", &fr, &bt, &a, &b, &x, &y, &lx, &ly) != 8) continue;
        memset(&s_script[s_script_n].pad, 0, sizeof(np_pad));
        s_script[s_script_n].frames = fr;
        s_script[s_script_n].pad.buttons = (uint16_t)bt;
        s_script[s_script_n].pad.analog[0] = (uint8_t)a;
        s_script[s_script_n].pad.analog[1] = (uint8_t)b;
        s_script[s_script_n].pad.analog[2] = (uint8_t)x;
        s_script[s_script_n].pad.analog[3] = (uint8_t)y;
        s_script[s_script_n].pad.lx = (int16_t)lx;
        s_script[s_script_n].pad.ly = (int16_t)ly;
        s_script_n++;
    }
    fclose(f);
    DeleteFileA(p);
    if (s_script_n) s_script_left = s_script[0].frames;
    if (online_get_diag()) {
        fprintf(stderr, "[netplay] pad script: %d steps (pad-builder call %u)\n", s_script_n, s_builder_calls);
        fflush(stderr);
    }
}

/* One call of the script: pad 0's raw slot gets the current step. */
static void play_pad_script(void)
{
    if (s_script_i >= s_script_n) return;
    while (s_script_left == 0) {
        if (++s_script_i >= s_script_n) { s_script_n = 0; return; }
        s_script_left = s_script[s_script_i].frames;
    }
    np_game_pad_write(0, &s_script[s_script_i].pad, 0x2000u + s_builder_calls);
    s_script_left--;
}

/* Control file (netplay_ctl.on): one command per line, taken when read.
 *   arm 0|1 [host|joiner]   mirror 0|1   record 0|1   replay <file>   screens 0|1 */
static void take_ctl_file(void)
{
    char p[MAX_PATH], line[200];
    FILE *f;
    exe_path("netplay_ctl.on", p, sizeof p);
    f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char cmd[16] = "", arg[160] = "", arg2[16] = "";
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%15s %159s %15s", cmd, arg, arg2) < 1) continue;
        if (!strcmp(cmd, "arm")) netplay_set_armed(atoi(arg), !strcmp(arg2, "joiner") ? NP_ROLE_JOINER : NP_ROLE_HOST);
        else if (!strcmp(cmd, "mirror")) netplay_set_mirror(atoi(arg));
        else if (!strcmp(cmd, "record")) netplay_set_record(atoi(arg));
        else if (!strcmp(cmd, "replay")) netplay_request_replay(arg);
        else if (!strcmp(cmd, "screens")) s_screens_forced = atoi(arg) ? 1 : -1;
        else if (!strcmp(cmd, "p2neutral")) s_p2_neutral = atoi(arg) ? 1 : 0;
        if (online_get_diag()) { fprintf(stderr, "[netplay] ctl: %s %s %s\n", cmd, arg, arg2); fflush(stderr); }
    }
    fclose(f);
    DeleteFileA(p);
}

void netplay_boot_init(void)
{
    char p[MAX_PATH], line[300], replay[MAX_PATH] = "";
    unsigned seed = 0;
    int have = 0, mirror = 0;
    FILE *f;
    exe_path("netplay_boot.on", p, sizeof p);
    f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char cmd[16] = "", arg[280] = "";
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%15s %279s", cmd, arg) < 1) continue;
        if (!strcmp(cmd, "record")) { seed = (unsigned)strtoul(arg, NULL, 16); have = 1; }
        else if (!strcmp(cmd, "replay")) { exe_path(arg, replay, sizeof replay); have = 2; }
        else if (!strcmp(cmd, "mirror")) mirror = 1;
    }
    fclose(f);
    DeleteFileA(p);
    if (!have) return;
    netplay_set_mirror(mirror);
    if (have == 1) netplay_set_record(1);
    if (!netplay_boot_begin(seed, have == 2 ? replay : NULL)) {
        fprintf(stderr, "[netplay] boot session NOT started (%s)\n", have == 2 ? replay : "record");
        fflush(stderr);
    }
}

static int dump_at_wants_prev(void)
{
    int i;
    for (i = 0; i < s_dump_at_n; i++)
        if (s_dump_at[i] == (DUMP_AT_FIGHT | 0u)) return 1;
    return 0;
}

static void write_dump_from(const uint8_t *ram, uint32_t frame, const char *tag);

/* Write guest RAM 0..XBOX_LOW_END: the live image, or `ram` (a copy of it). */
static void write_dump_from(const uint8_t *ram, uint32_t frame, const char *tag)
{
    static const uint8_t zero[NP_PAGE];
    char path[MAX_PATH];
    SYSTEMTIME t;
    np_dump_info info;
    FILE *f;
    uint32_t va;
    GetLocalTime(&t);
    _snprintf(path, sizeof path - 1, "%s\\dump_%02u%02u%02u_%s.bin", netplay_diag_dump_dir(),
              t.wHour, t.wMinute, t.wSecond, tag[0] ? tag : "untagged");
    path[sizeof path - 1] = 0;
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[netplay] cannot write %s\n", path); return; }
    memset(&info, 0, sizeof info);
    info.lo = 0;
    info.hi = XBOX_LOW_END;
    info.frame = frame;
    info.builder_calls = s_builder_calls;
    strncpy(info.tag, tag, NP_DUMP_TAG);
    np_dump_write_header(f, &info);
    for (va = info.lo; va < info.hi; va += NP_PAGE) {
        if (ram) { fwrite(ram + va, 1, NP_PAGE, f); continue; }
        {
            const void *host = (const void *)XBOX_PTR(va);
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(host, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
                !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                fwrite(host, 1, NP_PAGE, f);
            else
                fwrite(zero, 1, NP_PAGE, f);                    /* not committed: reads as zero */
        }
    }
    fclose(f);
    strncpy(s_last_dump, path, sizeof s_last_dump - 1);
    fprintf(stderr, "[netplay] wrote %s (pad-builder call %u)\n", path, s_builder_calls);
    fflush(stderr);
}

static void write_dump(void)
{
    write_dump_from(NULL, netplay_active() ? netplay_session_frame() : NP_NO_FRAME, s_dump_tag);
}

/* The rolling copy for "f-1": committed pages only, the rest zero. One
* VirtualQuery per region, not per page (a per-page walk took ~100 ms a frame
* and the character select crawled). */
static void copy_ram(uint8_t *dst)
{
    uint32_t va = 0;
    while (va < XBOX_LOW_END) {
        const uint8_t *host = (const uint8_t *)XBOX_PTR(va);
        MEMORY_BASIC_INFORMATION mbi;
        uint32_t len;
        if (!VirtualQuery(host, &mbi, sizeof mbi)) { memset((void *)(dst + va), 0, XBOX_LOW_END - va); return; }
        len = (uint32_t)((const uint8_t *)mbi.BaseAddress + mbi.RegionSize - host);
        if (len > XBOX_LOW_END - va) len = XBOX_LOW_END - va;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            memcpy((void *)(dst + va), host, len);
        else
            memset((void *)(dst + va), 0, len);
        va += len;
    }
}

void        netplay_diag_set_dirty(int on) { s_dirty_on = on ? 1 : 0; }
int         netplay_diag_dirty(void) { return s_dirty_on; }
const char *netplay_diag_dirty_report(void) { return s_dirty_report; }

/* Uses copy_ram above (region-wise VirtualQuery; the brief's per-page copy was
 * superseded by it, see the ledger). */
static void dirty_tick(void)
{
    static const uint32_t q = (XBOX_LOW_END - XBOX_HEAP_BASE) / 4u;
    uint32_t bounds[8] = { 0, XBOX_IMAGE_END, XBOX_STACK_BASE, XBOX_HEAP_BASE,
                           XBOX_HEAP_BASE + q, XBOX_HEAP_BASE + 2 * q, XBOX_HEAP_BASE + 3 * q, XBOX_LOW_END };
    uint32_t counts[7], total;
    if (!(s_dirty_on || s_dirty_file) || netplay_has_provider()) { s_dirty_phase = 0; return; }
    if (!s_snap) {
        s_snap = (uint8_t *)malloc(XBOX_LOW_END);
        s_cur = (uint8_t *)malloc(XBOX_LOW_END);
        if (!s_snap || !s_cur) { free(s_snap); free(s_cur); s_snap = s_cur = NULL; s_dirty_on = s_dirty_file = 0; return; }
    }
    if (s_dirty_phase == 0) {
        if (s_builder_calls % 120 != 0) return;
        copy_ram(s_snap);
        s_dirty_phase = 1;
        return;
    }
    copy_ram(s_cur);
    s_dirty_phase = 0;
    total = np_pages_diff(s_snap, s_cur, XBOX_LOW_END, 0, bounds, 7, counts);
    _snprintf(s_dirty_report, sizeof s_dirty_report - 1,
              "dirty pages in one frame: %u (image %u, runtime %u, stack %u, heap %u/%u/%u/%u)%s",
              total, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6],
              np_game_in_fight() ? " in a fight" : "");
    s_dirty_report[sizeof s_dirty_report - 1] = 0;
    fprintf(stderr, "[netplay] %s\n", s_dirty_report);
    fflush(stderr);
}

static void rate_log(void)
{
    static LARGE_INTEGER t0, freq;
    static uint32_t c0;
    LARGE_INTEGER now;
    if (s_builder_calls % 600 != 0 || !online_get_diag()) return;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    if (t0.QuadPart) {
        double secs = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        fprintf(stderr, "[netplay] pad builder %.1f calls/s, game %.1f fps\n",
                (double)(s_builder_calls - c0) / secs, d3d8_GetGameFPS());
        fflush(stderr);
    }
    t0 = now;
    c0 = s_builder_calls;
}

void netplay_diag_frame(void)
{
    s_builder_calls++;
    if ((s_builder_calls & 31) == 0) {                      /* request files, twice a second */
        char tag[64];
        s_mirror = exe_file_exists("netplay_mirror.on");
        s_dirty_file = exe_file_exists("netplay_dirty.on");
        s_screens = s_screens_forced ? (s_screens_forced > 0) : exe_file_exists("netplay_screens.on");
        take_ctl_file();
        if (exe_file_exists("netplay_pad.on")) load_pad_script();
        if (exe_file_take("netplay_shot.on", tag, sizeof tag)) {
            extern char g_doa2u_shot_path[MAX_PATH];
            SYSTEMTIME t; GetLocalTime(&t);
            _snprintf(g_doa2u_shot_path, MAX_PATH - 1, "%s\\shot_%02u%02u%02u_%s.bmp", netplay_diag_dump_dir(),
                      t.wHour, t.wMinute, t.wSecond, tag[0] ? tag : "untagged");
            g_doa2u_shot_path[MAX_PATH - 1] = 0;
        }
        if (exe_file_take("netplay_dump.on", tag, sizeof tag)) netplay_diag_request_dump(tag);
        if (exe_file_take("netplay_dump_at.on", tag, sizeof tag)) {
            char *sp = strchr(tag, ' ');
            if (sp) *sp = 0;
            dump_at_list(tag, sp ? sp + 1 : "");
        }
    }
    if (s_dump_at_n) {                                      /* boot-call dumps ("cN") need no session */
        int i;
        for (i = 0; i < s_dump_at_n; i++)
            if (s_dump_at[i] != NP_NO_FRAME && !(s_dump_at[i] & DUMP_AT_FIGHT) && (s_dump_at[i] & DUMP_AT_CALL) &&
                (s_dump_at[i] & 0x3FFFFFFFu) == s_builder_calls) {
                char t[NP_DUMP_TAG + 1];
                _snprintf(t, sizeof t - 1, "%.20s_c%u", s_dump_at_tag, s_builder_calls);
                t[NP_DUMP_TAG] = 0;
                netplay_diag_request_dump(t);
                s_dump_at[i] = NP_NO_FRAME;
            }
    }
    if (s_dump_at_n && netplay_active()) {
        int i;
        uint32_t now = netplay_session_frame();
        if (s_fight_frame == NP_NO_FRAME && np_game_in_fight()) {
            s_fight_frame = now;
            if (dump_at_wants_prev() && s_prev_ram && s_prev_frame == now - 1) {
                char t[NP_DUMP_TAG + 1];
                _snprintf(t, sizeof t - 1, "%.20s_fm1", s_dump_at_tag);
                t[NP_DUMP_TAG] = 0;
                write_dump_from(s_prev_ram, s_prev_frame, t);
            }
            for (i = 0; i < s_dump_at_n; i++)
                if (s_dump_at[i] == (DUMP_AT_FIGHT | 0u)) s_dump_at[i] = NP_NO_FRAME;
        }
        for (i = 0; i < s_dump_at_n; i++) {
            uint32_t want = s_dump_at[i];
            if (want == NP_NO_FRAME) continue;
            if (want & DUMP_AT_FIGHT) {
                if (s_fight_frame == NP_NO_FRAME) continue;
                want = s_fight_frame + (want & 0x7FFFFFFFu) - 1;
            }
            if (want == now) {
                char t[NP_DUMP_TAG + 1];
                _snprintf(t, sizeof t - 1, "%.20s_%u", s_dump_at_tag, now);
                t[NP_DUMP_TAG] = 0;
                netplay_diag_request_dump(t);
                s_dump_at[i] = NP_NO_FRAME;
            }
        }
        if (s_fight_frame == NP_NO_FRAME && dump_at_wants_prev()) {   /* keep last frame's RAM */
            if (!s_prev_ram) s_prev_ram = (uint8_t *)malloc(XBOX_LOW_END);
            if (s_prev_ram) { copy_ram(s_prev_ram); s_prev_frame = now; }
        }
    }
    if (s_dump_req) {
        InterlockedExchange(&s_dump_req, 0);
        write_dump();
    }
    play_pad_script();                                      /* before the mirror copies pad 0 */
    if (s_mirror) {                                         /* same rule as netplay_session.c mirror_pair() */
        np_pad m, none0 = {0};
        int owner = np_game_pause_owner();
        np_game_pad_read(0, &m);
        if (s_p2_neutral) memset(&m, 0, sizeof m);          /* keeps the port open, nothing pressed (no session: removing
                                                             * the mirror would unplug pad 2 and the game stops on its prompt) */
        if (owner == 0) np_game_pad_write(1, &none0, 0x1000u + s_builder_calls);
        else if (owner == 1) { np_game_pad_write(1, &m, 0x1000u + s_builder_calls); np_game_pad_write(0, &none0, 0x1000u + s_builder_calls); }
        else np_game_pad_write(1, &m, 0x1000u + s_builder_calls);
    }
    rate_log();
    dirty_tick();
    if ((s_builder_calls % 600) == 0 && online_get_diag() && netplay_active()) {
        extern void doa2u_rand_callers_dump(uint32_t);
        doa2u_rand_callers_dump(netplay_session_frame());
    }
    if (s_screens) {
        static uint32_t last = 0xFFFFFFFFu;
        uint32_t key = np_game_screen_key();
        if (key != last) {
            last = key;
            fprintf(stderr, "[netplay] screen %04X charsel=%d posed=%d tag=%d fight=%d (pad-builder call %u)\n",
                    key, np_game_at_charsel(), np_game_previews_posed(), np_game_is_tag(),
                    np_game_in_fight(), s_builder_calls);
            fflush(stderr);
        }
    }
}

uint32_t netplay_diag_forced_pads(void)
{
    return s_mirror ? 0x3u : 0u;
}
