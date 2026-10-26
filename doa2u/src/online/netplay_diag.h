/*
 * netplay_diag.h - diagnostics for building online play: guest RAM dumps,
 * a one-controller pad mirror for menu work, and the pad-builder rate.
 *
 * Requests come from files beside DOA2U.exe, checked twice a second, so a
 * script can drive them:
 *   netplay_dump.on    dump guest RAM once; the file's text is the tag; deleted after
 *   netplay_mirror.on  while present: port 1 copies port 0 and both are reported connected
 *   netplay_screens.on while present: log the adapter's screen readings at every change
 * Dumps go to %DOA2U_DUMP_DIR%, else %TEMP%\doa2u_dumps (80 MB each, kept
 * out of the OneDrive folders). With Diag=1 the pad-builder rate is logged
 * every 600 calls.
 */
#ifndef DOA2U_NETPLAY_DIAG_H
#define DOA2U_NETPLAY_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void        netplay_diag_frame(void);            /* every pad-builder call, game thread */
uint32_t    netplay_diag_forced_pads(void);      /* ports 0 and 1 while mirroring */
void        netplay_diag_request_dump(const char *tag);   /* written at the next pad-builder call */
uint32_t    netplay_diag_builder_calls(void);
const char *netplay_diag_dump_dir(void);
const char *netplay_diag_last_dump(void);       /* path of the latest dump, "" before one */

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_DIAG_H */
