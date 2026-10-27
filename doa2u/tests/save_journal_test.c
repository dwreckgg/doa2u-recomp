/* Host test for the save journal (src/kernel/save_journal.c), in a temporary folder.
 * A "crash" is a run that stops without its handles closing: the next start's
 * recovery must put every file back as it was before that save began. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "kernel/save_journal.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static WCHAR root[MAX_PATH];

static void path(WCHAR *out, const WCHAR *rel) { swprintf_s(out, MAX_PATH, L"%s\\%s", root, rel); }

static void put(const WCHAR *rel, const char *text)
{
    WCHAR p[MAX_PATH]; HANDLE h; DWORD n;
    path(p, rel);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(h, text, (DWORD)strlen(text), &n, NULL);
    CloseHandle(h);
}

static int is(const WCHAR *rel, const char *text)
{
    WCHAR p[MAX_PATH]; char buf[256]; DWORD n = 0; HANDLE h;
    path(p, rel);
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return text == NULL;
    ReadFile(h, buf, sizeof buf - 1, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    return text && strcmp(buf, text) == 0;
}

/* Open for writing through the journal, as the kernel's NtCreateFile does. */
static HANDLE open_w(const WCHAR *rel, DWORD disposition)
{
    WCHAR p[MAX_PATH]; HANDLE h;
    path(p, rel);
    sj_before_open_for_write(p);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    sj_track(h, p);
    return h;
}

static void write_s(HANDLE h, const char *text) { DWORD n; WriteFile(h, text, (DWORD)strlen(text), &n, NULL); }

static int leftovers(void)
{
    WCHAR pat[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE f; int n = 0;
    swprintf_s(pat, MAX_PATH, L"%s\\UserData\\*", root);
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do { if (wcsstr(fd.cFileName, L".sjundo") || wcsstr(fd.cFileName, L".sjnew")) n++; } while (FindNextFileW(f, &fd));
    FindClose(f);
    swprintf_s(pat, MAX_PATH, L"%s\\.sjcommit", root);
    if (GetFileAttributesW(pat) != INVALID_FILE_ATTRIBUTES) n++;
    return n;
}

int main(void)
{
    WCHAR tmp[MAX_PATH], p[MAX_PATH];
    HANDLE a, b;
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(root, MAX_PATH, L"%sdoa2u_sj_test_%lu", tmp, GetCurrentProcessId());
    CreateDirectoryW(root, NULL);
    path(p, L"UserData"); CreateDirectoryW(p, NULL);
    path(p, L"Cache"); CreateDirectoryW(p, NULL);

    /* 1. A finished save: new contents stay, no journal left. */
    sj_init(root);
    put(L"UserData\\ups.dat", "old profile");
    a = open_w(L"UserData\\ups.dat", OPEN_EXISTING);
    write_s(a, "new profile");
    CHECK(sj_close(a));
    CHECK(is(L"UserData\\ups.dat", "new profile"));
    CHECK(leftovers() == 0);

    /* 2. A crash mid-save across two files, one truncated at open and one new:
     *    recovery restores the old one and removes the partial new one. */
    put(L"UserData\\ups.dat", "old profile");
    a = open_w(L"UserData\\ups.dat", CREATE_ALWAYS);       /* truncates at open */
    write_s(a, "half");
    b = open_w(L"UserData\\meta.xbx", CREATE_NEW);
    write_s(b, "partial");
    CloseHandle(a); CloseHandle(b);                         /* the process dies: no sj_close */
    sj_forget_for_test();
    sj_init(root);                                          /* next start */
    CHECK(is(L"UserData\\ups.dat", "old profile"));
    CHECK(is(L"UserData\\meta.xbx", NULL));
    CHECK(leftovers() == 0);

    /* 3. One file closed, the other still open when the crash comes: the whole
     *    save rolls back, so the two files never mix old and new. */
    put(L"UserData\\ups.dat", "old profile");
    put(L"UserData\\meta.xbx", "old meta");
    a = open_w(L"UserData\\ups.dat", OPEN_EXISTING);
    b = open_w(L"UserData\\meta.xbx", OPEN_EXISTING);
    write_s(a, "new profile");
    CHECK(!sj_close(a));                                    /* not committed yet: b is open */
    write_s(b, "new meta");
    CloseHandle(b);                                         /* dies before closing b through the journal */
    sj_forget_for_test();
    sj_init(root);
    CHECK(is(L"UserData\\ups.dat", "old profile"));
    CHECK(is(L"UserData\\meta.xbx", "old meta"));

    /* 4. A crash after the commit marker but before the journal was cleared:
     *    the save had finished, so the new contents stay. */
    put(L"UserData\\ups.dat", "new profile");
    put(L"UserData\\ups.dat.sjundo", "old profile");
    put(L".sjcommit", "");
    sj_forget_for_test();
    sj_init(root);
    CHECK(is(L"UserData\\ups.dat", "new profile"));
    CHECK(leftovers() == 0);

    /* 5. Files outside the save partitions (the cache) are not journaled. */
    a = open_w(L"Cache\\voice.afs", CREATE_ALWAYS);
    write_s(a, "cache");
    CloseHandle(a);
    sj_forget_for_test();
    path(p, L"Cache\\voice.afs.sjnew");
    CHECK(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES);

    /* 6. A handle the journal never saw closes as not-a-commit. */
    CHECK(!sj_close(INVALID_HANDLE_VALUE));

    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
