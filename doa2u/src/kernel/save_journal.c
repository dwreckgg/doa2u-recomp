/**
 * Undo journal for the save partitions (see save_journal.h).
 *
 * A save is the run of save-partition changes that ends once nothing has
 * been written for SJ_QUIET_MS and no save file is open for writing: the
 * game writes SaveMeta.xbx, SaveImage.xbx and ups.dat one after another,
 * each opened and closed on its own, so the last close of one file is not
 * the end of the save. Before a file or directory is first created, opened
 * for writing or deleted, its original is kept beside it, written through:
 *   <file>.sjundo   the original contents (copied as <file>.sjtmp, then
 *                   renamed, so a half-made copy is never taken for one)
 *   <path>.sjnew    a marker: the file or directory did not exist
 * When the save is over (sj_poll, or sj_commit_now at a clean exit), the
 * files are flushed, the commit marker <root>\.sjcommit is written, the
 * journal files are removed and then the marker. At start-up, journal files
 * without the marker mean the last save was cut short: every original is put
 * back and every new file or directory removed, so all of the save's files go
 * back together and never mix old and new. With the marker, the save had
 * finished and only the clean-up is redone.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "save_journal.h"

#define SJ_MAX_OPEN  32
#define SJ_MAX_FILES 64

static WCHAR  g_root[MAX_PATH];
static HANDLE g_open[SJ_MAX_OPEN];
static int    g_open_n;
static WCHAR  g_files[SJ_MAX_FILES][MAX_PATH];
static int    g_files_n;
static unsigned long long g_last_ms;     /* the save's latest change */
static SRWLOCK g_lock = SRWLOCK_INIT;
static HANDLE g_timer;

static void lock(void)   { AcquireSRWLockExclusive(&g_lock); }
static void unlock(void) { ReleaseSRWLockExclusive(&g_lock); }

static int starts_with_ci(const WCHAR *s, const WCHAR *prefix)
{
    return _wcsnicmp(s, prefix, wcslen(prefix)) == 0;
}

/* In UserData or TitleData under the save root (not the Cache). */
static int in_partition(const WCHAR *path)
{
    WCHAR p[MAX_PATH];
    if (!g_root[0] || !path) return 0;
    swprintf_s(p, MAX_PATH, L"%s\\UserData\\", g_root);
    if (starts_with_ci(path, p)) return 1;
    swprintf_s(p, MAX_PATH, L"%s\\TitleData\\", g_root);
    return starts_with_ci(path, p);
}

static void journal_name(WCHAR *out, const WCHAR *path, const WCHAR *suffix)
{
    swprintf_s(out, MAX_PATH, L"%s%s", path, suffix);
}

int sj_access_writes(DWORD access)
{
    return (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) != 0;
}

int sj_open_modifies(DWORD access, DWORD disposition, int exists)
{
    if (sj_access_writes(access)) return 1;
    if (disposition == CREATE_ALWAYS || disposition == TRUNCATE_EXISTING || disposition == CREATE_NEW) return 1;
    return disposition == OPEN_ALWAYS && !exists;
}

/* Writes a file's bytes through to disk. */
static int write_through(const WCHAR *path, const void *data, DWORD size)
{
    DWORD n = 0;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    int ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = !size || (WriteFile(h, data, size, &n, NULL) && n == size);
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    return ok;
}

/* Copies a file to dst through a temporary name, so dst only ever exists
 * complete. */
static int copy_through(const WCHAR *src, const WCHAR *dst)
{
    WCHAR tmp[MAX_PATH];
    HANDLE h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    char *buf;
    DWORD n = 0;
    int ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return 0; }
    buf = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart + 1);
    if (!buf) { CloseHandle(h); return 0; }
    ok = ReadFile(h, buf, (DWORD)size.QuadPart, &n, NULL) && n == (DWORD)size.QuadPart;
    CloseHandle(h);
    journal_name(tmp, src, L".sjtmp");
    ok = ok && write_through(tmp, buf, n) &&
         MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(tmp);
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

static int journaled(const WCHAR *path)
{
    for (int i = 0; i < g_files_n; i++)
        if (_wcsicmp(g_files[i], path) == 0) return 1;
    return 0;
}

void sj_before_open_for_write(const WCHAR *path)
{
    WCHAR j[MAX_PATH];
    DWORD attrs;
    if (!in_partition(path)) return;
    lock();
    g_last_ms = GetTickCount64();
    if (!journaled(path) && g_files_n < SJ_MAX_FILES) {
        attrs = GetFileAttributesW(path);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            journal_name(j, path, L".sjnew");
            write_through(j, NULL, 0);
        } else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            journal_name(j, path, L".sjundo");
            if (!copy_through(path, j))
                fprintf(stderr, "[SAVE] journal: could not keep the original of %S\n", path);
        }
        /* an existing directory has nothing to keep */
        wcscpy_s(g_files[g_files_n++], MAX_PATH, path);
    }
    unlock();
}

void sj_track(HANDLE h, const WCHAR *path)
{
    if (h == INVALID_HANDLE_VALUE || !h || !in_partition(path)) return;
    lock();
    if (g_open_n < SJ_MAX_OPEN) g_open[g_open_n++] = h;
    g_last_ms = GetTickCount64();
    unlock();
}

