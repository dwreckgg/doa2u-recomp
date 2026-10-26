/*
 * xiso_extract.c - first-run asset install from a Dead or Alive 2 Ultimate disc image.
 * Reads XDVDFS from an .xiso or full .iso, extracts to <assets>.extracting, then moves it in.
 */

#include "xiso_extract.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define XDVDFS_SECTOR      2048u
#define XDVDFS_HEADER_SECT 32u
#define XDVDFS_MAGIC       "MICROSOFT*XBOX*MEDIA"
#define XDVDFS_ATTR_DIR    0x10

/* Game partition offsets: xiso, XGD1, XGD2, XGD3. */
static const uint64_t k_partitions[] = { 0, 0x18300000ull, 0xFD90000ull, 0x2080000ull };

/* The release this port was recompiled from: Dead or Alive 2 Ultimate (USA, Asia) (En,Ja).
 * Its DOA2.xbe must match exactly; any other region or version cannot run. */
#define DOA2U_TITLE_ID   0x54430006u
#define DOA2U_REGION     0x00000003u   /* North America | Japan */
#define DOA2U_VERSION    6u
#define DOA2U_XBE_NAME   "DOA2.xbe"
#define DOA2U_XBE_SIZE   8228864u
#define DOA2U_XBE_FNV64  0xC41B3B1159968AC9ull

/* All Dead or Alive Ultimate discs share title ID 54430006; the XBE version
 * tells them apart (xemu xdb / Redump: TC00606K = USA, Asia, TC00610E = Europe). */
static const struct { uint32_t version; int disc; const char *release; } k_releases[] = {
    {  5, 1, "USA, Asia" },
    {  6, 2, "USA, Asia" },
    {  9, 1, "Europe"    },
    { 10, 2, "Europe"    },
};

/* Every disc file outside DOA1/ (extracted, but never run by the port), with its size. */
static const struct { const char *name; uint32_t size; } k_required[] = {
    { "DOA2.xbe",              8228864u }, { "default.xbe",              65536u },
    { "Doa1.xbe",              4354048u }, { "dashupdate.xbe",        58421248u },
    { "update.xbe",            2326528u }, { "xtalkamp.bin",             23084u },
    { "doa2.txt",                    4u }, { "doau.xbx",                 10240u },
    { "doau20001.sim",            4096u }, { "doau20002.sim",             4096u },
    { "DOA2/2_VI_WIDE_B.sfd",  36182016u }, { "DOA2/4_VI_STD_B.sfd",   36595712u },
    { "DOA2/bgm.afs",         335177728u }, { "DOA2/voice.afs",        21843968u },
    { "DOA2/cg01.afs",        268677120u }, { "DOA2/cg02.afs",        272769024u },
    { "DOA2/cg03.afs",        306339840u }, { "DOA2/cg04.afs",        297947136u },
    { "DOA2/cg05.afs",        297947136u }, { "DOA2/cg06.afs",         79734784u },
    { "DOA2/datadvd.afs",     381280256u }, { "DOA2/datahdd.afs",     267153408u },
    { "DOA2/datahdd2.afs",    415268864u }, { "DOA2/doa2_op.sfd",     553394176u },
    { "DOA2/DOA_G4.sfd",      482758656u }, { "DOA2/ninja.aix",         3102720u },
    { "Media/ArialUni.Ttf",    23274572u }, { "Media/dlcustom.xpr",     1574912u },
    { "Media/Fonts/Font.xpr",     69632u }, { "Media/Sound.xsb",            684u },
    { "Media/Wave.xwb",          190012u },
};
#define N_REQUIRED (sizeof k_required / sizeof k_required[0])

typedef struct {
    WCHAR    rel[MAX_PATH];   /* path relative to the image root */
    char     name[64];        /* leaf name, for display */
    int      is_dir;
    uint32_t sector;
    uint32_t size;
} XisoEntry;

typedef struct {
    HANDLE     h;
    uint64_t   image_size;
    uint64_t   part;          /* game partition offset */
    XisoEntry *e;
    int        n, cap;
    char      *err;
    size_t     errlen;
} Xiso;

typedef struct {
    uint32_t title_id, region, version, size;
    uint64_t fnv;
    char     title[128];
} XbeInfo;

static int fail(Xiso *x, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(x->err, x->errlen, fmt, ap);
    va_end(ap);
    return 0;
}

static int read_at(HANDLE h, uint64_t off, void *buf, DWORD len)
{
    OVERLAPPED ov;
    DWORD got = 0;
    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    return ReadFile(h, buf, len, &got, &ov) && got == len;
}

