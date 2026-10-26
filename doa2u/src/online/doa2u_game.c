/*
 * doa2u_game.c - DOA2U's side of netplay_game.h: every guest address and
 * guest call online play needs. Each address says how it was found, so a
 * reader can re-check it against the generated code or a RAM dump.
 */
#include <string.h>

#include "netplay_game.h"
#include "game/recomp/recomp_types.h"

/* recomp_manual.c */
void     doa2u_netplay_force_pads(uint32_t mask);
uint32_t doa2u_rand_seed_va(void);
void     doa2u_worker_rand_seed(uint32_t seed);
/* kernel/xbox_fiber.c */
int xbox_fiber_active(void);
int xbox_fiber_is_primary(void);
int xbox_fiber_is_coroutine(void);

/* Pads. sub_000FA5D0 (boot) zeroes 4 x 0x80 bytes from 0xA94838 and opens
 * each pad into +0x78; sub_000FA6C0 (every frame) hands pad+0x19 to
 * XInputGetState (sub_003BA04C); sub_000FA7B0, the pad builder's loop,
 * reads that same slot: buttons pad+0x1D, analog pad+0x1F.., thumbs
 * pad+0x27/0x29/0x2B/0x2D. One slot per pad (DOA3 had two). */
#define PAD_VA(p)  (0x00A94838u + 0x80u * (uint32_t)(p))
#define PAD_RAW    0x19u

void np_game_read(uint32_t va, void *dst, uint32_t len)
{
    memcpy(dst, (const void *)XBOX_PTR(va), len);
}

void np_game_write(uint32_t va, const void *src, uint32_t len)
{
    memcpy((void *)XBOX_PTR(va), src, len);
}

void np_game_pad_read(int port, np_pad *p)
{
    uint32_t s = PAD_VA(port) + PAD_RAW;
    int i;
    p->buttons = MEM16(s + 4);
    for (i = 0; i < 8; i++) p->analog[i] = MEM8(s + 6 + i);
    p->lx = (int16_t)MEM16(s + 14);
    p->ly = (int16_t)MEM16(s + 16);
    p->rx = (int16_t)MEM16(s + 18);
    p->ry = (int16_t)MEM16(s + 20);
}

void np_game_pad_write(int port, const np_pad *p, uint32_t packet)
{
    uint32_t s = PAD_VA(port) + PAD_RAW;
    int i;
    MEM32(s) = packet;
    MEM16(s + 4) = p->buttons;
    for (i = 0; i < 8; i++) MEM8(s + 6 + i) = p->analog[i];
    MEM16(s + 14) = (uint16_t)p->lx;
    MEM16(s + 16) = (uint16_t)p->ly;
    MEM16(s + 18) = (uint16_t)p->rx;
    MEM16(s + 20) = (uint16_t)p->ry;
}

void np_game_pad_neutral(int port)
{
    uint32_t s = PAD_VA(port) + PAD_RAW, i;
    for (i = 4; i < 22; i++) MEM8(s + i) = 0;               /* the packet number stays */
}

void np_game_force_pads(uint32_t mask) { doa2u_netplay_force_pads(mask); }

/* CRT rand() is sub_003352A4: seed at [_getptd() (sub_00338901)] + 0x14,
 * multiplier 214013, increment 2531011 (recomp_0018.c). */
uint32_t np_game_rand_seed_va(void) { return doa2u_rand_seed_va(); }
void     np_game_worker_rand_seed(uint32_t seed) { doa2u_worker_rand_seed(seed); }

int np_game_rand_on_worker(void)
{
    return xbox_fiber_active() && !xbox_fiber_is_primary() && !xbox_fiber_is_coroutine();
}

/* ── Screens, settings image, regions (Task 4; values MEASURED, see
 * docs/netplay/doa2u-addresses.md for the dump pairs and queries) ─────── */
#include "kernel/xbox_memory_layout.h"

void xbox_path_set_title_redirect(int on);   /* kernel/kernel_path.c */
void doa2u_rebuild_button_maps(void);        /* recomp_manual.c: guest call */

/* MEASURED 2026-10-26 (docs/netplay/doa2u-addresses.md). DOA2U keeps one
 * menu-screen byte where DOA3 had a mode byte and a screen byte, and an
 * in-round flag beside it; the predicates below are written from those. */
#define GAME_MODE_VA     0x00878392u   /* 1 title/profile, 2 mode menu, 5 Versus (Single and Tag), 9 Sparring */
#define GAME_MODE_TITLE  1
#define GAME_MODE_VERSUS 5
#define MENU_SCREEN_VA   0x00878391u   /* 0 none (title, fight, results), 1 mode menu / Versus submenu, 2 character select, 3 stage select */
#define SCRST_VS_SUBMENU 1
#define SCRST_CHARSEL    2
#define SCRST_STAGESEL   3
#define ROUND_VA         0x008783C8u   /* 1 from the fight intro through the results screen */
#define MENU_PANEL_VA    0x00806458u   /* 1 while the mode menu / Versus submenu icon panel is alive; 0 on the character
                                        * select, the results and Settings. Needed because the screen byte keeps 2 after
                                        * backing out of the character select to the submenu (measured: x_sub_back) */
