/* netplay_hook.c - see netplay_hook.h. */
#include "netplay_hook.h"
#include "netplay_diag.h"

void netplay_frame_hook(void)
{
    netplay_diag_frame();
}

uint32_t netplay_hook_forced_pads(void)
{
    return netplay_diag_forced_pads();
}

uint32_t netplay_hook_filter_pad_mask(uint32_t host_mask)
{
    uint32_t forced = netplay_hook_forced_pads();
    return forced ? forced : host_mask;
}

void netplay_hook_note_rand(void) {}

int netplay_hook_session_active(void) { return 0; }