/* The save is complete: marker, journal removal, marker removal. Caller holds the lock. */
static void commit_locked(void)
{
    WCHAR marker[MAX_PATH], j[MAX_PATH];
    if (!g_files_n) return;
    swprintf_s(marker, MAX_PATH, L"%s\\.sjcommit", g_root);
    write_through(marker, NULL, 0);
    for (int i = 0; i < g_files_n; i++) {
        journal_name(j, g_files[i], L".sjundo"); DeleteFileW(j);
        journal_name(j, g_files[i], L".sjnew");  DeleteFileW(j);
    }
    DeleteFileW(marker);
    fprintf(stderr, "[SAVE] save finished (%d file(s)); its undo copies are cleared\n", g_files_n);
    g_files_n = 0;
}

void sj_close(HANDLE h)
{
    int tracked = 0;
    if (!h || h == INVALID_HANDLE_VALUE) return;
    lock();
    for (int i = 0; i < g_open_n; i++)
        if (g_open[i] == h) { g_open[i] = g_open[--g_open_n]; tracked = 1; break; }
    if (tracked) {
        FlushFileBuffers(h);
        g_last_ms = GetTickCount64();
    }
    CloseHandle(h);
    unlock();
}

int sj_poll(unsigned long long now_ms)
{
    int committed = 0;
    lock();
    if (g_files_n && g_open_n == 0 && now_ms >= g_last_ms + SJ_QUIET_MS) {
        commit_locked();
        committed = 1;
    }
    unlock();
    return committed;
}

void sj_commit_now(void)
{
    lock();
    if (g_open_n == 0) commit_locked();
    unlock();
}

static VOID CALLBACK on_timer(PVOID ctx, BOOLEAN fired)
{
    (void)ctx; (void)fired;
    sj_poll(GetTickCount64());
}

void sj_start_timer(void)
{
    if (!g_timer && !CreateTimerQueueTimer(&g_timer, NULL, on_timer, NULL, 250, 250, WT_EXECUTEDEFAULT))
        g_timer = NULL;
}

/* A directory the save created: clear its journal, then its files (all new
 * with it), then the directory. */
static void recover_dir(const WCHAR *dir, int committed, int *restored);
static int remove_new_dir(const WCHAR *dir, int *restored)
{
    WCHAR pat[MAX_PATH], item[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    recover_dir(dir, 0, restored);
    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            swprintf_s(item, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
            DeleteFileW(item);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return RemoveDirectoryW(dir);
}

/* Start-up: undo (or finish clearing) the journal found under a directory. */
static void recover_dir(const WCHAR *dir, int committed, int *restored)
{
    WCHAR pat[MAX_PATH], full[MAX_PATH], orig[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        size_t len = wcslen(fd.cFileName);
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        swprintf_s(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { recover_dir(full, committed, restored); continue; }
        if (len > 6 && !_wcsicmp(fd.cFileName + len - 6, L".sjtmp")) {
            DeleteFileW(full);                       /* an undo copy cut short: never used */
        } else if (len > 7 && !_wcsicmp(fd.cFileName + len - 7, L".sjundo")) {
            wcscpy_s(orig, MAX_PATH, full);
            orig[wcslen(orig) - 7] = 0;
            if (committed) DeleteFileW(full);
            else if (MoveFileExW(full, orig, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) (*restored)++;
        } else if (len > 6 && !_wcsicmp(fd.cFileName + len - 6, L".sjnew")) {
            wcscpy_s(orig, MAX_PATH, full);
            orig[wcslen(orig) - 6] = 0;
            if (!committed) {
                DWORD a = GetFileAttributesW(orig);
                if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (remove_new_dir(orig, restored)) (*restored)++;
                } else if (DeleteFileW(orig) || GetLastError() == ERROR_FILE_NOT_FOUND) {
                    (*restored)++;
                }
            }
            DeleteFileW(full);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

void sj_init(const WCHAR *save_root)
{
    WCHAR marker[MAX_PATH], dir[MAX_PATH];
    int committed, restored = 0;
    lock();
    wcscpy_s(g_root, MAX_PATH, save_root ? save_root : L"");
    {   size_t n = wcslen(g_root); if (n && g_root[n - 1] == L'\\') g_root[n - 1] = 0; }
    g_open_n = 0;
    g_files_n = 0;
    if (g_root[0]) {
        swprintf_s(marker, MAX_PATH, L"%s\\.sjcommit", g_root);
        committed = GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
        swprintf_s(dir, MAX_PATH, L"%s\\UserData", g_root);
        recover_dir(dir, committed, &restored);
        swprintf_s(dir, MAX_PATH, L"%s\\TitleData", g_root);
        recover_dir(dir, committed, &restored);
        DeleteFileW(marker);
        if (restored)
            fprintf(stderr, "[SAVE] the last save did not finish: %d file(s) put back as they were before it\n", restored);
    }
    unlock();
}

void sj_forget_for_test(void)
{
    lock();
    g_open_n = 0;
    g_files_n = 0;
    unlock();
}