/* Table path ("DOA2/bgm.afs") as a Windows relative path. */
static void required_path(size_t i, WCHAR *out)
{
    MultiByteToWideChar(CP_ACP, 0, k_required[i].name, -1, out, MAX_PATH);
    for (WCHAR *c = out; *c; c++)
        if (*c == L'/') *c = L'\\';
}

static int find_partition(Xiso *x)
{
    static char sect[XDVDFS_SECTOR];
    for (size_t i = 0; i < sizeof k_partitions / sizeof k_partitions[0]; i++) {
        uint64_t off = k_partitions[i] + (uint64_t)XDVDFS_HEADER_SECT * XDVDFS_SECTOR;
        if (off + XDVDFS_SECTOR > x->image_size) continue;
        if (!read_at(x->h, off, sect, XDVDFS_SECTOR)) continue;
        if (memcmp(sect, XDVDFS_MAGIC, 20) || memcmp(sect + 0x7EC, XDVDFS_MAGIC, 20))
            continue;
        x->part = k_partitions[i];
        return 1;
    }
    return 0;
}

static int name_ok(const char *s, int len)
{
    if (len <= 0) return 0;
    if ((len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c >= 0x7F || strchr("\\/:*?\"<>|", c)) return 0;
    }
    return 1;
}

static int walk_dir(Xiso *x, uint32_t sector, uint32_t size, const WCHAR *prefix, int depth);

/* In-order walk of one directory's tree; `budget` bounds the node count
 * so a corrupt image with a cycle in it cannot loop forever. */
static int walk_node(Xiso *x, const uint8_t *tab, uint32_t tsize, uint32_t off,
                     const WCHAR *prefix, int depth, int *budget)
{
    uint16_t left, right;
    uint32_t sector, size;
    uint8_t attr, nlen;
    XisoEntry *e;
    WCHAR wname[64];
    size_t pos = (size_t)off * 4;

    if (--*budget < 0) return fail(x, "The disc image's file table is damaged (loop in directory).");
    if (pos + 14 > tsize) return fail(x, "The disc image's file table is damaged (entry out of range).");
    memcpy(&left, tab + pos, 2);
    memcpy(&right, tab + pos + 2, 2);
    memcpy(&sector, tab + pos + 4, 4);
    memcpy(&size, tab + pos + 8, 4);
    attr = tab[pos + 12];
    nlen = tab[pos + 13];
    if (left == 0xFFFF) return fail(x, "The disc image's file table is damaged (padding entry).");
    if (pos + 14 + nlen > tsize || !name_ok((const char *)tab + pos + 14, nlen))
        return fail(x, "The disc image's file table is damaged (bad file name).");

    if (left && !walk_node(x, tab, tsize, left, prefix, depth, budget)) return 0;

    if (x->n == x->cap) {
        int cap = x->cap ? x->cap * 2 : 64;
        XisoEntry *ne = (XisoEntry *)realloc(x->e, (size_t)cap * sizeof *ne);
        if (!ne) return fail(x, "Out of memory reading the disc image.");
        x->e = ne; x->cap = cap;
    }
    e = &x->e[x->n++];
    memset(e, 0, sizeof *e);
    memcpy(e->name, tab + pos + 14, nlen < 63 ? nlen : 63);
    MultiByteToWideChar(CP_ACP, 0, e->name, -1, wname, 64);
    if (swprintf_s(e->rel, MAX_PATH, prefix[0] ? L"%s\\%s" : L"%s%s", prefix, wname) < 0)
        return fail(x, "A path on the disc image is too long.");
    e->is_dir = (attr & XDVDFS_ATTR_DIR) != 0;
    e->sector = sector;
    e->size = size;
    if (!e->is_dir &&
        x->part + (uint64_t)sector * XDVDFS_SECTOR + size > x->image_size)
        return fail(x, "The disc image is incomplete: %s runs past the end of the file. "
                       "The download or dump was probably cut short.", e->name);

    if (e->is_dir) {
        WCHAR sub[MAX_PATH];
        wcscpy_s(sub, MAX_PATH, e->rel);
        if (!walk_dir(x, sector, size, sub, depth + 1)) return 0;
    }

    if (right && !walk_node(x, tab, tsize, right, prefix, depth, budget)) return 0;
    return 1;
}

