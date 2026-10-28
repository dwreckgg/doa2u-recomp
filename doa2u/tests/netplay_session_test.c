/* netplay_session.c against the fake game: the barrier, the pre-session
 * mirror, recording and replay (matching and diverging), the ring of recent
 * frames, and the online provider path (start parameters, joiner restore,
 * transitions). */
#include "online/netplay_session.h"
#include "fake_game.h"
#include "netplay_stubs.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static np_status st(void) { np_status s; memset(&s, 0, sizeof s); netplay_get_status(&s); return s; }

static void tick(void)
{
    fake_game_poll();
    netplay_input_tick();
    fake_game_step();
}

/* Reach an active session from the character select: posed, io quiet. */
static int reach_barrier(void)
{
    int i;
    fg.at_charsel = 1;
    fg.posed = 1;
    for (i = 0; i < 40 && st().state != NP_STATE_ACTIVE; i++) tick();
    return st().state == NP_STATE_ACTIVE;
}

/* The scripted match every recording and replay plays: pads from the frame
 * number, the fight from frame 5, a screen change there, out at frame 30. */
static void script(uint32_t frame)
{
    fg.host[0].buttons = (uint16_t)(frame * 37u + 1u);
    fg.host[0].lx = (int16_t)(frame * 100);
    fg.host[1].buttons = (uint16_t)(frame * 11u);
    if (frame == 5) { fg.in_fight = 1; fg.at_charsel = 0; fg.screen = 2; }
    if (frame == 30) fg.left = 1;
}

static int play_match(int perturb_at)
{
    int i;
    for (i = 0; i < 60 && st().state == NP_STATE_ACTIVE; i++) {
        uint32_t f = netplay_session_frame();
        script(f);
        if ((int)f == perturb_at) fg_mem(FG_FIGHTERS)[5] ^= 0xFF;
        tick();
    }
    return st().state != NP_STATE_ACTIVE;
}

static void reset_screens(void)
{
    fg.in_fight = 0; fg.left = 0; fg.screen = 1;
    fg.at_charsel = 0; fg.posed = 0;
    memset(fg.host, 0, sizeof fg.host);
}

static void test_barrier_and_mirror(void)
{
    int i;
    uint8_t before[FG_SAVE_LEN];
    fake_game_reset();
    fg_mem(FG_SAVE)[26] = 0; fg_mem(FG_SAVE)[27] = 2;            /* this profile: nothing unlocked */
    memcpy(before, fg_mem(FG_SAVE), FG_SAVE_LEN);
    netplay_set_mirror(1);
    netplay_set_armed(1, NP_ROLE_HOST);
    CHECK(netplay_forced_pads() == NP_SESSION_PADS);              /* one controller: port 1 forced */
    CHECK(netplay_filter_pad_mask(0x1) == NP_SESSION_PADS);

    fg.at_charsel = 1;                                           /* previews still loading */
    for (i = 0; i < 12; i++) tick();
    CHECK(st().state == NP_STATE_ARMED);

    fg.host[0].buttons = 0x10;
    tick();
    CHECK(fg.slot[1].buttons == 0x10);                           /* pre-session mirror */

    fg.posed = 1;
    for (i = 0; i < 5; i++) tick();
    g_io++;                                                      /* a file read: start over */
    for (i = 0; i < 10; i++) tick();                             /* the reset frame, then 9 quiet */
    CHECK(st().state == NP_STATE_ARMED);
    CHECK(g_det_begin == 0);
    tick();                                                      /* the 10th quiet frame */
    CHECK(st().state == NP_STATE_ACTIVE);
    CHECK(netplay_session_frame() == 1);                         /* frame 0 ran in the same tick */
    CHECK(g_det_begin == 1 && g_apu_begin == 1);
    {   /* Pins (passed on first run): the game's rand() seed is set at frame 0, not only at the first screen change:
         * the character select draws random idle animations before any transition, and a
         * joiner or replay with its own boot's seed picked different ones (2026-10-26). */
        uint32_t seed; memcpy(&seed, fg_mem(FG_SEED), 4);
        CHECK(seed != 12345u);
        CHECK(fg.worker_seed != 0);
    }
    CHECK(fg.forced == NP_SESSION_PADS && fg.force_calls == 1);
    CHECK(fg.packet[0] == 0x1000u && fg.packet[1] == 0x1000u);   /* frame 0's packet */
    CHECK(fg.slot[1].buttons == fg.slot[0].buttons);             /* in-session mirror */
    fg.pause_owner = 0;                                          /* the pause menu belongs to one pad: the */
    fg.host[0].buttons = 0x02;                                   /* other pad pressing the same keys makes */
    tick();                                                      /* the game show its reconnect prompt */
    CHECK(fg.slot[0].buttons == 0x02 && fg.slot[1].buttons == 0);
    fg.pause_owner = 1;                                          /* Player 2 took the (mirrored) Start press */
    tick();
    CHECK(fg.slot[0].buttons == 0 && fg.slot[1].buttons == 0x02);
    fg.pause_owner = -1;
    fg.host[0].buttons = 0;
    CHECK(fg_mem(FG_SAVE)[26] == 1 && fg_mem(FG_SAVE)[27] == 7);  /* the match runs unlocked */
    CHECK(fg.redirect == 1);                                      /* the host's saves go aside too */

    netplay_set_armed(0, NP_ROLE_HOST);
    tick();
    CHECK(st().state == NP_STATE_OFF && g_det_end == 1 && g_apu_end == 1);
    CHECK(!memcmp(fg_mem(FG_SAVE), before, FG_SAVE_LEN));         /* the profile is as it was */
    CHECK(fg.redirect == 0);
    netplay_set_mirror(0);
}

