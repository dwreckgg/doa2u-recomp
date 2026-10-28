/* netplay_files.c: the game-data fingerprint two players compare before a session. */
#include "online/netplay_files.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void put(const wchar_t *root, const wchar_t *rel, size_t size, unsigned seed)
{
    wchar_t p[MAX_PATH];
    unsigned char *b = (unsigned char *)malloc(size ? size : 1);
    size_t i;
    FILE *f;
    for (i = 0; i < size; i++) b[i] = (unsigned char)(i * 31u + seed);
    swprintf_s(p, MAX_PATH, L"%s\\%s", root, rel);
    f = _wfopen(p, L"wb");
    if (f) { fwrite(b, 1, size, f); fclose(f); }
    free(b);
}

static void poke(const wchar_t *root, const wchar_t *rel, long at, unsigned char v)
{
    wchar_t p[MAX_PATH];
    FILE *f;
    swprintf_s(p, MAX_PATH, L"%s\\%s", root, rel);
    f = _wfopen(p, L"r+b");
    if (f) { fseek(f, at, SEEK_SET); fputc(v, f); fclose(f); }
}

/* A small XBE: header with a certificate (title ID, allowed media, version) and two
 * sections of 4 KB; `sig` fills the signature, `code` changes one section byte. */
static void put_xbe(const wchar_t *root, uint32_t media, unsigned sig, uint32_t version, int code)
{
    unsigned char x[0x3000];
    wchar_t p[MAX_PATH];
    uint32_t base = 0x10000, v;
    int i;
    FILE *f;
    memset(x, 0, sizeof x);
    memcpy(x, "XBEH", 4);
    for (i = 4; i < 0x104; i++) x[i] = (unsigned char)(i * 7u + sig);
    v = base;          memcpy(x + 0x104, &v, 4);
    v = 0x1000;        memcpy(x + 0x108, &v, 4);   /* header size */
    v = 0x3000;        memcpy(x + 0x10C, &v, 4);
    v = base + 0x200;  memcpy(x + 0x118, &v, 4);   /* certificate */
    v = 2;             memcpy(x + 0x11C, &v, 4);   /* sections */
    v = base + 0x300;  memcpy(x + 0x120, &v, 4);   /* section headers */
    v = 0x54430006u;   memcpy(x + 0x200 + 0x08, &v, 4);
    memcpy(x + 0x200 + 0x9C, &media, 4);
    memcpy(x + 0x200 + 0xAC, &version, 4);
    for (i = 0; i < 2; i++) {
        unsigned char *h = x + 0x300 + 56 * i;
        v = 0x11000u + 0x1000u * (uint32_t)i; memcpy(h + 0x04, &v, 4);   /* virtual address */
        v = 0x1000;                           memcpy(h + 0x08, &v, 4);
        v = 0x1000u + 0x1000u * (uint32_t)i;  memcpy(h + 0x0C, &v, 4);   /* raw address */
        v = 0x1000;                           memcpy(h + 0x10, &v, 4);   /* raw size */
    }
    for (i = 0x1000; i < 0x3000; i++) x[i] = (unsigned char)(i * 13u);
    if (code) x[0x2345] ^= 0x5A;
    swprintf_s(p, MAX_PATH, L"%s\\doa2.xbe", root);
    f = _wfopen(p, L"wb");
    if (f) { fwrite(x, 1, sizeof x, f); fclose(f); }
}

static void tree(const wchar_t *root, int reverse)
{
    wchar_t p[MAX_PATH];
    CreateDirectoryW(root, NULL);
    swprintf_s(p, MAX_PATH, L"%s\\DOA2", root); CreateDirectoryW(p, NULL);
    if (!reverse) {
        put(root, L"default.xbe", 5000, 1);
        put(root, L"DOA2\\datahdd.afs", 300 * 1024, 2);
        put(root, L"DOA2\\voice.afs", 20 * 1024 * 1024, 3);    /* spans a 16 MB sample */
    } else {
        put(root, L"DOA2\\voice.afs", 20 * 1024 * 1024, 3);
        put(root, L"DOA2\\datahdd.afs", 300 * 1024, 2);
        put(root, L"default.xbe", 5000, 1);
    }
    put(root, L"doa2u_shader_cache.bin", 777, (unsigned)reverse * 9u);   /* made by the PC: never counted */
}

static void wipe(const wchar_t *root)
{
    wchar_t p[MAX_PATH];
    static const wchar_t *files[] = { L"default.xbe", L"DOA2\\datahdd.afs", L"DOA2\\voice.afs", L"DOA2\\VOICE2.afs",
                                      L"doa2u_shader_cache.bin", NULL };
    int i;
    for (i = 0; files[i]; i++) { swprintf_s(p, MAX_PATH, L"%s\\%s", root, files[i]); DeleteFileW(p); }
    swprintf_s(p, MAX_PATH, L"%s\\DOA2", root); RemoveDirectoryW(p);
    RemoveDirectoryW(root);
}

