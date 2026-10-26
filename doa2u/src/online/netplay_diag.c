/* netplay_diag.c - see netplay_diag.h. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "netplay_diag.h"
#include "netplay_dump.h"
#include "netplay_game.h"
#include "online_settings.h"
#include "game/recomp/recomp_types.h"
#include "kernel/xbox_memory_layout.h"

float d3d8_GetGameFPS(void);   /* d3d/d3d8_device.c */

static uint32_t      s_builder_calls;
static int           s_mirror;
static volatile LONG s_dump_req;
static char          s_dump_tag[NP_DUMP_TAG + 1];
static char          s_last_dump[MAX_PATH];

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

static void write_dump(void)
{
    static const uint8_t zero[NP_PAGE];
    char path[MAX_PATH];
    SYSTEMTIME t;
    np_dump_info info;
    FILE *f;
    uint32_t va;
    GetLocalTime(&t);
    _snprintf(path, sizeof path - 1, "%s\\dump_%02u%02u%02u_%s.bin", netplay_diag_dump_dir(),
              t.wHour, t.wMinute, t.wSecond, s_dump_tag[0] ? s_dump_tag : "untagged");
    path[sizeof path - 1] = 0;
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[netplay] cannot write %s\n", path); return; }
    memset(&info, 0, sizeof info);
    info.lo = 0;
    info.hi = XBOX_LOW_END;
    info.frame = 0xFFFFFFFFu;
    info.builder_calls = s_builder_calls;
    strncpy(info.tag, s_dump_tag, NP_DUMP_TAG);
    np_dump_write_header(f, &info);
    for (va = info.lo; va < info.hi; va += NP_PAGE) {
        const void *host = (const void *)XBOX_PTR(va);
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(host, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            fwrite(host, 1, NP_PAGE, f);
        else
            fwrite(zero, 1, NP_PAGE, f);                    /* not committed: reads as zero */
    }
    fclose(f);
    strncpy(s_last_dump, path, sizeof s_last_dump - 1);
    fprintf(stderr, "[netplay] wrote %s (pad-builder call %u)\n", path, s_builder_calls);
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
        if (exe_file_take("netplay_dump.on", tag, sizeof tag)) netplay_diag_request_dump(tag);
    }
    if (s_dump_req) {
        InterlockedExchange(&s_dump_req, 0);
        write_dump();
    }
    if (s_mirror) {
        np_pad m;
        np_game_pad_read(0, &m);
        np_game_pad_write(1, &m, 0x1000u + s_builder_calls);
    }
    rate_log();
}

uint32_t netplay_diag_forced_pads(void)
{
    return s_mirror ? 0x3u : 0u;
}
