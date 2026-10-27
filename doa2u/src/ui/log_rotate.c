/* See log_rotate.h. */
#include "log_rotate.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* name.<n>.ext for path name.ext (name.<n> when there is no extension). */
static void numbered(char *out, size_t cap, const char *path, int n)
{
    const char *slash1 = strrchr(path, '\\'), *slash2 = strrchr(path, '/');
    const char *slash = slash1 > slash2 ? slash1 : slash2;
    const char *dot = strrchr(path, '.');
    if (dot && slash && dot < slash) dot = NULL;
    if (dot)
        snprintf(out, cap, "%.*s.%d%s", (int)(dot - path), path, n, dot);
    else
        snprintf(out, cap, "%s.%d", path, n);
}

void log_rotate(const char *path, int keep)
{
    char from[MAX_PATH], to[MAX_PATH];
    int i;
    if (!path || keep < 1) return;
    numbered(to, sizeof to, path, keep);
    DeleteFileA(to);
    for (i = keep - 1; i >= 1; i--) {
        numbered(from, sizeof from, path, i);
        numbered(to, sizeof to, path, i + 1);
        MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING);
    }
    numbered(to, sizeof to, path, 1);
    MoveFileExA(path, to, MOVEFILE_REPLACE_EXISTING);
}
