/*
 * xiso_extract.h - first-run asset install from a Dead or Alive 2 Ultimate disc image.
 * Reads XDVDFS from an .xiso or full .iso, checks the release and region, copies it into assets.
 */

#ifndef DOA2U_XISO_EXTRACT_H
#define DOA2U_XISO_EXTRACT_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XisoProgress {
    volatile LONG64 bytes_done;
    volatile LONG64 bytes_total;
    volatile LONG   files_done;
    volatile LONG   files_total;
    volatile LONG   cancel;           /* set by the caller to abort */
    char            current_name[64]; /* file being written (display only) */
} XisoProgress;

/* Non-zero when every file the game needs is present in assets_dir. */
int xiso_assets_ready(const WCHAR *assets_dir);

/* Validate `image` and extract it into `assets_dir`; blocking, poll `prog`.
 * Returns 1 on success, else writes a user-facing explanation to err. */
int xiso_install(const WCHAR *image, const WCHAR *assets_dir,
                 XisoProgress *prog, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_XISO_EXTRACT_H */
