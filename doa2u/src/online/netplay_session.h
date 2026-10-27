/**
 * netplay_session.h - lockstep session core for online play.
 *
 * A session covers one stay in Versus (Single Battle or Tag Battle). It is
 * armed from the Online tab and starts at the barrier, the first input frame
 * on which the Versus character select has settled. From there until the
 * players leave Versus:
 *
 *   - both players' pads come from the input exchange and are written into
 *     guest ports 0 (host) and 1 (joiner) before the per-frame pad builder
 *     runs; ports 2 and 3 are closed;
 *   - guest clocks, worker scheduling and the audio chip follow the frame
 *     counter (kernel/xbox_det.c, apu/apu_core.c);
 *   - the rand seed, the match settings (the host's save image with every
 *     character and costume unlocked) and the fight-state regions come from
 *     the session start parameters; each machine's own save image is backed
 *     up, its saves are redirected to a side folder meanwhile, and both are
 *     put back at the end, so a session never changes a player's profile;
 *   - a digest of the fight state is recorded every frame.
 *
 * Outside a session none of this is active and the game runs as before.
 * Ported from parkerallan/doa3-recomp (MIT), doa3/src/online at 919c49f;
 * the game-specific parts are behind netplay_game.h.
 */
#ifndef DOA2U_NETPLAY_SESSION_H
#define DOA2U_NETPLAY_SESSION_H

#include <stdint.h>
#include "netplay_format.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { NP_ROLE_HOST = 0, NP_ROLE_JOINER = 1 };

enum {
    NP_STATE_OFF = 0,     /* not armed: the game runs as before */
    NP_STATE_ARMED,       /* waiting for the Versus character select */
    NP_STATE_ACTIVE       /* lockstep session running */
};

#define NP_SESSION_PADS 0x3u      /* ports 0 (host) and 1 (joiner) */
#define NP_MAX_REGIONS  96        /* sync + extra + the seed/clock hash */
#define NP_NO_FRAME     0xFFFFFFFFu

typedef struct {
    int  state;           /* NP_STATE_* */
    int  role;            /* NP_ROLE_* */
    int  tag;             /* 0 Single Battle, 1 Tag Battle (at the barrier) */
    uint32_t frame;       /* frames since the barrier */
    uint64_t digest;      /* digest of the latest frame */
    uint32_t sessions;    /* sessions completed since launch */
    char last_log[260];   /* digest log written by the last session */
    int  replaying;       /* a replay is loaded or running */
    uint32_t replay_frames;
    uint32_t diverged_at; /* first differing frame, NP_NO_FRAME while matching */
    char message[200];    /* last record/replay/arming message */
} np_status;

/* Online tab controls. `role` only matters for a local session: JOINER
 * exercises the joiner path (save redirect, settings image, restore).
 * Arming refuses (and says why in the status message) when the region
 * tables fail their checks. */
void netplay_set_armed(int armed, int role);
int  netplay_armed(void);
/* Player 2 copies player 1's pad, before and during a local session. */
void netplay_set_mirror(int on);
int  netplay_mirror(void);
int  netplay_active(void);
void netplay_get_status(np_status *out);

/* Determinism testing. Recording writes netplay_replay_*.dnr when a session
 * ends. A replay starts at the same character select, plays the recorded
 * pads, and reports the first frame whose digest differs. */
void netplay_set_record(int on);
int  netplay_record(void);
void netplay_request_replay(const char *path);   /* loaded at the next input tick */
/* Lockstep from boot (spec section 12): called before the guest's entry point.
 * Arms the session with fixed clock bases and `seed`, or replays `replay_path`
 * (a recording made from boot). Frames count from the first input tick and
 * the session lasts until disarmed or, for a replay, the recording's end. */
int  netplay_boot_begin(uint32_t seed, const char *replay_path);
/* In a session every movie is skipped (the game's movie start returns at once
 * and its own finished check ends the movie): playback stalls on frame-counted
 * time, and an online session must not depend on host-timed decoding. */
int  netplay_skip_movies(void);
int  netplay_list_replays(char (*names)[128], int max);

/* Game hooks (recomp_manual.c, through netplay_frame_hook). */
void     netplay_input_tick(void);                    /* before the pad builder */
void     netplay_note_rand(void);                     /* every CRT rand() call */
uint32_t netplay_filter_pad_mask(uint32_t host_mask); /* XGetDevices / XGetDeviceChanges */
/* Ports the session forces present (0 outside a session). A forced port the
 * game has not opened yet must be reported as an insertion. */
uint32_t netplay_forced_pads(void);
uint32_t netplay_session_frame(void);                 /* frames since the barrier */
uint32_t netplay_seed_va(void);                       /* the game thread's rand() seed word, 0 outside a session */

/* ── Remote input provider (netplay.c) ─────────────────────────────────
 * With a provider set, an armed session is an online one. Everything is
 * called on the game thread from the input tick. */
typedef struct {
    /* Every input tick while armed and not yet running. `at_barrier` is set
     * once the local game has settled at the character select; `tag` is the
     * battle type there. Return 1 to begin (the start parameters were
     * loaded or captured), 0 to keep waiting, -1 to disarm. */
    int  (*barrier)(int at_barrier, int tag);
    /* One lockstep frame: this machine's pad in, both players' pads out
     * (0 = host, 1 = joiner). Returns 0 when the session must end. */
    int  (*exchange)(const np_pad *local, np_pad out[2]);
    /* The digest of the state entering the frame just exchanged. */
    void (*digest)(uint32_t frame, uint64_t digest);
    /* The session ended. */
    void (*end)(void);
    /* A screen transition inside the session (same frame on both sides):
     * the host sends its sync regions, the joiner applies them. Return 0 to
     * end the session. */
    int  (*transition)(uint32_t index);
} np_provider;

int  netplay_sync_capture(const uint8_t **blob, uint32_t *len);
int  netplay_sync_apply(const uint8_t *blob, uint32_t len);
void netplay_set_provider(const np_provider *p);   /* NULL: local sessions only */
int  netplay_has_provider(void);

/* Start parameters as one blob (netplay_format.h). The capture buffer
 * belongs to this module until the next capture. Non-zero on success. */
int  netplay_params_capture(const uint8_t **blob, uint32_t *len,
                            const uint8_t joiner_layout[NP_LAYOUT_MAX], uint8_t joiner_laysel);
int  netplay_params_load(const uint8_t *blob, uint32_t len);
void netplay_local_layout(uint8_t layout[NP_LAYOUT_MAX], uint8_t *laysel);
uint32_t netplay_build_stamp(void);
void netplay_session_set_end_note(const char *note);

/* Per-region hashes of a recent frame (the last 256, all of them while
 * recording, replaying or with diagnostics on). */
int  netplay_session_frame_regions(uint32_t frame, uint32_t *out, int max);
int  netplay_session_region_count(void);
void netplay_session_peek(uint32_t va, void *dst, uint32_t len);
const char *netplay_session_region_name(int k);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_SESSION_H */
