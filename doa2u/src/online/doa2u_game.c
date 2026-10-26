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
