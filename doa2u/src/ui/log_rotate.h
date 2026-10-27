/* Keeping earlier logs at start-up: see log_rotate.c. */
#ifndef DOA2U_LOG_ROTATE_H
#define DOA2U_LOG_ROTATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Before `path` is opened afresh: path becomes name.1.ext, name.1.ext
 * becomes name.2.ext and so on up to name.<keep>.ext; the oldest is
 * dropped. Missing files are skipped. */
void log_rotate(const char *path, int keep);

#ifdef __cplusplus
}
#endif

#endif
