/* fake_game.c - see fake_game.h. */
#include "fake_game.h"
#include "online/netplay_session.h"

#include <string.h>

fake_game_state fg;
static uint8_t s_mem[FG_RAM_HI - FG_RAM_LO];

static const np_region k_sync[] = {
    { FG_FIGHTERS, 16, "fighters" },
    { FG_TIMER,     8, "timer" },
};
static const np_region k_extra[] = {
    { FG_PADS,  4, "pads" },
    { FG_PAUSE, 1, "pause" },
};
static const np_game_desc k_desc = {
    k_sync, 2, k_extra, 2, FG_SAVE_LEN, FG_LAYOUT_LEN, FG_RAM_LO, FG_RAM_HI
};

uint8_t *fg_mem(uint32_t va) { return &s_mem[va - FG_RAM_LO]; }

static uint32_t rd32(uint32_t va) { uint32_t v; memcpy(&v, fg_mem(va), 4); return v; }
static void wr32(uint32_t va, uint32_t v) { memcpy(fg_mem(va), &v, 4); }

void fake_game_reset(void)
{
    memset(&fg, 0, sizeof fg);
    memset(s_mem, 0, sizeof s_mem);
    fg.pause_owner = -1;
    fg.screen = 1;
    wr32(FG_SEED, 12345u);
}

void fake_game_poll(void)
{
    int p;
    for (p = 0; p < 4; p++) fg.slot[p] = fg.host[p];
}

void fake_game_step(void)
{
    uint8_t *f = fg_mem(FG_FIGHTERS);
    fg_mem(FG_PADS)[0] = (uint8_t)fg.slot[0].buttons;
    fg_mem(FG_PADS)[1] = (uint8_t)fg.slot[1].buttons;
    if (fg.in_fight) {
        uint32_t seed = rd32(FG_SEED) * 214013u + 2531011u;
        wr32(FG_SEED, seed);
        netplay_note_rand();
        f[0] = (uint8_t)(f[0] + (uint8_t)fg.slot[0].buttons + (uint8_t)(seed >> 16));
        f[1] ^= (uint8_t)fg.slot[1].buttons;
        wr32(FG_TIMER, rd32(FG_TIMER) + 1);
    }
}

const np_game_desc *np_game(void) { return &k_desc; }
void np_game_read(uint32_t va, void *dst, uint32_t len) { memcpy(dst, fg_mem(va), len); }
void np_game_write(uint32_t va, const void *src, uint32_t len) { memcpy(fg_mem(va), src, len); }

int      np_game_at_charsel(void) { return fg.at_charsel; }
int      np_game_previews_posed(void) { return fg.posed; }
int      np_game_is_tag(void) { return fg.tag; }
int      np_game_in_fight(void) { return fg.in_fight; }
int      np_game_pause_owner(void) { return fg.pause_owner; }
uint32_t np_game_screen_key(void) { return fg.screen; }
int      np_game_left_versus(void) { return fg.left; }
void     np_game_session_reset(void) { fg.session_resets++; }

void np_game_pad_read(int port, np_pad *p) { *p = fg.slot[port]; }
void np_game_pad_write(int port, const np_pad *p, uint32_t packet) { fg.slot[port] = *p; fg.packet[port] = packet; }
void np_game_pad_neutral(int port) { memset(&fg.slot[port], 0, sizeof(np_pad)); }
void np_game_force_pads(uint32_t mask) { fg.forced = mask; fg.force_calls++; }

void np_game_save_read(uint8_t *dst) { memcpy(dst, fg_mem(FG_SAVE), FG_SAVE_LEN); }
void np_game_save_write(const uint8_t *src) { memcpy(fg_mem(FG_SAVE), src, FG_SAVE_LEN); fg.rebuilds++; }

void np_game_save_apply(const uint8_t *save, const uint8_t *joiner_layout, uint8_t joiner_laysel)
{
    uint8_t vol[2];
    memcpy(vol, fg_mem(FG_SAVE), 2);
    memcpy(fg_mem(FG_SAVE), save, FG_SAVE_LEN);
    memcpy(fg_mem(FG_SAVE), vol, 2);
    memcpy(fg_mem(FG_SAVE + 8 + 4), joiner_layout, FG_LAYOUT_LEN);
    fg_mem(FG_SAVE + 24 + 1)[0] = joiner_laysel;
    fg.rebuilds++;
}

void np_game_layout_read(int port, uint8_t *layout, uint8_t *laysel)
{
    memcpy(layout, fg_mem(FG_SAVE + 8 + 4 * (uint32_t)port), FG_LAYOUT_LEN);
    *laysel = fg_mem(FG_SAVE + 24 + (uint32_t)port)[0];
}

void     np_game_title_redirect(int on) { fg.redirect = on; }
int      np_game_profile_loaded(void) { return fg.profile; }
void     np_game_pad_history_clear(void) { memset(fg_mem(FG_PADS), 0, 4); }
void     np_game_save_unlock_all(uint8_t *save) { save[26] = 1; save[27] = 7; }
uint32_t np_game_rand_seed_va(void) { return FG_SEED; }
void     np_game_worker_rand_seed(uint32_t seed) { fg.worker_seed = seed; }
int      np_game_rand_on_worker(void) { return fg.on_worker; }