#define TAG_FLAG_VA      0x0087D49Du   /* 1 on every Tag Battle screen from its character select on */
#define POSE_VA(side)    (0x00875C5Cu + 0x800u * (uint32_t)(side))   /* the portrait bone bank's first row: 1.0f once the preview model is posed */
#define POSE_EMPTY       0x00000000u
#define SAVE_VA          0x007EF8E0u   /* the profile image (ups.dat decoded); game-data block at +0xF200, pointer at 0x87CFA8 */
#define SAVE_LEN         0x00012148u
#define SAVE_VOL_MUSIC   0xF21Au
#define SAVE_VOL_SE      0xF21Bu
#define SAVE_LAYOUT_LEN  16u           /* 8 button words */
#define SAVE_LAYOUT(p)   (0xF21Eu + 0x14u * (uint32_t)(p))
#define SAVE_LAYSEL(p)   (0xF22Eu + 0x14u * (uint32_t)(p))

int np_game_at_charsel(void)
{
    return MEM8(GAME_MODE_VA) == GAME_MODE_VERSUS && MEM8(MENU_SCREEN_VA) == SCRST_CHARSEL &&
           MEM8(ROUND_VA) == 0 && MEM8(MENU_PANEL_VA) == 0;
}

int np_game_previews_posed(void)
{
    return MEM32(POSE_VA(0)) != POSE_EMPTY && MEM32(POSE_VA(1)) != POSE_EMPTY;
}

int      np_game_is_tag(void) { return MEM8(TAG_FLAG_VA) != 0; }
int      np_game_in_fight(void) { return MEM8(ROUND_VA) != 0; }
/* The panel byte also toggles with the round UI inside a fight (measured: a
 * flip 20 frames into round 1 and at each round change), so it only counts
 * on the menus: a round change is not a screen transition. */
uint32_t np_game_screen_key(void)
{
    uint8_t in_round = MEM8(ROUND_VA);
    return ((uint32_t)MEM8(GAME_MODE_VA) << 24) | ((uint32_t)MEM8(MENU_SCREEN_VA) << 16) |
           ((uint32_t)(!in_round && MEM8(MENU_PANEL_VA) ? 1 : 0) << 8) | in_round;
}

/* The players left Versus: another game mode (the mode menu reads 2, the
 * title 1), or the Single/Tag submenu; a Versus menu screen that is neither
 * the character select nor the stage select for MENU_EXIT_FRAMES covers any
 * other way out (the results -> character select path passes through screen
 * 0 briefly, so screen 0 alone is not an exit). */
#define MENU_EXIT_FRAMES 600
static uint32_t s_menu_frames;

int np_game_left_versus(void)
{
    uint8_t scr = MEM8(MENU_SCREEN_VA), in_round = MEM8(ROUND_VA);
    if (MEM8(GAME_MODE_VA) != GAME_MODE_VERSUS) return 1;
    if (!in_round && scr == SCRST_VS_SUBMENU) return 1;
    if (!in_round && MEM8(MENU_PANEL_VA)) return 1;            /* the submenu reached by backing out keeps screen 2 */
    if (!in_round && scr != SCRST_CHARSEL && scr != SCRST_STAGESEL) {
        if (++s_menu_frames > MENU_EXIT_FRAMES) return 1;
    } else {
        s_menu_frames = 0;
    }
    return 0;
}

void np_game_session_reset(void) { s_menu_frames = 0; }

void np_game_save_read(uint8_t *dst) { np_game_read(SAVE_VA, dst, SAVE_LEN); }

void np_game_save_write(const uint8_t *src)
{
    np_game_write(SAVE_VA, src, SAVE_LEN);
    doa2u_rebuild_button_maps();
}

void np_game_save_apply(const uint8_t *save, const uint8_t *joiner_layout, uint8_t joiner_laysel)
{
    uint8_t vol[2];
    vol[0] = MEM8(SAVE_VA + SAVE_VOL_MUSIC);
    vol[1] = MEM8(SAVE_VA + SAVE_VOL_SE);
    np_game_write(SAVE_VA, save, SAVE_LEN);
    MEM8(SAVE_VA + SAVE_VOL_MUSIC) = vol[0];
    MEM8(SAVE_VA + SAVE_VOL_SE) = vol[1];
    np_game_write(SAVE_VA + SAVE_LAYOUT(1), joiner_layout, SAVE_LAYOUT_LEN);
    MEM8(SAVE_VA + SAVE_LAYSEL(1)) = joiner_laysel;
    doa2u_rebuild_button_maps();
}

