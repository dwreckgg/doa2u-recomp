/* A stand-in for the game behind netplay_game.h, for host tests of the
 * session core. Guest memory is a 64 KB array at guest 0x10000; the test
 * drives the screens directly and fake_game_step() plays one game frame. */
#ifndef DOA2U_FAKE_GAME_H
#define DOA2U_FAKE_GAME_H

#include <stdint.h>
#include "online/netplay_game.h"

#define FG_RAM_LO     0x00010000u
#define FG_RAM_HI     0x00020000u
#define FG_FIGHTERS   0x00010000u   /* sync, 16 bytes */
#define FG_TIMER      0x00010100u   /* sync, 8 bytes */
#define FG_PADS       0x00010200u   /* extra, 4 bytes: what the pad builder folded */
#define FG_PAUSE      0x00010210u   /* extra, 1 byte */
#define FG_SAVE       0x00010300u   /* 32 bytes: volumes +0 +1, layouts +8+4p, laysel +24+p, unlock flag +26, costumes +27 */
#define FG_SEED       0x00010400u
#define FG_SAVE_LEN   32u
#define FG_LAYOUT_LEN 4u

typedef struct {
    int      at_charsel, posed, tag, in_fight, left, pause_owner;   /* pause_owner: -1 none */
    uint32_t screen;
    np_pad   host[4];       /* what XInputGetState put into the raw slots this frame */
    np_pad   slot[4];       /* the raw slots the pad builder reads */
    uint32_t packet[4];
    uint32_t forced;
    int      force_calls, rebuilds, redirect, session_resets, on_worker;
    uint32_t worker_seed;
} fake_game_state;

extern fake_game_state fg;

uint8_t *fg_mem(uint32_t va);
void     fake_game_reset(void);
void     fake_game_poll(void);   /* XInputGetState: host pads into the raw slots */
void     fake_game_step(void);   /* the pad builder and one frame of game logic */

#endif