static int find_replay(char *out, size_t cap)
{
    char names[8][128];
    int n = netplay_list_replays(names, 8);
    if (n != 1) return 0;
    strncpy(out, names[0], cap - 1);
    out[cap - 1] = 0;
    return 1;
}

static void test_record_and_replay(void)
{
    char name[128];
    np_status s;
    uint32_t frames;

    fake_game_reset();
    netplay_set_record(1);
    netplay_set_armed(1, NP_ROLE_HOST);
    CHECK(reach_barrier());
    CHECK(play_match(-1));
    s = st();
    CHECK(strncmp(s.message, "Recorded netplay_replay_", 24) == 0);
    netplay_set_armed(0, NP_ROLE_HOST);
    netplay_set_record(0);
    tick();
    CHECK(find_replay(name, sizeof name));
    frames = 0;

    /* Replay on a game that has moved on since: the start parameters put the
     * regions, the save image and the seed back. */
    reset_screens();
    fg_mem(FG_FIGHTERS)[0] = 0x77;
    netplay_request_replay(name);
    tick();
    s = st();
    CHECK(strncmp(s.message, "Replay ready (Single Battle", 27) == 0);
    frames = s.replay_frames;
    CHECK(frames >= 30);
    CHECK(reach_barrier());
    CHECK(play_match(-1));
    s = st();
    CHECK(strncmp(s.message, "Replay matched the recording for all", 36) == 0);
    CHECK(s.diverged_at == NP_NO_FRAME);
    CHECK(netplay_armed() == 0);                                 /* a replay runs once */

    /* The same replay with one fighter byte flipped before frame 12 is
     * recorded: the state entering frame 12 differs, in the fighters region. */
    reset_screens();
    netplay_request_replay(name);
    tick();
    CHECK(reach_barrier());
    CHECK(play_match(12));
    s = st();
    CHECK(s.diverged_at == 12);
    CHECK(strstr(s.message, "DIVERGED at frame 12 (fighters") != NULL);

    remove(name);
}

/* Lockstep from boot (spec section 12): requested before the guest runs (saves
 * go aside and movies are skipped at once: movie playback stalls on
 * frame-counted time, 2026-10-27), the clocks and the chip armed at the first
 * pad-builder call, the regions captured there and put back by a replay, the
 * pad history cleared; leaving Versus does not end it; a recording from boot
 * replays from boot. */
