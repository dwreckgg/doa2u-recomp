/* See netplay_files.h. */
#include "netplay_files.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#define FP_SAMPLE  (64u * 1024u)
#define FP_STRIDE  (16ull * 1024u * 1024u)
#define FNV64_INIT 1469598103934665603ull
#define FNV64_MUL  1099511628211ull

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= FNV64_MUL; }
    return h;
}

/* Left out: the files the PC writes into the game folder itself. */
static int skipped(const wchar_t *name)
{
    return _wcsnicmp(name, L"doa2u_", 6) == 0 || _wcsicmp(name, L"desktop.ini") == 0 ||
           _wcsicmp(name, L"thumbs.db") == 0;
}

/* The sampled content of one file, with its size. */
static uint64_t file_hash(const wchar_t *path, uint64_t *size_out)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    LARGE_INTEGER size;
    unsigned char *buf;
    uint64_t hash = FNV64_INIT, at;
    *size_out = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h, &size)) { CloseHandle(h); return 0; }
    *size_out = (uint64_t)size.QuadPart;
    buf = (unsigned char *)malloc(FP_SAMPLE);
    if (!buf) { CloseHandle(h); return 0; }
    for (at = 0;; at += FP_STRIDE) {
        uint64_t off = at;
        int last = 0;
        if (off + FP_SAMPLE >= *size_out) {                  /* the end: the last 64 KB */
            off = *size_out > FP_SAMPLE ? *size_out - FP_SAMPLE : 0;
            last = 1;
        }
        {
            LARGE_INTEGER pos;
            DWORD got = 0;
            pos.QuadPart = (LONGLONG)off;
            if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN) ||
                !ReadFile(h, buf, (DWORD)(*size_out - off < FP_SAMPLE ? *size_out - off : FP_SAMPLE), &got, NULL))
                break;
            hash = fnv(hash, &off, sizeof off);
            hash = fnv(hash, buf, got);
        }
        if (last) break;
    }
    free(buf);
    CloseHandle(h);
    return hash;
}

typedef struct { wchar_t name[MAX_PATH]; int dir; } entry;

static int by_name(const void *a, const void *b)
{
    return _wcsicmp(((const entry *)a)->name, ((const entry *)b)->name);
}

/* Walks `dir` in name order (any case), so the result does not depend on the order
 * the files were made in; `rel` is the path so far, lower case with '/'. */
static uint64_t walk(uint64_t fp, const wchar_t *dir, const char *rel, FILE *listing, int depth)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    entry *e = NULL;
    size_t n = 0, cap = 0, i;
    if (depth > 16) return fp;
    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return fp;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || skipped(fd.cFileName)) continue;
        if (n == cap) {
            entry *ne = (entry *)realloc(e, (cap = cap ? cap * 2 : 64) * sizeof *e);
            if (!ne) break;
            e = ne;
        }
        wcscpy_s(e[n].name, MAX_PATH, fd.cFileName);
        e[n].dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        n++;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    if (n) qsort(e, n, sizeof *e, by_name);
    for (i = 0; i < n; i++) {
        wchar_t full[MAX_PATH];
        char path[MAX_PATH * 2];
        size_t k;
        int len;
        swprintf_s(full, MAX_PATH, L"%s\\%s", dir, e[i].name);
        len = snprintf(path, sizeof path, "%s%s", rel, rel[0] ? "/" : "");
        if (len < 0 || len >= (int)sizeof path) continue;
        k = (size_t)len;
        WideCharToMultiByte(CP_UTF8, 0, e[i].name, -1, path + k, (int)(sizeof path - k), NULL, NULL);
        for (len = 0; path[len]; len++)
            if (path[len] >= 'A' && path[len] <= 'Z') path[len] = (char)(path[len] - 'A' + 'a');
        if (e[i].dir) {
            fp = walk(fp, full, path, listing, depth + 1);
        } else {
            uint64_t size, h = file_hash(full, &size);
            fp = fnv(fp, path, strlen(path) + 1);
            fp = fnv(fp, &size, sizeof size);
            fp = fnv(fp, &h, sizeof h);
            if (listing) fprintf(listing, "%s %llu %016llX\n", path, (unsigned long long)size, (unsigned long long)h);
        }
    }
    free(e);
    return fp;
}

uint64_t np_files_fingerprint(const wchar_t *dir, FILE *listing)
{
    uint64_t fp;
    if (!dir || !dir[0]) return 0;
    fp = walk(FNV64_INIT, dir, "", listing, 0);
    return fp == FNV64_INIT ? 0 : fp;
}
