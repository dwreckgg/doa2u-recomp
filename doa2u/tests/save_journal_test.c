/* Host test for the save journal (src/kernel/save_journal.c), in a temporary folder.
 * A "crash" is a run that stops before the journal commits: the next start's
 * recovery must put every file back as it was before that save began. A save
 * commits once no save file has been written for SJ_QUIET_MS (sj_poll), or at
 * a clean exit (sj_commit_now). */
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

/* Open for writing through the journal, as the kernel bridge's NtCreateFile does. */
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

/* As much time later as the quiet period needs. */
static int poll_later(void) { return sj_poll(GetTickCount64() + SJ_QUIET_MS + 10); }

static int count_journal(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH], sub[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE f; int n = 0;
    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (wcsstr(fd.cFileName, L".sjundo") || wcsstr(fd.cFileName, L".sjnew") || wcsstr(fd.cFileName, L".sjtmp")) n++;
        else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            swprintf_s(sub, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
            n += count_journal(sub);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}

static int leftovers(void)
{
    WCHAR p[MAX_PATH]; int n;
    path(p, L"UserData");
    n = count_journal(p);
    path(p, L".sjcommit");
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) n++;
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

    /* 1. A finished save: not committed while the quiet period runs, then
     *    committed; new contents stay, no journal left. */
    sj_init(root);
    put(L"UserData\\ups.dat", "old profile");
    a = open_w(L"UserData\\ups.dat", OPEN_EXISTING);
    write_s(a, "new profile");
    sj_close(a);
    CHECK(!sj_poll(GetTickCount64()));                      /* just written: not quiet yet */
    CHECK(leftovers() > 0);
    CHECK(poll_later());
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
    sj_close(a);
    CHECK(!poll_later());                                   /* not committed: b is open */
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

    /* 6. The game's own save order (traced 2026-10-27): the save folder is
     *    created, then SaveMeta, SaveImage and ups.dat are each opened, written
     *    and closed in turn. A crash after the second file must roll back all
     *    of it, the new folder included. */
    sj_init(root);
    put(L"UserData\\ups.dat", "old profile");
    path(p, L"UserData\\NEWSAVE");
    sj_before_open_for_write(p);
    CHECK(CreateDirectoryW(p, NULL));
    a = open_w(L"UserData\\NEWSAVE\\SaveMeta.xbx", CREATE_ALWAYS);
    write_s(a, "meta"); sj_close(a);
    a = open_w(L"UserData\\ups.dat", CREATE_ALWAYS);
    write_s(a, "new profile"); sj_close(a);
    sj_forget_for_test();                                   /* dies inside the quiet period */
    sj_init(root);
    CHECK(is(L"UserData\\ups.dat", "old profile"));
    path(p, L"UserData\\NEWSAVE");
    CHECK(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES);
    CHECK(leftovers() == 0);

    /* 7. A clean exit commits a save whose quiet period has not run out. */
    a = open_w(L"UserData\\ups.dat", CREATE_ALWAYS);
    write_s(a, "exit profile"); sj_close(a);
    sj_commit_now();
    CHECK(leftovers() == 0);
    sj_forget_for_test();
    sj_init(root);
    CHECK(is(L"UserData\\ups.dat", "exit profile"));

    /* 8. ... but not one with a save file still open. */
    a = open_w(L"UserData\\ups.dat", CREATE_ALWAYS);
    write_s(a, "half");
    sj_commit_now();
    CHECK(leftovers() > 0);
    CloseHandle(a);
    sj_forget_for_test();
    sj_init(root);
    CHECK(is(L"UserData\\ups.dat", "exit profile"));

    /* 9. An undo copy cut short (the crash came while it was written) is
     *    never moved over the real file; it is cleared. */
    put(L"UserData\\ups.dat", "good profile");
    put(L"UserData\\ups.dat.sjtmp", "go");
    sj_forget_for_test();
    sj_init(root);
    CHECK(is(L"UserData\\ups.dat", "good profile"));
    path(p, L"UserData\\ups.dat.sjtmp");
    CHECK(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES);

    /* 10. Which opens are save writes: write or delete access, or a
     *     disposition that creates or truncates. A read-only open of an
     *     existing file is not (a held one must never block the commit). */
    CHECK(!sj_open_modifies(GENERIC_READ, OPEN_EXISTING, 1));
    CHECK(!sj_open_modifies(GENERIC_READ, OPEN_ALWAYS, 1));
    CHECK(sj_open_modifies(GENERIC_READ, OPEN_ALWAYS, 0));    /* creates it */
    CHECK(sj_open_modifies(GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS, 1));
    CHECK(sj_open_modifies(GENERIC_WRITE, CREATE_ALWAYS, 1));
    CHECK(sj_open_modifies(GENERIC_READ, TRUNCATE_EXISTING, 1));
    CHECK(sj_open_modifies(DELETE, OPEN_EXISTING, 1));
    CHECK(sj_open_modifies(FILE_APPEND_DATA, OPEN_EXISTING, 1));
    CHECK(!sj_access_writes(GENERIC_READ | SYNCHRONIZE | FILE_READ_ATTRIBUTES));
    CHECK(sj_access_writes(GENERIC_WRITE | SYNCHRONIZE));
    CHECK(sj_access_writes(DELETE));

    /* 11. Closing a handle the journal never saw just closes it. */
    sj_close(INVALID_HANDLE_VALUE);

    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
