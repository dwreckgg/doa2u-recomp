/* Undo journal for the save partitions (UserData and TitleData under the save
 * root): a save cut short by a crash or a killed process is rolled back at the
 * next start, so the profile is never left half written. See save_journal.c. */
#ifndef DOA2U_SAVE_JOURNAL_H
#define DOA2U_SAVE_JOURNAL_H

#include <windows.h>

/* Sets the save root and recovers an unfinished save from the last run. */
void sj_init(const WCHAR *save_root);
/* Before a file is opened for writing (or deleted): keeps its original, once
 * per save, if it lies in a save partition. */
void sj_before_open_for_write(const WCHAR *path);
/* After that open: the handle belongs to the save until it closes. */
void sj_track(HANDLE h, const WCHAR *path);
/* Flushes and closes a handle; returns non-zero if it was a journaled handle
 * whose close finished the save (the journal was committed). */
int  sj_close(HANDLE h);
/* After a delete outside any open handle: commit the save if nothing is open. */
void sj_commit_if_idle(void);
/* Test hook: forget the in-memory state, as a process that died would. */
void sj_forget_for_test(void);

#endif
