/* Undo journal for the save partitions (UserData and TitleData under the save
 * root): a save cut short by a crash or a killed process is rolled back at the
 * next start, so the profile is never left half written. See save_journal.c. */
#ifndef DOA2U_SAVE_JOURNAL_H
#define DOA2U_SAVE_JOURNAL_H

#include <windows.h>

/* A save is over once no save file has been written for this long and none is
 * open for writing. The game writes its three save files within ~15 ms. */
#define SJ_QUIET_MS 1500

/* Sets the save root and recovers an unfinished save from the last run. */
void sj_init(const WCHAR *save_root);
/* Starts the background check that commits a save once it has gone quiet. */
void sj_start_timer(void);

/* Does an open with this host access and disposition change the file (or
 * create it, when it does not exist yet)? */
int  sj_open_modifies(DWORD access, DWORD disposition, int exists);
/* Does a handle with this host access write (or delete) its file? */
int  sj_access_writes(DWORD access);

/* Before a file or directory is created, opened for writing or deleted: keeps
 * its original (or notes that it did not exist), once per save, if it lies in
 * a save partition. */
void sj_before_open_for_write(const WCHAR *path);
/* After a writing open: the handle belongs to the save until it closes. */
void sj_track(HANDLE h, const WCHAR *path);
/* Flushes (if it is a save handle) and closes any handle. */
void sj_close(HANDLE h);
/* Commits the save if it has been quiet since now_ms - SJ_QUIET_MS; returns
 * non-zero when it committed. */
int  sj_poll(unsigned long long now_ms);
/* Clean exit: commits the save at once unless a save file is still open. */
void sj_commit_now(void);
/* Test hook: forget the in-memory state, as a process that died would. */
void sj_forget_for_test(void);

#endif