int main(void)
{
    wchar_t tmp[MAX_PATH], a[MAX_PATH], b[MAX_PATH], p[MAX_PATH], q[MAX_PATH];
    uint64_t fa, fb, f;
    char line[512];
    FILE *lst;
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(a, MAX_PATH, L"%sdoa2u_fp_a_%lu", tmp, GetCurrentProcessId());
    swprintf_s(b, MAX_PATH, L"%sdoa2u_fp_b_%lu", tmp, GetCurrentProcessId());
    tree(a, 0);
    tree(b, 1);

    /* the same files elsewhere, made in another order (and a different shader cache): same */
    fa = np_files_fingerprint(a, NULL);
    fb = np_files_fingerprint(b, NULL);
    CHECK(fa != 0 && fa == fb);

    /* a changed byte the samples cover: different (first 64 KB, last 64 KB, every 16 MB) */
    poke(b, L"DOA2\\datahdd.afs", 10, 0xEE);
    CHECK(np_files_fingerprint(b, NULL) != fa);
    poke(b, L"DOA2\\datahdd.afs", 10, (unsigned char)(10 * 31u + 2));
    CHECK(np_files_fingerprint(b, NULL) == fa);
    poke(b, L"DOA2\\voice.afs", 16 * 1024 * 1024 + 5, 0x11);
    CHECK(np_files_fingerprint(b, NULL) != fa);
    poke(b, L"DOA2\\voice.afs", 16 * 1024 * 1024 + 5, (unsigned char)((16u * 1024 * 1024 + 5) * 31u + 3));
    CHECK(np_files_fingerprint(b, NULL) == fa);
    poke(b, L"DOA2\\voice.afs", 20 * 1024 * 1024 - 3, 0x22);                 /* the last 64 KB */
    CHECK(np_files_fingerprint(b, NULL) != fa);
    poke(b, L"DOA2\\voice.afs", 20 * 1024 * 1024 - 3, (unsigned char)((20u * 1024 * 1024 - 3) * 31u + 3));

    /* a change outside the samples is not seen (the fingerprint reads samples, not whole files) */
    poke(b, L"DOA2\\datahdd.afs", 100 * 1024, 0x33);
    CHECK(np_files_fingerprint(b, NULL) == fa);
    poke(b, L"DOA2\\datahdd.afs", 100 * 1024, (unsigned char)(100u * 1024 * 31u + 2));

    /* another size: different */
    put(b, L"default.xbe", 5001, 1);
    CHECK(np_files_fingerprint(b, NULL) != fa);
    put(b, L"default.xbe", 5000, 1);
    CHECK(np_files_fingerprint(b, NULL) == fa);

    /* a renamed file: different; another letter case only: same (Windows paths ignore case) */
    swprintf_s(p, MAX_PATH, L"%s\\DOA2\\voice.afs", b);
    swprintf_s(q, MAX_PATH, L"%s\\DOA2\\VOICE2.afs", b);
    MoveFileW(p, q);
    CHECK(np_files_fingerprint(b, NULL) != fa);
    MoveFileW(q, p);
    swprintf_s(q, MAX_PATH, L"%s\\DOA2\\VOICE.AFS", b);
    MoveFileW(p, q);
    CHECK(np_files_fingerprint(b, NULL) == fa);

    /* the listing: one line per counted file, the PC's own files left out */
    swprintf_s(p, MAX_PATH, L"%s\\listing.txt", tmp);
    lst = _wfopen(p, L"w+");
    CHECK(lst != NULL);
    if (lst) {
        int n = 0, cache = 0, voice = 0;
        f = np_files_fingerprint(a, lst);
        CHECK(f == fa);
        rewind(lst);
        while (fgets(line, sizeof line, lst)) {
            n++;
            if (strstr(line, "shader_cache")) cache = 1;
            if (strstr(line, "doa2/voice.afs 20971520 ")) voice = 1;
        }
        fclose(lst);
        _wremove(p);
        CHECK(n == 3 && !cache && voice);
    }

    /* .xbe files: their code and data sections, title ID and version count; the header
     * fields extraction tools patch (allowed media) and the signature do not */
    {
        wchar_t c[MAX_PATH];
        uint64_t x0;
        swprintf_s(c, MAX_PATH, L"%sdoa2u_fp_x_%lu", tmp, GetCurrentProcessId());
        CreateDirectoryW(c, NULL);
        put_xbe(c, 0x00000002u, 1, 6, 0);
        x0 = np_files_fingerprint(c, NULL);
        put_xbe(c, 0x400001FFu, 2, 6, 0);                 /* media-patched, other signature */
        CHECK(np_files_fingerprint(c, NULL) == x0);
        put_xbe(c, 0x00000002u, 1, 6, 1);                 /* a byte of its code differs */
        CHECK(np_files_fingerprint(c, NULL) != x0);
        put_xbe(c, 0x00000002u, 1, 7, 0);                 /* another version */
        CHECK(np_files_fingerprint(c, NULL) != x0);
        put(c, L"doa2.xbe", 3000, 4);                      /* not an XBE inside: counted like any file */
        CHECK(np_files_fingerprint(c, NULL) != 0);
        /* the dashboard updaters (never run by the game; many dumps blank them): not counted */
        x0 = np_files_fingerprint(c, NULL);
        put(c, L"dashupdate.xbe", 5000, 1);
        put(c, L"update.xbe", 7000, 2);
        CHECK(np_files_fingerprint(c, NULL) == x0);
        put(c, L"DashUpdate.xbe", 5001, 9);
        CHECK(np_files_fingerprint(c, NULL) == x0);
        swprintf_s(p, MAX_PATH, L"%s\\dashupdate.xbe", c); DeleteFileW(p);
        swprintf_s(p, MAX_PATH, L"%s\\update.xbe", c); DeleteFileW(p);
        swprintf_s(p, MAX_PATH, L"%s\\doa2.xbe", c); DeleteFileW(p);
        RemoveDirectoryW(c);
    }

    /* nothing there: 0 */
    CHECK(np_files_fingerprint(L"Z:\\no\\such\\folder\\anywhere", NULL) == 0);

    wipe(a);
    wipe(b);
    printf("netplay_files_test: %d failure(s)\n", failures);
    return failures;
}
