/*
 * netplay_game.h - everything the session core needs from the game: guest
 * memory, the screens, the pads, the settings image and the CRT rand seed.
 * One implementation per game: doa2u_game.c (DOA2U's addresses) and
 * tests/fake_game.c (host tests). All of it runs on the game thread.
 */
#ifndef DOA2U_NETPLAY_GAME_H
#define DOA2U_NETPLAY_GAME_H

#include <stdint.h>
#include "netplay_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const np_region *sync;   /* fight state: carried at the barrier and transitions, digested in fights */
    int              n_sync;
    const np_region *extra;  /* digested every frame (pads, pause, mode) */
    int              n_extra;
    uint32_t         save_len;    /* bytes of the in-memory save image */
    uint32_t         layout_len;  /* bytes of one port's button layout (<= NP_LAYOUT_MAX) */
    uint32_t         ram_lo, ram_hi;   /* guest RAM the regions must lie in */
} np_game_desc;

const np_game_desc *np_game(void);

void     np_game_read(uint32_t va, void *dst, uint32_t len);
void     np_game_write(uint32_t va, const void *src, uint32_t len);

/* Screens. */
int      np_game_at_charsel(void);      /* on the Versus character select (Single or Tag) */
int      np_game_previews_posed(void);  /* both cursor characters' preview models are in */
int      np_game_is_tag(void);          /* the Versus battle type is Tag */
int      np_game_in_fight(void);
int      np_game_pause_owner(void);     /* -1 = not paused, else the port (0/1) whose pause menu is up */
uint32_t np_game_screen_key(void);      /* changes exactly when the screen changes */
int      np_game_left_versus(void);     /* once per session frame: 1 = the players left Versus */
void     np_game_session_reset(void);   /* at session start: reset left_versus's counters */

/* Pads: the raw slot the per-frame pad builder reads. */
void     np_game_pad_read(int port, np_pad *p);
void     np_game_pad_write(int port, const np_pad *p, uint32_t packet);
void     np_game_pad_neutral(int port);
void     np_game_force_pads(uint32_t mask);   /* ports in mask open, the rest closed, history cleared */

/* The in-memory save image (match settings, button layouts, volumes). */
void     np_game_save_read(uint8_t *dst);                 /* save_len bytes */
void     np_game_save_write(const uint8_t *src);          /* whole image, then the button maps rebuilt */
/* Start-parameter image: keeps this machine's volume bytes, gives port 1 the
 * joiner's layout, rebuilds the button maps. */
void     np_game_save_apply(const uint8_t *save, const uint8_t *joiner_layout, uint8_t joiner_laysel);
void     np_game_layout_read(int port, uint8_t *layout, uint8_t *laysel);   /* layout_len bytes */
/* In a save image COPY (save_len bytes, never the live one): every hidden
 * character available and every character's costume count at its cap, so
 * both machines play with the full roster whatever their own profiles hold.
 * The live image is restored when the session ends. */
void     np_game_save_unlock_all(uint8_t *save);
void     np_game_title_redirect(int on);  /* in a session: the game's saves go to a side folder */
void     np_game_pad_history_clear(void); /* as if no button was held last frame (press edges) */

/* CRT rand(). */
uint32_t np_game_rand_seed_va(void);      /* the game thread's seed word, 0 if not known yet */
void     np_game_worker_rand_seed(uint32_t seed);
int      np_game_rand_on_worker(void);    /* the rand() call in progress is a worker fiber's */

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_GAME_H */
