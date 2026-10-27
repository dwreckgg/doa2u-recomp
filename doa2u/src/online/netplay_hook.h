/* netplay_hook.h - the only calls the recompiled game makes into online
 * play (game/recomp/recomp_manual.c). Each is one flag check when nothing
 * online is in use. */
#ifndef DOA2U_NETPLAY_HOOK_H
#define DOA2U_NETPLAY_HOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every call of the per-frame pad builder (sub_000FA760), before it runs:
 * the XInputGetState poll has filled the raw pad slots, the builder has not
 * folded them yet. */
void     netplay_frame_hook(void);
/* XGetDevices / XGetDeviceChanges: the pad set the game is told about. */
uint32_t netplay_hook_filter_pad_mask(uint32_t host_mask);
uint32_t netplay_hook_forced_pads(void);
/* CRT rand() (sub_003352A4), every call. */
void     netplay_hook_note_rand(void);
int      netplay_hook_session_active(void);
uint32_t netplay_hook_seed_va(void);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_HOOK_H */
