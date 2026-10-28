/*
 * doa2u_game.c - DOA2U's side of netplay_game.h: every guest address and
 * guest call online play needs. Each address says how it was found, so a
 * reader can re-check it against the generated code or a RAM dump.
 */
#include <windows.h>
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

/* Guest RAM is one 128 MB view of a page-file section (kernel/xbox_memory_layout.c),
 * so every block is normally mapped; each is checked once and remembered. */
const uint8_t *np_game_block_ptr(uint32_t va)
{
    static uint8_t s_known[NP_GAME_SWEEP_HI / NP_GAME_SWEEP_BLOCK];   /* 0 not checked, 1 mapped, 2 not */
    const uint8_t *host;
    uint32_t k = va / NP_GAME_SWEEP_BLOCK;
    if (va % NP_GAME_SWEEP_BLOCK || va >= NP_GAME_SWEEP_HI) return NULL;
    host = (const uint8_t *)XBOX_PTR(va);
    if (!s_known[k]) {
        MEMORY_BASIC_INFORMATION mbi;
        s_known[k] = 2;
        if (VirtualQuery(host, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (const uint8_t *)mbi.BaseAddress + mbi.RegionSize >= host + NP_GAME_SWEEP_BLOCK)
            s_known[k] = 1;
    }
    return s_known[k] == 1 ? host : NULL;
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
#define ROUND_VA         0x008783C8u   /* 1 from the fight intro through the results screen; STICKY after a pause-menu Quit
                                        * (still 1 on the title, mode menu, submenu and character select after it) */
#define SEL_FLAG_VA(p)   (0x00852F9Fu + 0x70u * (uint32_t)(p))   /* 1 while player p's character-select panel is live: every
                                        * character-select dump (Versus Single/Tag, Sparring, re-entered, after a Quit), 0 on all
                                        * 30 other dumps (2026-10-26 table, classes FIGHT vs CHARSEL) */
#define FIGHT_FLAG_VA    0x00852F45u   /* 0xFF from the fight intro through the results, 0 on every menu, load and
                                        * character select, including the one reached by a Quit (same table) */
#define PAUSE_VA         0x0080B55Fu   /* 1 while the pause menu is up (dump pausetest, 2026-10-26) */
#define PAUSE_OWNER_VA   0x00878390u   /* port whose pause menu it is (dumps pausetest 0 / pause_p2_* 1; read by sub_000DB42B) */
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

/* The select flags also go 1 for the round transition inside a fight (trace
 * 2026-10-26: key 5000006 between rounds), where the screen byte is 0; the
 * character select always has screen byte 2 (every character-select dump,
 * including after a Quit and after the results). */
int np_game_at_charsel(void)
{
    return MEM8(GAME_MODE_VA) == GAME_MODE_VERSUS && MEM8(MENU_SCREEN_VA) == SCRST_CHARSEL &&
           MEM8(SEL_FLAG_VA(0)) == 1 && MEM8(SEL_FLAG_VA(1)) == 1;
}

int np_game_previews_posed(void)
{
    return MEM32(POSE_VA(0)) != POSE_EMPTY && MEM32(POSE_VA(1)) != POSE_EMPTY;
}

int      np_game_is_tag(void) { return MEM8(TAG_FLAG_VA) != 0; }
/* The fight flag drops to 0 for the round transition (measured 2026-10-26:
 * key 5000001 -> 5000100 -> 5000001 at every round change), so inside Versus
 * on the no-menu screen the round byte keeps the fight going; the round byte
 * alone is sticky after a Quit, so it never counts on a menu. */
int np_game_in_fight(void)
{
    if (MEM8(GAME_MODE_VA) != GAME_MODE_VERSUS || MEM8(MENU_SCREEN_VA) != 0 || np_game_at_charsel()) return 0;
    return MEM8(FIGHT_FLAG_VA) == 0xFF || MEM8(ROUND_VA) != 0;
}
int      np_game_pause_owner(void) { return MEM8(PAUSE_VA) ? (MEM8(PAUSE_OWNER_VA) & 1) : -1; }
/* The panel byte also toggles with the round UI inside a fight (measured: a
 * flip 20 frames into round 1 and at each round change), so it only counts
 * on the menus: a round change is not a screen transition. */
uint32_t np_game_screen_key(void)
{
    uint32_t fight = np_game_in_fight() ? 1u : 0u;
    uint32_t sel = np_game_at_charsel() ? 2u : 0u;      /* not the raw flags: they flip between rounds */
    return ((uint32_t)MEM8(GAME_MODE_VA) << 24) | ((uint32_t)MEM8(MENU_SCREEN_VA) << 16) |
           ((!fight && MEM8(MENU_PANEL_VA)) ? 1u << 8 : 0u) | sel | fight;
}

/* The players left Versus: another game mode (the mode menu reads 2, the
 * title 1), or the Single/Tag submenu, fresh (screen 1) or reached by
 * backing out of the character select (screen stays 2, the icon panel is
 * back). A Versus menu screen that is neither the character select nor the
 * stage select for MENU_EXIT_FRAMES covers any other way out. Screen 0
 * with no round is also the stage load (about 480 frames, panel byte 1,
 * measured 2026-10-26) and the results -> character select path, so it is
 * never an exit by itself and the timer is longer than any load. */
#define MENU_EXIT_FRAMES 1800
static uint32_t s_menu_frames;

int np_game_left_versus(void)
{
    uint8_t scr = MEM8(MENU_SCREEN_VA);
    int fight = np_game_in_fight(), sel = np_game_at_charsel();
    if (MEM8(GAME_MODE_VA) != GAME_MODE_VERSUS) return 1;
    if (!fight && scr == SCRST_VS_SUBMENU) return 1;
    if (!fight && !sel && scr == SCRST_CHARSEL && MEM8(MENU_PANEL_VA)) return 1;
    if (!fight && !sel && scr != SCRST_STAGESEL) {
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

/* The builder's per-pad aggregates (the "pads" region): last frame's buttons,
 * press edges and repeat counters. The pad-forcing path clears the same block
 * (doa2u_netplay_force_pads in recomp_manual.c). */
void np_game_pad_history_clear(void)
{
    static const uint8_t zero[0x150];
    np_game_write(0x00A94A70u, zero, sizeof zero);
}

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
    { ROUND_VA,     0x0001u, "round" },     /* sticky after a pause-menu Quit: carried from the host so both
                                            * machines (and a replay) start a match with the same value */
    { 0x00855C10u, 0x7D00u, "fightA" },
    /* Boot-relative counters (Task 7, 2026-10-26): two fresh launches dumped at
     * session frame 2548 differed by the same 9 frames in every one of these
     * (the barrier lands a few frames apart from one boot to the next), and
     * the fight blocks built during the stage load already differed with
     * them. Carried from the host at session start and at transitions so a
     * joiner and a replay start from the same counts. No heap pointers:
     * checked word by word in dumps C_2549/D_2549. */
    { 0x006E3104u, 0x0004u, "frames_a" },  /* frames since boot, read by sub_0014C5C0.. */
    { 0x0087D56Cu, 0x0004u, "frames_b" },  /* frames since boot, 40 readers (sub_001E33E0, sub_001F4830..) */
    { 0x008509A0u, 0x0004u, "uitick0" },   /* per-screen frame counters, 0 on the title */
    /* The two per-player idle records (0x8509A0 and 0x850A7C, stride 0xDC): +0 frame
     * counter, +0xBE the idle-animation countdown, +0xC0/+0xC2 the last pick
     * (sub_0005FE10 -> sub_0005D5E0 -> rand() when the countdown runs out). The
     * record-vs-replay dumps one frame before the fight differed in exactly these
     * words once the other counters were carried. The records also hold heap
     * pointers (+0x40.., +0x98.., +0xC4), so only the counter words travel. */
    { 0x00850A5Cu, 0x0008u, "idle0" },
    /* The records' idle-animation slots (+0x64..+0x97: animation address in the
     * image, progress/frame words, floats; +0xAC..+0xB7 more floats). Their
     * "animation finished" word (+0x7C) gates the next pick, so with the slot
     * uncarried a replay after an unrelated match (the select opened at another
     * moment, another animation phase) picked one frame late (2026-10-26). The
     * heap pointers at +0x98..+0xAB are left out. */
    { 0x00850A04u, 0x0034u, "idle0_slot" },
    { 0x00850A4Cu, 0x000Cu, "idle0_slotb" },
    { 0x00850AE0u, 0x0034u, "idle1_slot" },
    { 0x00850B28u, 0x000Cu, "idle1_slotb" },
    { 0x00850A7Cu, 0x0004u, "uitick0b" },
    { 0x00850B38u, 0x0008u, "idle1" },
    { 0x00852FB0u, 0x0004u, "uitick1" },
    { 0x00853020u, 0x0004u, "uitick2" },
    { 0x00853090u, 0x0004u, "uitick3" },
    { 0x008A7F24u, 0x0004u, "uitick4" },
    { 0x008A7F8Cu, 0x0004u, "uitick5" },
    /* 0x850720 is NOT carried: it is the D3D fence number (sub_003445F0 -> InsertFence), host timing by nature */
    { 0x00850730u, 0x0004u, "tickfine2" },
    { 0x007CA864u, 0x0004u, "timerw0" },   /* round-timer words (999 at a fight start, 240 at the 24\"0 pause) */
    { 0x007CA9ACu, 0x0004u, "timerw1" },
    { 0x007CA9FCu, 0x0004u, "timerw2" },
    { 0x0087D5B4u, 0x0010u, "timer_a" },   /* sub_000652A0 */
    { 0x0087D630u, 0x0014u, "timer_b" },   /* sub_00068328 / sub_00158BB0; +0 is another counter of the family (rec/replay 0xFE vs 0x101) */
    { 0x0087F2B0u, 0x02F0u, "uislots" },   /* 6-word records with a counter each (sub_00069E49 / sub_0006A190) */
    { 0x00881B94u, 0x000Cu, "misc_a" },
    { 0x008847E4u, 0x0004u, "misc_b" },    /* sub_0009D3DB */
    { 0x0088B0B0u, 0x0004u, "misc_c" },
    /* The D3D device's swap counter (device 0x355500 + 0x2478, mirrored to
     * +0x1DE4 by KickOff each frame): the game reads it through the D3D getter
     * sub_00344180 (callers sub_00021370 / sub_000227D0 / sub_0011D250 /
     * sub_00282DA0) as its idle timer, so the character-select idle picks, and
     * with them the rand() stream and the intro poses, fell on different frames
     * on two boots (rand-caller histogram 2026-10-26: sites 0005D5E5 / 00060078
     * on the select; replays diverged at the first fight frame with the fight
     * blocks already holding different poses one frame before). Swaps run one
     * per game loop here, so the count advances identically once carried. */
    { 0x003572E4u, 0x0004u, "swapcnt_a" },
    { 0x00357978u, 0x0004u, "swapcnt_b" },
    /* Boot-time values that never change afterwards but differ per launch
     * (record-vs-replay dumps at fight frames 9, 10 and 300, 2026-10-26: the
     * same words differ at all three and none changes between them): a float
     * table (no pointers), two copies of one float, an 18-float block, a
     * counter and a flag word. Carried so the two machines compute from the
     * same constants; which of them the fight reads was not established. */
    { 0x0083D37Cu, 0x4094u, "boottab" },
    { 0x0085052Cu, 0x0004u, "bootf_a" },
    { 0x00852F2Cu, 0x0004u, "bootf_b" },
    { 0x00850698u, 0x0060u, "bootf_c" },
    { 0x00850878u, 0x0004u, "cnt_a" },
    { 0x0088F85Cu, 0x0004u, "flag_a" },
    /* Camera smoothing state (0x87A900..0x87AD78, no pointers): nine words
     * already differed one frame before the fight and the camera matrices
     * (fightC1/fightD) parted on fight frame 10, then re-converged by frame
     * 300. Carried so the intro camera starts from the same lag state. */
    { 0x0087A900u, 0x0478u, "camlag" },
    /* The game's own random generator: a Mersenne Twister object at 0x8A5768
     * (624 state words + index; sub_0001FD20 seeds it, sub_0001FF40 draws, about
     * 40 call sites through sub_000D93E0/sub_000D93F0). It is seeded at boot,
     * so two launches draw different numbers and the camera, the hair and then
     * the fighters parted at an arbitrary frame (10, 130, 1290 in three pairs)
     * with equal inputs and an equal CRT rand stream. Its words look like heap
     * pointers by chance; none is. */
    { 0x008A5768u, 0x09C8u, "mt19937" },
    { 0x0085FE90u, 0x3C00u, "fightB" },     /* +0xA00 (2026-10-26): 0x863090..0x863A90 drifted apart by fight frame 300 while everything else matched */
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
    /* per-boot words the two-boot test found (final review I4): */
    { 0x007BFDDCu, 0x0004u, "tick1s" },    /* last tick of the main loop's once-a-second probe (sub_002A5530) */
    { 0x00801A18u, 0x0004u, "playtick" },  /* tick at profile load, for play time (sub_00023C10); set in-session */
    { 0x003DC9FCu, 0x0004u, "bootword" },  /* differs per boot, no absolute reference in the code */
};
static const np_region k_extra[] = {
    { 0x00A94A70u, 0x0150u, "pads" },      /* per-pad aggregates the builder writes */
    { 0x00878391u, 0x0002u, "screen" },    /* menu screen byte + game mode byte */
    { 0x0087D49Du, 0x0001u, "tag" },
    { PAUSE_VA,    0x0001u, "pause" },
};

static const np_game_desc k_desc = {
    k_sync, (int)(sizeof k_sync / sizeof k_sync[0]),
    k_extra, (int)(sizeof k_extra / sizeof k_extra[0]),
    SAVE_LEN, SAVE_LAYOUT_LEN, 0x00010000u, XBOX_LOW_END
};

const np_game_desc *np_game(void) { return &k_desc; }
