/* The game-data fingerprint two players compare before a session (netplay_files.c).
 *
 * Every file under the game folder counts, by its path (relative, lower case, '/'),
 * its size and samples of its content: the first and last 64 KB and 64 KB at every
 * 16 MB. A change outside the samples is not seen; a different dump, version or a
 * modded file almost always shows. Files the PC itself writes there (names starting
 * "doa2u_", such as the shader cache) and desktop.ini / thumbs.db are left out. */
#ifndef DOA2U_NETPLAY_FILES_H
#define DOA2U_NETPLAY_FILES_H

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The fingerprint of the files under `dir` (0 when it holds none). With `listing`,
 * also writes one line per counted file: "<path> <size> <sample hash>". */
uint64_t np_files_fingerprint(const wchar_t *dir, FILE *listing);

#ifdef __cplusplus
}
#endif

#endif
