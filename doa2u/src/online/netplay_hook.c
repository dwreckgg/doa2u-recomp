/* netplay_hook.c - see netplay_hook.h. */
#include "netplay_hook.h"
#include "netplay_diag.h"
#include "netplay_session.h"
#include "netplay.h"

void netplay_frame_hook(void)
{
    netplay_diag_frame();
    netplay_poll();                                     /* the online lobby: one flag check offline */
    netplay_input_tick();
}

uint32_t netplay_hook_forced_pads(void)
{
    return netplay_forced_pads() | netplay_diag_forced_pads();
}

uint32_t netplay_hook_filter_pad_mask(uint32_t host_mask)
{
    uint32_t forced = netplay_hook_forced_pads();
    return forced ? forced : host_mask;
}

void netplay_hook_note_rand(void) { netplay_note_rand(); }

int netplay_hook_session_active(void) { return netplay_active(); }
int netplay_hook_skip_movies(void) { return netplay_skip_movies(); }
uint32_t netplay_hook_seed_va(void) { return netplay_seed_va(); }
void netplay_hook_fpu_guard(void) { netplay_fpu_guard(); }