static void test_boot_session(void)
{
    char name[128];
    uint8_t before[FG_SAVE_LEN];
    uint32_t seed;
    np_status s;
    int i, det0 = g_det_begin, apu0 = g_apu_begin;

    fake_game_reset();
    reset_screens();
    memcpy(before, fg_mem(FG_SAVE), FG_SAVE_LEN);
    netplay_set_record(1);
    CHECK(netplay_boot_begin(0x1234u, NULL));
    CHECK(st().state == NP_STATE_ACTIVE);
    CHECK(g_det_begin == det0 && g_apu_begin == apu0);             /* not before the first frame */
    CHECK(fg.redirect == 1);                                      /* saves aside from the start */
    CHECK(netplay_skip_movies() == 1);                            /* the intro never plays */
    CHECK(netplay_session_frame() == 0);
    memcpy(&seed, fg_mem(FG_SEED), 4);
    CHECK(seed == 12345u);                                        /* no guest write before frame 0 */
    fg_mem(FG_FIGHTERS)[3] = 0x5A;                                /* boot left this behind */
    fg_mem(FG_PADS)[0] = 0x10;                                    /* a press before the session's frame 0 */
    fg.screen = 0;                                                /* the first frame-loop tick has no screen yet */
    script(0);
    tick();                                                       /* frame 0 */
    CHECK(netplay_session_frame() == 1);
    CHECK(g_det_begin == det0 + 1 && g_apu_begin == apu0 + 1);     /* armed at frame 0 */
    CHECK(fg.worker_seed != 0);
    memcpy(&seed, fg_mem(FG_SEED), 4);
    CHECK(seed == 0x1234u);                                       /* the seed goes in at frame 0 */
    for (i = 1; i < 40; i++) { script(netplay_session_frame()); tick(); }
    CHECK(st().state == NP_STATE_ACTIVE);                         /* frame 30 left Versus: no end */
    CHECK(netplay_session_frame() == 40);
    CHECK(!memcmp(fg_mem(FG_SAVE) + 26, before + 26, 2));         /* no profile yet: image untouched */
    {
        int r0;
        for (i = 30; i < 40; i++) fg_mem(FG_SAVE)[i] = (uint8_t)(0x40 + i);   /* the game loads the profile */
        script(40);
        tick();
        CHECK(fg_mem(FG_SAVE)[26] == 1 && fg_mem(FG_SAVE)[27] == 7);  /* every character and costume */
        r0 = fg.rebuilds;
        script(41);
        tick();
        CHECK(fg.rebuilds == r0);                                 /* unchanged image: not applied again */
        fg_mem(FG_SAVE)[26] = 0;                                  /* the game rewrites the image */
        script(42);
        tick();
        CHECK(fg_mem(FG_SAVE)[26] == 1 && fg.rebuilds == r0 + 1); /* applied again */
    }
    netplay_set_armed(0, NP_ROLE_HOST);
    tick();
    CHECK(st().state == NP_STATE_OFF);
    CHECK(netplay_skip_movies() == 0);                            /* out of a session movies play */
    s = st();
    CHECK(strncmp(s.message, "Recorded netplay_replay_", 24) == 0);
    netplay_set_record(0);
    CHECK(find_replay(name, sizeof name));

    /* The same boot, replayed on a boot that left something else behind: the
     * regions captured at frame 0 go back, and it matches to the end. */
    fake_game_reset();
    reset_screens();
    fg_mem(FG_FIGHTERS)[3] = 0xC3;
    CHECK(netplay_boot_begin(0, name));
    for (i = 0; i < 80 && st().state == NP_STATE_ACTIVE; i++) { script(netplay_session_frame()); tick(); }
    s = st();
    CHECK(s.state == NP_STATE_OFF);
    if (strncmp(s.message, "Replay matched", 14)) fprintf(stderr, "boot replay: %s\n", s.message);
    CHECK(strncmp(s.message, "Replay matched the recording for all", 36) == 0);
    remove(name);
}

/* Counters in the digest (review L2): the game thread's rand() calls, the guest
 * kernel calls and the audio chip's stepped frames are part of every frame's
 * digest, in a region named "counters", so a replay (and an online peer) that
 * ran different guest code - or stepped the chip a different number of times -
 * is caught at that frame even when no hashed memory differs yet. Each counter
 * is perturbed alone, at frame 10 of a replay, with no memory change: the
 * replay must say it diverged at frame 10 in "counters". */
static void boot_record_20(char *name, size_t cap)
{
    int i;
    fake_game_reset();
    reset_screens();
    g_kcalls = 0; g_apu_chip = 0;
    netplay_set_record(1);
    CHECK(netplay_boot_begin(0x4321u, NULL));
    for (i = 0; i < 20; i++) { g_kcalls += 7; g_apu_chip += 25; script(netplay_session_frame()); tick(); }
    netplay_set_armed(0, NP_ROLE_HOST);
    g_kcalls += 7; g_apu_chip += 25;                              /* the ending tick records frame 20 too */
    script(netplay_session_frame());
    tick();
    netplay_set_record(0);
    CHECK(find_replay(name, cap));
}