static int walk_dir(Xiso *x, uint32_t sector, uint32_t size, const WCHAR *prefix, int depth)
{
    uint8_t *tab;
    int ok, budget;
    uint64_t off = x->part + (uint64_t)sector * XDVDFS_SECTOR;

    if (depth > 32) return fail(x, "The disc image's file table is damaged (too deep).");
    if (size == 0) return 1;                                  /* empty directory */
    if (size > 16u * 1024 * 1024 || off + size > x->image_size)
        return fail(x, "The disc image's file table is damaged (directory out of range).");
    tab = (uint8_t *)malloc(size);
    if (!tab) return fail(x, "Out of memory reading the disc image.");
    if (!read_at(x->h, off, tab, size)) { free(tab); return fail(x, "Could not read the disc image."); }
    /* A directory whose first sector is all padding is empty. */
    if (size >= 4 && tab[0] == 0xFF && tab[1] == 0xFF && tab[2] == 0xFF && tab[3] == 0xFF) {
        free(tab);
        return 1;
    }
    budget = (int)(size / 14) + 1;
    ok = walk_node(x, tab, size, 0, prefix, depth, &budget);
    free(tab);
    return ok;
}

/* `rel` is a backslash path from the image root, matched case-insensitively. */
static const XisoEntry *find_file(const Xiso *x, const WCHAR *rel)
{
    for (int i = 0; i < x->n; i++)
        if (!x->e[i].is_dir && !_wcsicmp(x->e[i].rel, rel))
            return &x->e[i];
    return NULL;
}

/* Read an XBE's certificate (title, region, version) and hash the whole file. */
static int read_xbe_info(Xiso *x, const XisoEntry *e, XbeInfo *info)
{
    uint8_t *d;
    uint32_t base, cert;
    WCHAR wt[41];

    memset(info, 0, sizeof *info);
    if (e->size < 0x200 || e->size > 64u * 1024 * 1024)
        return fail(x, "This disc image's %s is damaged.", e->name);
    d = (uint8_t *)malloc(e->size);
    if (!d) return fail(x, "Out of memory reading the disc image.");
    if (!read_at(x->h, x->part + (uint64_t)e->sector * XDVDFS_SECTOR, d, e->size)) {
        free(d);
        return fail(x, "Could not read %s from the disc image.", e->name);
    }
    memcpy(&base, d + 0x104, 4);
    memcpy(&cert, d + 0x118, 4);
    if (memcmp(d, "XBEH", 4) || cert < base || (uint64_t)cert - base + 0xB0 > e->size) {
        free(d);
        return fail(x, "This disc image's %s is damaged.", e->name);
    }
    cert -= base;
    memcpy(&info->title_id, d + cert + 0x08, 4);
    memcpy(&info->region, d + cert + 0xA0, 4);
    memcpy(&info->version, d + cert + 0xAC, 4);
    memcpy(wt, d + cert + 0x0C, 80);
    wt[40] = 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, wt, -1, info->title, sizeof info->title, NULL, NULL) ||
        !info->title[0])
        strcpy_s(info->title, sizeof info->title, "another game");
    info->size = e->size;
    info->fnv = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < e->size; i++)
        info->fnv = (info->fnv ^ d[i]) * 0x100000001B3ull;
    free(d);
    fprintf(stderr, "[SETUP] %s: \"%s\" title %08X region %X version %u fnv %016llX\n",
            e->name, info->title, info->title_id, info->region, info->version,
            (unsigned long long)info->fnv);
    return 1;
}

/* The disc must be the USA, Asia DOA2 Ultimate disc with the exact DOA2.xbe the port
 * was built from; each mismatch names the release, disc, version and region found. */
static int check_disc(Xiso *x)
{
    const XisoEntry *xbe = find_file(x, L"" DOA2U_XBE_NAME);
    const char *release = NULL;
    int disc = 0, is_doa2;
    XbeInfo info;

    if (!xbe) xbe = find_file(x, L"default.xbe");
    if (!xbe)
        return fail(x, "This disc image has no default.xbe, so it is not an Xbox game disc.");
    if (!read_xbe_info(x, xbe, &info)) return 0;

    if (info.title_id != DOA2U_TITLE_ID)
        return fail(x, "This disc image is \"%s\", not Dead or Alive 2 Ultimate.", info.title);
    for (size_t i = 0; i < sizeof k_releases / sizeof k_releases[0]; i++)
        if (k_releases[i].version == info.version) {
            release = k_releases[i].release;
            disc = k_releases[i].disc;
        }

    is_doa2 = !_stricmp(xbe->name, DOA2U_XBE_NAME);
    if (!is_doa2 && disc == 2)
        return fail(x, "The disc image is damaged: DOA2.xbe is missing.");
    if (disc == 1 || !is_doa2) {
        if (release)
            return fail(x, "This is the Dead or Alive 1 Ultimate disc of the %s release. The "
                           "port is built from the Dead or Alive 2 Ultimate disc of the USA, Asia "
                           "release (Dead or Alive 2 Ultimate (USA, Asia) (En,Ja)) and cannot run "
                           "any other disc.", release);
        return fail(x, "This disc image is \"%s\", not Dead or Alive 2 Ultimate.", info.title);
    }
    if (release && info.version != DOA2U_VERSION)
        return fail(x, "This is the %s release of Dead or Alive 2 Ultimate. The port is built "
                       "from the USA, Asia release (Dead or Alive 2 Ultimate (USA, Asia) (En,Ja)) "
                       "and cannot run any other version.", release);
    if (info.version != DOA2U_VERSION || info.region != DOA2U_REGION ||
        info.size != DOA2U_XBE_SIZE || info.fnv != DOA2U_XBE_FNV64)
        return fail(x, "This is a different release of Dead or Alive 2 Ultimate. The port is "
                       "built from the USA, Asia release (Dead or Alive 2 Ultimate (USA, Asia) "
                       "(En,Ja)) and cannot run any other version.");
    return 1;
}

