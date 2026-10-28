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

    /* nothing there: 0 */
    CHECK(np_files_fingerprint(L"Z:\\no\\such\\folder\\anywhere", NULL) == 0);

    wipe(a);
    wipe(b);
    printf("netplay_files_test: %d failure(s)\n", failures);
    return failures;
}