static np_status boot_replay_20(const char *name, int which)
{
    int i;
    fake_game_reset();
    reset_screens();
    g_kcalls = 0; g_apu_chip = 0;
    CHECK(netplay_boot_begin(0, name));
    for (i = 0; i < 40 && st().state == NP_STATE_ACTIVE; i++) {
        uint32_t f = netplay_session_frame();
        g_kcalls += 7; g_apu_chip += 25;
        if (f == 10) {
            if (which == 1) g_kcalls += 1;                       /* one more guest kernel call */
            if (which == 2) g_apu_chip += 1;                     /* one more chip frame (a retire-wait step) */
            if (which == 3) netplay_note_rand();                 /* one more game-thread rand() call */
        }
        script(f);
        tick();
    }
    return st();
}

static void test_counters_in_digest(void)
{
    char name[128];
    np_status s;
    int which;
    boot_record_20(name, sizeof name);
    s = boot_replay_20(name, 0);                                  /* control: nothing perturbed */
    CHECK(strncmp(s.message, "Replay matched the recording for all", 36) == 0);
    for (which = 1; which <= 3; which++) {
        s = boot_replay_20(name, which);
        if (s.diverged_at != 10 || !strstr(s.message, "(counters"))
            fprintf(stderr, "counter %d: %s\n", which, s.message);
        CHECK(s.diverged_at == 10);
        CHECK(strstr(s.message, "DIVERGED at frame 10 (counters") != NULL);
    }
    remove(name);
}

static void test_ring(void)
{
    uint32_t h[NP_MAX_REGIONS];
    int i;
    fake_game_reset();
    netplay_set_armed(1, NP_ROLE_HOST);
    CHECK(reach_barrier());
    fg.in_fight = 1;
    for (i = 0; i < 299; i++) tick();
    CHECK(netplay_session_frame() == 300);
    CHECK(netplay_session_frame_regions(299, h, NP_MAX_REGIONS) == netplay_session_region_count());
    CHECK(netplay_session_frame_regions(44, h, NP_MAX_REGIONS) == 6);   /* 2 sync + 2 extra + seedclock + counters */
    CHECK(netplay_session_frame_regions(43, h, NP_MAX_REGIONS) == 0);   /* older than 256 frames */
    CHECK(netplay_session_frame_regions(300, h, NP_MAX_REGIONS) == 0);  /* not yet */
    CHECK(strcmp(netplay_session_region_name(0), "fighters") == 0);
    CHECK(strcmp(netplay_session_region_name(4), "seedclock") == 0);
    CHECK(strcmp(netplay_session_region_name(5), "counters") == 0);
    netplay_set_armed(0, NP_ROLE_HOST);
    tick();
}

/* ── provider ────────────────────────────────────────────────────────── */

static uint8_t  s_blob[4096];
static uint32_t s_blob_len;
static int      s_barrier_calls, s_ends, s_transitions[4], s_ntrans, s_digests;
static uint32_t s_last_digest_frame;

static int pv_barrier(int at_barrier, int tag)
{
    (void)tag;
    s_barrier_calls++;
    if (!at_barrier) return 0;
    return netplay_params_load(s_blob, s_blob_len) ? 1 : -1;
}

static int s_exchange_fail;
static int pv_exchange(const np_pad *local, np_pad out[2])
{
    if (s_exchange_fail) return 0;
    out[0] = *local;
    memset(&out[1], 0, sizeof out[1]);
    out[1].buttons = 0x4242;
    return 1;
}

static void pv_digest(uint32_t frame, uint64_t digest) { (void)digest; s_digests++; s_last_digest_frame = frame; }
static void pv_end(void) { s_ends++; }
static int  pv_transition(uint32_t index) { if (s_ntrans < 4) s_transitions[s_ntrans++] = (int)index; return 1; }