void np_game_layout_read(int port, uint8_t *layout, uint8_t *laysel)
{
    np_game_read(SAVE_VA + SAVE_LAYOUT(port), layout, SAVE_LAYOUT_LEN);
    *laysel = MEM8(SAVE_VA + SAVE_LAYSEL(port));
}

void np_game_title_redirect(int on) { xbox_path_set_title_redirect(on); }

/* The profile's game-data block sits at SAVE_VA + 0xF200 (the game's pointer
 * at 0x87CFA8 points there once a profile is loaded; Task 4 confirms that
 * MEM32(0x87CFA8) == SAVE_VA + 0xF200). Inside it: three bytes at +0x2ED2 for
 * the hidden characters (1 = available) and a costume count per character
 * slot at +0x2ED8; the slot's character id is the byte at 0x6959DC[slot] and
 * its costume total the byte at 0x6E0F08[id]. Layout from the Unlockables tab
 * work (branch unlockables-tab, unlockables.h), where it was confirmed
 * against the game's own unlock routine sub_000DC640 and its character
 * select sub_0011E41B. */
#define GD_OFF            0xF200u
#define GD_CHAR_FLAGS     (GD_OFF + 0x2ED2u)
#define GD_COSTUME_COUNTS (GD_OFF + 0x2ED8u)
#define GD_SLOTS          15u
#define SLOT_ID_TABLE_VA  0x006959DCu
#define COSTUME_TOTALS_VA 0x006E0F08u

void np_game_save_unlock_all(uint8_t *save)
{
    uint32_t slot;
    if (SAVE_LEN < GD_COSTUME_COUNTS + GD_SLOTS) return;
    save[GD_CHAR_FLAGS] = save[GD_CHAR_FLAGS + 1] = save[GD_CHAR_FLAGS + 2] = 1;
    for (slot = 0; slot < GD_SLOTS; slot++) {
        uint8_t id = MEM8(SLOT_ID_TABLE_VA + slot), total;
        if (id > 0x18) continue;
        total = MEM8(COSTUME_TOTALS_VA + id);
        if (total == 0 || total > 64) continue;
        if (save[GD_COSTUME_COUNTS + slot] < total) save[GD_COSTUME_COUNTS + slot] = total;
    }
}

/* Fight state (first pass; Task 7 completes it from replay diffs). Found by
 * diffing two moments of a Versus fight (vs_fight_a / vs_fight_b) and two
 * cursor positions of the character select, keeping the BSS blocks below
 * the heap that changed, then scanning each for dwords in the heap range
 * (0x880000..0x5000000) over seven dumps: the blocks below hold none;
 * 0x87AD60.. held 14 and is carried in the pointer-free pieces between them;
 * 0x80ACF0.. held 4 at its head and starts after them. */
static const np_region k_sync[] = {
    { 0x00855C10u, 0x7D00u, "fightA" },
    { 0x0085FE90u, 0x3200u, "fightB" },
    { 0x0087AD78u, 0x008Cu, "fightC0" },
    { 0x0087AE14u, 0x004Cu, "fightC1" },
    { 0x0087AF20u, 0x1080u, "fightC2" },
    { 0x00877EF0u, 0x0410u, "fightD" },
    { 0x008793F0u, 0x03E0u, "fightE" },
    { 0x0084D340u, 0x07F0u, "fightF" },
    { 0x00849EC0u, 0x0200u, "fightG" },
    { 0x0084A3A0u, 0x0270u, "fightH" },
    { 0x0084A760u, 0x03C0u, "fightI" },
    { 0x00875C20u, 0x1000u, "banks" },     /* the two fighters' bone banks (32 rows each) */
    { 0x008076E0u, 0x0008u, "picks1" },    /* character-select cursor bytes that followed a cursor move in both walks */
    { 0x00807778u, 0x0004u, "picks2" },
    { 0x008083C0u, 0x0008u, "picks3" },
    { 0x0080AD20u, 0x02E0u, "selidx" },    /* 16-byte records whose index byte followed the cursor */
};
static const np_region k_extra[] = {
    { 0x00A94A70u, 0x0150u, "pads" },      /* per-pad aggregates the builder writes */
    { 0x00878391u, 0x0002u, "screen" },    /* menu screen byte + game mode byte */
    { 0x008783C8u, 0x0001u, "round" },
    { 0x0087D49Du, 0x0001u, "tag" },
    { 0x0080B55Fu, 0x0001u, "pause" },
};

static const np_game_desc k_desc = {
    k_sync, (int)(sizeof k_sync / sizeof k_sync[0]),
    k_extra, (int)(sizeof k_extra / sizeof k_extra[0]),
    SAVE_LEN, SAVE_LAYOUT_LEN, 0x00010000u, XBOX_LOW_END
};

const np_game_desc *np_game(void) { return &k_desc; }