static void delete_tree(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            swprintf_s(p, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(p);
            else { SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL); DeleteFileW(p); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* Move everything under `from` into `to`, replacing same-named files.
 * Both sit next to the exe, so every move is a same-volume rename. */
static int merge_move(const WCHAR *from, const WCHAR *to)
{
    WCHAR pat[MAX_PATH], s[MAX_PATH], d[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int ok = 1;
    CreateDirectoryW(to, NULL);
    swprintf_s(pat, MAX_PATH, L"%s\\*", from);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        swprintf_s(s, MAX_PATH, L"%s\\%s", from, fd.cFileName);
        swprintf_s(d, MAX_PATH, L"%s\\%s", to, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!merge_move(s, d)) ok = 0;
        } else {
            SetFileAttributesW(d, FILE_ATTRIBUTE_NORMAL);
            if (!MoveFileExW(s, d, MOVEFILE_REPLACE_EXISTING)) ok = 0;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(from);
    return ok;
}

static int extract_file(Xiso *x, const XisoEntry *e, const WCHAR *dst,
                        uint8_t *buf, DWORD bufsize, XisoProgress *p)
{
    HANDLE out = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    uint64_t src = x->part + (uint64_t)e->sector * XDVDFS_SECTOR;
    uint32_t left = e->size;
    if (out == INVALID_HANDLE_VALUE)
        return fail(x, "Could not create %s in the assets folder (error %lu). "
                       "Check the game folder is not read-only.", e->name, GetLastError());
    while (left) {
        DWORD chunk = left < bufsize ? left : bufsize, wrote = 0;
        if (p->cancel) { CloseHandle(out); return fail(x, "Cancelled."); }
        if (!read_at(x->h, src, buf, chunk)) {
            CloseHandle(out);
            return fail(x, "Could not read %s from the disc image (error %lu).", e->name, GetLastError());
        }
        if (!WriteFile(out, buf, chunk, &wrote, NULL) || wrote != chunk) {
            DWORD le = GetLastError();
            CloseHandle(out);
            if (le == ERROR_DISK_FULL || le == ERROR_HANDLE_DISK_FULL)
                return fail(x, "The drive ran out of space while writing %s.", e->name);
            return fail(x, "Could not write %s (error %lu).", e->name, le);
        }
        src += chunk;
        left -= chunk;
        InterlockedExchangeAdd64(&p->bytes_done, chunk);
    }
    CloseHandle(out);
    return 1;
}

static int file_size_is(const WCHAR *path, uint32_t size)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return 0;
    if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return fa.nFileSizeHigh == 0 && fa.nFileSizeLow == size;
}

int xiso_assets_ready(const WCHAR *assets_dir)
{
    WCHAR p[MAX_PATH], rel[MAX_PATH];
    for (size_t i = 0; i < N_REQUIRED; i++) {
        required_path(i, rel);
        swprintf_s(p, MAX_PATH, L"%s\\%s", assets_dir, rel);
        if (!file_size_is(p, k_required[i].size)) return 0;
    }
    return 1;
}

int xiso_install(const WCHAR *image, const WCHAR *assets_dir,
                 XisoProgress *prog, char *err, size_t errlen)
{
    Xiso x;
    LARGE_INTEGER sz;
    WCHAR stage[MAX_PATH], parent[MAX_PATH], p[MAX_PATH], rel[MAX_PATH], *slash;
    ULARGE_INTEGER avail;
    uint64_t total = 0;
    uint8_t *buf = NULL;
    const DWORD bufsize = 4u * 1024 * 1024;
    int ok = 0, files = 0;

    memset(&x, 0, sizeof x);
    stage[0] = 0;
    x.err = err; x.errlen = errlen;
    err[0] = 0;

    x.h = CreateFileW(image, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (x.h == INVALID_HANDLE_VALUE) {
        fail(&x, "Could not open the selected file (error %lu).", GetLastError());
        return 0;
    }
    if (!GetFileSizeEx(x.h, &sz)) { fail(&x, "Could not read the selected file."); goto done; }
    x.image_size = (uint64_t)sz.QuadPart;

    if (!find_partition(&x)) {
        fail(&x, "This file is not an Xbox disc image. Select the Dead or Alive 2 Ultimate "
                 ".xiso / .xiso.iso (or the original .iso dump).");
        goto done;
    }
    {
        static char sect[XDVDFS_SECTOR];
        uint32_t root_sect, root_size;
        if (!read_at(x.h, x.part + (uint64_t)XDVDFS_HEADER_SECT * XDVDFS_SECTOR, sect, XDVDFS_SECTOR)) {
            fail(&x, "Could not read the disc image.");
            goto done;
        }
        memcpy(&root_sect, sect + 20, 4);
        memcpy(&root_size, sect + 24, 4);
        if (!walk_dir(&x, root_sect, root_size, L"", 0)) goto done;
    }
    if (!check_disc(&x)) goto done;
    for (size_t i = 0; i < N_REQUIRED; i++) {
        const XisoEntry *e;
        required_path(i, rel);
        e = find_file(&x, rel);
        if (!e || e->size != k_required[i].size) {
            fail(&x, "The disc image is damaged: %s is %s.", k_required[i].name,
                 e ? "the wrong size" : "missing");
            goto done;
        }
    }

    for (int i = 0; i < x.n; i++)
        if (!x.e[i].is_dir) { total += x.e[i].size; files++; }
    prog->bytes_total = (LONG64)total;
    prog->files_total = files;

    /* Stage next to the assets folder: same volume, so the final move is a rename. */
    swprintf_s(stage, MAX_PATH, L"%s.extracting", assets_dir);
    wcscpy_s(parent, MAX_PATH, assets_dir);
    slash = wcsrchr(parent, L'\\');
    if (slash) slash[1] = 0;
    if (GetDiskFreeSpaceExW(parent, &avail, NULL, NULL) &&
        avail.QuadPart < total + 64ull * 1024 * 1024) {
        fail(&x, "Not enough free space: the game files need %.1f GB and the drive "
                 "has %.1f GB free.", total / 1073741824.0, avail.QuadPart / 1073741824.0);
        goto done;
    }
    delete_tree(stage);                     /* left over from an interrupted install */
    if (!CreateDirectoryW(stage, NULL)) {
        fail(&x, "Could not create a folder next to the game (error %lu). "
                 "Move the game out of Program Files or any read-only folder.", GetLastError());
        goto done;
    }

    buf = (uint8_t *)malloc(bufsize);
    if (!buf) { fail(&x, "Out of memory."); goto done; }
    for (int i = 0; i < x.n; i++) {
        const XisoEntry *e = &x.e[i];
        swprintf_s(p, MAX_PATH, L"%s\\%s", stage, e->rel);
        if (e->is_dir) {
            if (!CreateDirectoryW(p, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                fail(&x, "Could not create folder %s (error %lu).", e->name, GetLastError());
                goto done;
            }
            continue;
        }
        strcpy_s(prog->current_name, sizeof prog->current_name, e->name);
        if (!extract_file(&x, e, p, buf, bufsize, prog)) goto done;
        InterlockedIncrement(&prog->files_done);
    }

    /* Verify what landed on disk before it replaces anything. */
    for (int i = 0; i < x.n; i++) {
        if (x.e[i].is_dir) continue;
        swprintf_s(p, MAX_PATH, L"%s\\%s", stage, x.e[i].rel);
        if (!file_size_is(p, x.e[i].size)) {
            fail(&x, "Verification failed: %s did not extract correctly.", x.e[i].name);
            goto done;
        }
    }
    if (!merge_move(stage, assets_dir)) {
        fail(&x, "Could not move the extracted files into the assets folder. "
                 "Close anything that has files in it open and try again.");
        goto done;
    }
    if (!xiso_assets_ready(assets_dir)) {
        fail(&x, "Verification failed: the assets folder is still missing game files.");
        goto done;
    }
    ok = 1;

done:
    if (!ok) fprintf(stderr, "[SETUP] install failed: %s\n", err);
    if (!ok && stage[0]) delete_tree(stage);
    free(buf);
    free(x.e);
    CloseHandle(x.h);
    prog->current_name[0] = 0;
    return ok;
}