static void test_provider(void)
{
    static const np_provider pv = { pv_barrier, pv_exchange, pv_digest, pv_end, pv_transition };
    uint8_t joiner_layout[NP_LAYOUT_MAX] = { 9, 8, 7, 6 }, local_save[FG_SAVE_LEN];
    const uint8_t *blob;
    uint32_t len;
    int i;

    s_barrier_calls = s_ends = s_ntrans = s_digests = 0;          /* earlier tests use the same provider */
    /* the host's machine: fighters pattern A, its own save image */
    fake_game_reset();
    for (i = 0; i < 16; i++) fg_mem(FG_FIGHTERS)[i] = (uint8_t)(0xA0 + i);
    for (i = 0; i < (int)FG_SAVE_LEN; i++) fg_mem(FG_SAVE)[i] = (uint8_t)(0x30 + i);
    CHECK(netplay_params_capture(&blob, &len, joiner_layout, 3));
    CHECK(len <= sizeof s_blob);
    memcpy(s_blob, blob, len);
    s_blob_len = len;

    /* the joiner's machine: other fighters, other save image and volumes */
    fake_game_reset();
    for (i = 0; i < 16; i++) fg_mem(FG_FIGHTERS)[i] = (uint8_t)(0x50 + i);
    for (i = 0; i < (int)FG_SAVE_LEN; i++) fg_mem(FG_SAVE)[i] = (uint8_t)(0x90 + i);
    memcpy(local_save, fg_mem(FG_SAVE), FG_SAVE_LEN);

    netplay_set_provider(&pv);
    netplay_set_armed(1, NP_ROLE_JOINER);
    CHECK(netplay_forced_pads() == NP_SESSION_PADS);              /* online lobby */
    CHECK(reach_barrier());
    CHECK(s_barrier_calls >= 10);
    for (i = 0; i < 16; i++) CHECK(fg_mem(FG_FIGHTERS)[i] == (uint8_t)(0xA0 + i));   /* host's state */
    CHECK(fg_mem(FG_SAVE)[0] == 0x90 && fg_mem(FG_SAVE)[1] == 0x91);                  /* own volumes */
    CHECK(fg_mem(FG_SAVE)[2] == 0x32);                                                 /* host's settings */
    CHECK(fg_mem(FG_SAVE)[26] == 1 && fg_mem(FG_SAVE)[27] == 7);                      /* unlocked, from the host's capture */
    CHECK(!memcmp(fg_mem(FG_SAVE + 12), joiner_layout, FG_LAYOUT_LEN));               /* port 1 = joiner layout */
    CHECK(fg_mem(FG_SAVE + 25)[0] == 3);
    CHECK(fg.redirect == 1);
    CHECK(fg.slot[1].buttons == 0x4242);                          /* the exchange's pads went in */

    fg.screen = 7;                                               /* a screen change */
    tick();
    CHECK(s_ntrans == 1 && s_transitions[0] == 1);
    CHECK(s_digests == 2 && s_last_digest_frame == 1);

    fg.left = 1;
    tick();
    CHECK(s_ends == 1);
    CHECK(fg.redirect == 0);
    CHECK(!memcmp(fg_mem(FG_SAVE), local_save, FG_SAVE_LEN));    /* the joiner's own image back */
    netplay_set_armed(0, NP_ROLE_HOST);
    tick();
    netplay_set_provider(NULL);
}

/* Restart-on-join, the new process (spec section 12): the session is asked
 * for before the guest runs and waits, armed, at the first frame-loop tick
 * while the connection comes up (the pump), then runs the provider's start
 * handshake there; the joiner takes the host's carried regions, not its own,
 * and its own save image stays as loaded (the profile is not up yet). If the
 * connection fails the game carries on offline with saves still aside. */
static int s_pump_calls, s_pump_fail;
static const np_provider *s_pump_provider;
static int fake_pump(void)
{
    s_pump_calls++;
    if (s_pump_fail) return -1;
    if (s_pump_calls == 3) netplay_set_provider(s_pump_provider);   /* connected on the third pump */
    return 0;
}

static void test_boot_online(void)
{
    static const np_provider pv = { pv_barrier, pv_exchange, pv_digest, pv_end, pv_transition };
    uint8_t joiner_layout[NP_LAYOUT_MAX] = { 9, 8, 7, 6 };
    const uint8_t *blob;
    uint32_t len;
    int i, det0, ends0;

    /* the host's game at its frame 0: fighters pattern A */
    fake_game_reset();
    for (i = 0; i < 16; i++) fg_mem(FG_FIGHTERS)[i] = (uint8_t)(0xA0 + i);
    CHECK(netplay_params_capture(&blob, &len, joiner_layout, 3));
    memcpy(s_blob, blob, len);
    s_blob_len = len;

    /* the joiner's game: other fighters, its own save image */
    fake_game_reset();
    reset_screens();
    for (i = 0; i < 16; i++) fg_mem(FG_FIGHTERS)[i] = (uint8_t)(0x50 + i);
    for (i = 0; i < (int)FG_SAVE_LEN; i++) fg_mem(FG_SAVE)[i] = (uint8_t)(0x90 + i);
    netplay_set_provider(NULL);
    s_pump_calls = 0; s_pump_fail = 0; s_pump_provider = &pv;
    netplay_set_boot_pump(fake_pump);
    det0 = g_det_begin; ends0 = s_ends;
    CHECK(netplay_boot_online(NP_ROLE_JOINER, 0));
    CHECK(st().state == NP_STATE_ARMED);                          /* waiting, not running */
    CHECK(netplay_skip_movies() == 1);                            /* no intro while it waits */
    CHECK(fg.redirect == 1);
    CHECK(g_det_begin == det0);

    script(0);
    tick();                                                       /* the first tick: waits, then frame 0 */
    CHECK(s_pump_calls >= 3);
    CHECK(st().state == NP_STATE_ACTIVE);
    CHECK(netplay_session_frame() == 1);
    CHECK(g_det_begin == det0 + 1);
    for (i = 0; i < 16; i++) CHECK(fg_mem(FG_FIGHTERS)[i] == (uint8_t)(0xA0 + i));   /* the host's regions */
    CHECK(fg_mem(FG_SAVE)[2] == 0x92);                            /* its own image, untouched */
    CHECK(fg.slot[1].buttons == 0x4242);                          /* pads from the exchange */
    for (i = 30; i < 40; i++) fg_mem(FG_SAVE)[i] = (uint8_t)(0x40 + i);   /* both load the host's profile */
    tick();
    CHECK(fg_mem(FG_SAVE)[26] == 1 && fg_mem(FG_SAVE)[27] == 7);  /* unlocked */
    CHECK(!memcmp(fg_mem(FG_SAVE + 12), joiner_layout, FG_LAYOUT_LEN));   /* port 1: the joiner's layout */
    CHECK(fg_mem(FG_SAVE + 25)[0] == 3);
    CHECK(fg_mem(FG_SAVE)[0] == 0x90 && fg_mem(FG_SAVE)[1] == 0x91);      /* its own volumes */
    fg.left = 1;
    tick();
    CHECK(st().state == NP_STATE_ACTIVE);                         /* leaving Versus does not end it */
    {   /* the exchange fails (a desync, a lost peer): the session ends and stays
         * ended; the character-select meeting point is not offered again
         * (final review C1: it desyncs between different-history games) */
        int calls0 = s_barrier_calls;
        s_exchange_fail = 1;
        tick();
        CHECK(st().state == NP_STATE_OFF && s_ends == ends0 + 1);
        CHECK(netplay_armed() == 0);
        fg.left = 0; fg.at_charsel = 1; fg.posed = 1;
        for (i = 0; i < 40; i++) tick();
        CHECK(st().state == NP_STATE_OFF && s_barrier_calls == calls0);
        s_exchange_fail = 0;
        reset_screens();
    }
    netplay_set_provider(NULL);

    /* the connection never comes: offline, saves still aside */
    fake_game_reset();
    reset_screens();
    s_pump_calls = 0; s_pump_fail = 1;
    CHECK(netplay_boot_online(NP_ROLE_JOINER, 0));
    tick();
    CHECK(st().state == NP_STATE_OFF);
    CHECK(netplay_skip_movies() == 0);
    CHECK(fg.redirect == 1);
    netplay_set_boot_pump(NULL);
}

int main(void)
{
    test_barrier_and_mirror();
    test_record_and_replay();
    test_boot_session();
    test_counters_in_digest();
    test_boot_online();
    test_ring();
    test_provider();
    {   /* digest logs written by the replays */
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA("netplay_digest_*.txt", &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do DeleteFileA(fd.cFileName); while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    printf("netplay_session_test: %d failure(s)\n", failures);
    return failures;
}
