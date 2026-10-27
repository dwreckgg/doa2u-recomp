/*
 * netplay_session.c - lockstep session core for online play (see header).
 *
 * Ported from parkerallan/doa3-recomp (MIT), doa3/src/online/
 * netplay_session.c at 919c49f. The state machine, the barrier, the
 * transition reseed, the digest, recording and replay are DOA3's; every
 * guest address and guest call is behind netplay_game.h (doa2u_game.c), and
 * the byte formats are in netplay_format.c.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netplay_session.h"
#include "netplay_game.h"
#include "online_settings.h"
#include "kernel/xbox_det.h"

void     mcpx_apu_det_begin(uint64_t xgscnt_base);   /* apu_core.c: chip stepped per frame */
uint64_t mcpx_apu_xgscnt_now(void);
void     mcpx_apu_det_end(void);
void     mcpx_apu_det_frame(void);

#define BARRIER_QUIET_FRAMES 10      /* no guest file activity for this long at character select */
#define REC_RING             256u    /* frames kept when not recording */

typedef struct {
    uint32_t frame;
    uint64_t digest;
    uint32_t region[NP_MAX_REGIONS];
    np_pad   in[2];
    uint32_t screen;
    uint32_t kcalls, boost;
    uint16_t rand_game, rand_workers;
} np_frame_rec;

static volatile int s_armed, s_mirror, s_record;
static volatile int s_role = NP_ROLE_HOST;
static volatile LONG s_req_replay;
static char      s_req_path[260];
static int       s_state = NP_STATE_OFF;
static const np_provider *s_provider;
static int       s_abort;
static char      s_end_note[200];
static char      s_msg[200];
static char      s_last_log[260];
static char      s_last_replay[260];

static np_replay s_rp;
static int       s_rp_loaded;
static int       s_replaying;
static uint32_t  s_div_frame = NP_NO_FRAME;
static int       s_div_region = -1;
static uint32_t  s_div_rec, s_div_now;

static int       s_active_role, s_tag;
static uint32_t  s_frame, s_sessions;
static uint64_t  s_digest;
static np_params s_params;
static uint8_t  *s_save_backup;
static int       s_have_backup;
static uint32_t  s_seed_va;
static uint32_t  s_rand_game, s_rand_workers;
static uint32_t  s_quiet_frames, s_quiet_io;
static uint32_t  s_last_screen, s_transitions;

static np_frame_rec *s_rec;
static uint32_t  s_rec_n, s_rec_cap;
static int       s_keep_all;          /* record, replay or diagnostics: every frame is kept */

static uint8_t  *s_blob;              /* last captured start parameters / sync set */
static uint32_t  s_blob_len, s_blob_cap;
static uint8_t  *s_scratch;           /* region bytes for hashing */
static uint32_t  s_scratch_len;

static int       s_tables_checked, s_tables_ok;
static int       s_boot;              /* armed before the guest's entry (spec section 12) */
static int       s_boot_armed;        /* clocks, chip and seeds in (frame 0 of a boot session) */
static int       s_boot_online, s_boot_battle;   /* restart-on-join: waits for the provider at frame 0 */
static int     (*s_boot_pump)(void);
static uint32_t  s_profile_hash;      /* boot sessions: the save image as last seen */

/* ── tables ─────────────────────────────────────────────────────────── */

static int n_hash(void)
{
    const np_game_desc *g = np_game();
    return g->n_sync + g->n_extra + 1;
}

static int tables_ok(void)
{
    const np_game_desc *g = np_game();
    char why[160];
    int i;
    if (s_tables_checked) return s_tables_ok;
    s_tables_checked = 1;
    s_tables_ok = 0;
    if (n_hash() > NP_MAX_REGIONS) {
        _snprintf(s_msg, sizeof(s_msg) - 1, "Too many state regions (%d, at most %d)", n_hash(), NP_MAX_REGIONS);
        return 0;
    }
    if (g->layout_len > NP_LAYOUT_MAX) {
        _snprintf(s_msg, sizeof(s_msg) - 1, "Button layout is %u bytes, at most %u", g->layout_len, NP_LAYOUT_MAX);
        return 0;
    }
    if (!np_regions_check(g->sync, g->n_sync, g->ram_lo, g->ram_hi, why, sizeof why) ||
        !np_regions_check(g->extra, g->n_extra, g->ram_lo, g->ram_hi, why, sizeof why)) {
        _snprintf(s_msg, sizeof(s_msg) - 1, "State regions rejected: %s", why);
        return 0;
    }
    s_scratch_len = 0;
    for (i = 0; i < g->n_sync; i++) if (g->sync[i].len > s_scratch_len) s_scratch_len = g->sync[i].len;
    for (i = 0; i < g->n_extra; i++) if (g->extra[i].len > s_scratch_len) s_scratch_len = g->extra[i].len;
    s_scratch = (uint8_t *)malloc(s_scratch_len ? s_scratch_len : 1);
    s_save_backup = (uint8_t *)malloc(g->save_len ? g->save_len : 1);
    s_tables_ok = s_scratch && s_save_backup;
    return s_tables_ok;
}

static const char *region_name(int k)
{
    const np_game_desc *g = np_game();
    if (k < 0) return "?";
    if (k < g->n_sync) return g->sync[k].name;
    if (k < g->n_sync + g->n_extra) return g->extra[k - g->n_sync].name;
    return "seedclock";
}

static uint32_t region_hash(const np_region *r)
{
    np_game_read(r->va, s_scratch, r->len);
    return np_fnv32(NP_FNV32_INIT, s_scratch, r->len);
}

static uint32_t sync_total(void)
{
    const np_game_desc *g = np_game();
    return np_regions_total(g->sync, g->n_sync);
}

static void sync_read(uint8_t *dst)
{
    const np_game_desc *g = np_game();
    uint32_t off = 0;
    int i;
    for (i = 0; i < g->n_sync; i++) { np_game_read(g->sync[i].va, dst + off, g->sync[i].len); off += g->sync[i].len; }
}

static void sync_write(const uint8_t *src)
{
    const np_game_desc *g = np_game();
    uint32_t off = 0;
    int i;
    for (i = 0; i < g->n_sync; i++) { np_game_write(g->sync[i].va, src + off, g->sync[i].len); off += g->sync[i].len; }
}

static int blob_reserve(uint32_t n)
{
    if (n > s_blob_cap) {
        uint8_t *b = (uint8_t *)realloc(s_blob, n);
        if (!b) return 0;
        s_blob = b;
        s_blob_cap = n;
    }
    s_blob_len = n;
    return 1;
}

/* ── frame records ──────────────────────────────────────────────────── */

static np_frame_rec *rec_new(uint32_t frame)
{
    uint32_t want = s_keep_all ? frame + 1 : REC_RING;
    if (want > s_rec_cap) {
        uint32_t cap = s_rec_cap ? s_rec_cap : REC_RING;
        np_frame_rec *n;
        while (cap < want) cap *= 2;
        if (cap > NP_REPLAY_MAX_FRAMES) cap = NP_REPLAY_MAX_FRAMES;
        if (cap < want) return NULL;                       /* an hour is the limit */
        n = (np_frame_rec *)realloc(s_rec, (size_t)cap * sizeof(*n));
        if (!n) return NULL;
        s_rec = n;
        s_rec_cap = cap;
    }
    return &s_rec[s_keep_all ? frame : frame % REC_RING];
}

static const np_frame_rec *rec_get(uint32_t frame)
{
    const np_frame_rec *r;
    if (!s_rec || frame >= s_rec_n) return NULL;
    if (!s_keep_all && s_rec_n - frame > REC_RING) return NULL;
    r = &s_rec[s_keep_all ? frame : frame % REC_RING];
    return r->frame == frame ? r : NULL;
}

static void record_frame(const np_pad in[2])
{
    const np_game_desc *g = np_game();
    np_frame_rec *r = rec_new(s_frame);
    np_frame_rec tmp;
    int in_fight = np_game_in_fight(), k, nh = n_hash();
    if (!r) r = &tmp;                                      /* past the limit: digest only */
    r->frame = s_frame;
    /* Menus: nothing from the sync set is compared. The selected character
     * is written when a preview model finishes loading, which lands a frame
     * apart on two machines' disks; the transition resync makes both sides
     * identical again before the fight. */
    for (k = 0; k < g->n_sync; k++) r->region[k] = in_fight ? region_hash(&g->sync[k]) : 0;
    for (k = 0; k < g->n_extra; k++) r->region[g->n_sync + k] = region_hash(&g->extra[k]);
    {
        uint32_t v[3] = { 0, 0, 0 };
        if (s_seed_va) np_game_read(s_seed_va, &v[0], 4);   /* every frame: the menus draw from it too */
        v[1] = xbox_det_tick_ms();
        v[2] = (uint32_t)xbox_det_filetime();
        r->region[nh - 1] = np_fnv32(NP_FNV32_INIT, v, sizeof v);
    }
    r->digest = np_digest_combine(r->region, nh);
    r->in[0] = in[0];
    r->in[1] = in[1];
    r->screen = np_game_screen_key();
    r->kcalls = xbox_det_kcalls();
    r->boost = (uint32_t)(xbox_det_boost() / 733333u);
    r->rand_game = (uint16_t)s_rand_game;
    r->rand_workers = (uint16_t)s_rand_workers;
    s_rand_game = s_rand_workers = 0;
    s_digest = r->digest;
    if (r != &tmp) s_rec_n = s_frame + 1;
}

/* ── files ──────────────────────────────────────────────────────────── */

static uint32_t build_stamp(void)
{
    const uint8_t *base = (const uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

static void stamp_name(char *out, size_t cap, const char *prefix, const char *suffix, const char *ext)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    _snprintf(out, cap - 1, "%s_%04u%02u%02u_%02u%02u%02u_%s.%s", prefix,
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, suffix, ext);
    out[cap - 1] = 0;
}

static void write_log(void)
{
    FILE *f;
    uint32_t i;
    int k, nh = n_hash();
    stamp_name(s_last_log, sizeof s_last_log, "netplay_digest",
               s_active_role == NP_ROLE_JOINER ? "joiner" : "host", "txt");
    f = fopen(s_last_log, "w");
    if (!f) { s_last_log[0] = 0; return; }
    fprintf(f, "# DOA2U netplay session digest\n");
    fprintf(f, "# role=%s battle=%s frames=%u seed=%08X tick_base=%u qpc_base=%llu ft_base=%llu\n",
            s_active_role == NP_ROLE_JOINER ? "joiner" : "host", s_tag ? "tag" : "single",
            s_rec_n, s_params.seed, s_params.tick_ms,
            (unsigned long long)s_params.qpc, (unsigned long long)s_params.filetime);
    if (s_end_note[0]) { fprintf(f, "# ended: %s\n", s_end_note); s_end_note[0] = 0; }
    if (s_replaying) {
        fprintf(f, "# replay of %s\n", s_req_path);
        if (s_div_frame == NP_NO_FRAME)
            fprintf(f, "# replay matched the recording for all %u frames\n", s_rec_n);
        else
            fprintf(f, "# replay DIVERGED at frame %u in %s (recorded %08X, now %08X)\n",
                    s_div_frame, region_name(s_div_region), s_div_rec, s_div_now);
    }
    fprintf(f, "# frame digest");
    for (k = 0; k < nh; k++) fprintf(f, " %s", region_name(k));
    fprintf(f, " p1 p2 screen kcalls boost rand_game rand_workers\n");
    for (i = 0; i < s_rec_n; i++) {
        const np_frame_rec *r = rec_get(i);
        if (!r) continue;
        fprintf(f, "%u %016llX", r->frame, (unsigned long long)r->digest);
        for (k = 0; k < nh; k++) fprintf(f, " %08X", r->region[k]);
        fprintf(f, " %04X %04X %08X %u %u %u %u\n", r->in[0].buttons, r->in[1].buttons,
                r->screen, r->kcalls, r->boost, r->rand_game, r->rand_workers);
    }
    fclose(f);
}

static void write_replay(void)
{
    np_replay r;
    FILE *f;
    uint32_t i;
    int k, nh = n_hash();
    memset(&r, 0, sizeof r);
    stamp_name(s_last_replay, sizeof s_last_replay, "netplay_replay", s_tag ? "tag" : "single", "dnr");
    r.build_stamp = build_stamp();
    r.role = s_active_role;
    r.tag = s_tag;
    r.params = s_params;                                   /* borrowed, not freed below */
    r.n_hash = (uint32_t)nh;
    r.frames = s_rec_n;
    r.pads = (np_pad *)malloc((size_t)s_rec_n * 2 * sizeof(np_pad));
    r.digests = (uint64_t *)malloc((size_t)s_rec_n * sizeof(uint64_t));
    r.hashes = (uint32_t *)malloc((size_t)s_rec_n * nh * sizeof(uint32_t));
    f = (r.pads && r.digests && r.hashes) ? fopen(s_last_replay, "wb") : NULL;
    if (f) {
        for (i = 0; i < s_rec_n; i++) {
            r.pads[2 * i] = s_rec[i].in[0];
            r.pads[2 * i + 1] = s_rec[i].in[1];
            r.digests[i] = s_rec[i].digest;
            for (k = 0; k < nh; k++) r.hashes[(size_t)i * nh + k] = s_rec[i].region[k];
        }
        if (!np_replay_write(f, &r)) s_last_replay[0] = 0;
        fclose(f);
    } else {
        s_last_replay[0] = 0;
    }
    free(r.pads);
    free(r.digests);
    free(r.hashes);
}

/* ── start parameters ───────────────────────────────────────────────── */

static int params_copy(np_params *dst, const np_params *src)
{
    np_params t = *src;
    t.save = (uint8_t *)malloc(src->save_len ? src->save_len : 1);
    t.sync = (uint8_t *)malloc(src->sync_len ? src->sync_len : 1);
    if (!t.save || !t.sync) { free(t.save); free(t.sync); return 0; }
    memcpy(t.save, src->save, src->save_len);
    memcpy(t.sync, src->sync, src->sync_len);
    np_params_free(dst);
    *dst = t;
    return 1;
}

/* Parameters made on this machine: a local session (port 1 keeps its own
 * layout) or the host's side of an online one (port 1 carries the joiner's
 * layout, received over the network). */
static int make_local_params(np_params *p, const uint8_t *joiner_layout, uint8_t joiner_laysel)
{
    const np_game_desc *g = np_game();
    LARGE_INTEGER q;
    xbox_det_bases b;
    np_params_free(p);
    memset(p, 0, sizeof(*p));
    QueryPerformanceCounter(&q);
    p->seed = (uint32_t)q.QuadPart ^ (uint32_t)(q.QuadPart >> 32) ^ GetTickCount();
    xbox_det_current_bases(&b);
    p->tick_ms = b.tick_ms;
    p->qpc = b.qpc;
    p->filetime = b.filetime;
    p->xgscnt = mcpx_apu_xgscnt_now();
    p->save_len = g->save_len;
    p->save = (uint8_t *)malloc(g->save_len ? g->save_len : 1);
    p->sync_len = sync_total();
    p->sync = (uint8_t *)malloc(p->sync_len ? p->sync_len : 1);
    if (!p->save || !p->sync) { np_params_free(p); return 0; }
    np_game_save_read(p->save);
    np_game_save_unlock_all(p->save);                      /* the match runs with the full roster */
    if (joiner_layout) {
        memcpy(p->joiner_layout, joiner_layout, NP_LAYOUT_MAX);
        p->joiner_laysel = joiner_laysel;
    } else {
        np_game_layout_read(1, p->joiner_layout, &p->joiner_laysel);
    }
    sync_read(p->sync);
    return 1;
}

static void apply_params(const np_params *p, int role)
{
    (void)role;
    /* Both machines: the live image is replaced for the match (the host's
     * settings, every character and costume unlocked), the game's saves go
     * to a side folder meanwhile, and the player's own image comes back at
     * the end, so a session never changes anyone's profile. */
    np_game_save_read(s_save_backup);
    s_have_backup = 1;
    np_game_title_redirect(1);
    np_game_save_apply(p->save, p->joiner_layout, p->joiner_laysel);
    sync_write(p->sync);
    s_seed_va = np_game_rand_seed_va();
    if (s_seed_va) np_game_write(s_seed_va, &p->seed, 4);
    np_game_worker_rand_seed(p->seed ^ 0x5A5A5A5Au);
    np_game_force_pads(NP_SESSION_PADS);
}

/* ── session start / end ────────────────────────────────────────────── */

/* The session may only start once character select has settled: opening it
 * starts loading both cursor characters' preview models on worker time, so
 * a barrier on the first character-select frame catches the load at a
 * different point on every machine. Settled = both previews posed and no
 * guest file open/read for BARRIER_QUIET_FRAMES. */
static int at_barrier(void)
{
    uint32_t io = xbox_det_io_count();
    if (!np_game_at_charsel() || !np_game_previews_posed() || io != s_quiet_io) {
        s_quiet_io = io;
        s_quiet_frames = 0;
        return 0;
    }
    return ++s_quiet_frames >= BARRIER_QUIET_FRAMES;
}

/* One-controller mirror: both pads get the real pad's input, except on the
 * pause menu. That menu belongs to the pad whose Start the game took (either
 * one, since both pressed it), and the other pad pressing the same keys there
 * makes the game show "Please reconnect the controller to controller port %d"
 * and ignore the menu (measured 2026-10-26, with and without a session). So
 * while paused the owner gets the input and the other pad is neutral. */
static void mirror_pair(np_pad in[2])
{
    int owner = np_game_pause_owner();
    if (owner == 0) memset(&in[1], 0, sizeof in[1]);
    else if (owner == 1) { in[1] = in[0]; memset(&in[0], 0, sizeof in[0]); }
    else in[1] = in[0];
}

static void end_session(const char *why)
{
    if (online_get_diag())
        fprintf(stderr, "[netplay] session ended at frame %u (%s), screen key %08X\n", s_frame, why, np_game_screen_key());
    mcpx_apu_det_end();
    xbox_det_end();
    if (s_have_backup) {
        np_game_save_write(s_save_backup);
        np_game_title_redirect(0);
        s_have_backup = 0;
    }
    if (s_keep_all && (s_record || s_replaying || (s_end_note[0] && online_get_diag()))) write_log();
    else s_end_note[0] = 0;
    if (s_replaying) {
        if (s_div_frame == NP_NO_FRAME)
            _snprintf(s_msg, sizeof(s_msg) - 1, "Replay matched the recording for all %u frames", s_rec_n);
        else
            _snprintf(s_msg, sizeof(s_msg) - 1, "Replay DIVERGED at frame %u (%s: recorded %08X, now %08X)",
                      s_div_frame, region_name(s_div_region), s_div_rec, s_div_now);
        s_replaying = 0;
        s_armed = 0;                                       /* a replay runs once */
        np_replay_free(&s_rp);
        s_rp_loaded = 0;
    } else if (s_record) {
        write_replay();
        _snprintf(s_msg, sizeof(s_msg) - 1, "Recorded %s", s_last_replay[0] ? s_last_replay : "(write failed)");
    }
    s_sessions++;
    s_rec_n = 0;
    if (s_boot_online) s_armed = 0;   /* an online boot session is not offered again: the connection
                                       * layer relaunches the game (final review C1, I3) */
    s_boot = 0;
    s_boot_armed = 0;
    s_boot_online = 0;
    s_state = s_armed ? NP_STATE_ARMED : NP_STATE_OFF;
    if (s_provider) s_provider->end();
}

static void begin_session(void)
{
    s_div_frame = NP_NO_FRAME;
    s_div_region = -1;
    if (s_rp_loaded) {
        s_replaying = 1;
        s_active_role = s_rp.role;
        params_copy(&s_params, &s_rp.params);
        _snprintf(s_msg, sizeof(s_msg) - 1, "Replaying %s", s_req_path);
    } else if (s_provider) {
        s_replaying = 0;                                   /* parameters came from the provider */
        s_active_role = s_role;
        _snprintf(s_msg, sizeof(s_msg) - 1, "Online session started (%s)",
                  s_role == NP_ROLE_JOINER ? "joiner" : "host");
    } else {
        s_replaying = 0;
        s_active_role = s_role;
        make_local_params(&s_params, NULL, 0);
    }
    s_keep_all = s_record || s_replaying || online_get_diag();
    s_rec_n = 0;
    apply_params(&s_params, s_active_role);
    s_tag = np_game_is_tag() ? 1 : 0;
    s_frame = 0;
    s_last_screen = np_game_screen_key();
    s_transitions = 0;
    np_game_session_reset();
    {
        xbox_det_bases b;
        b.tick_ms = s_params.tick_ms;
        b.qpc = s_params.qpc;
        b.filetime = s_params.filetime;
        xbox_det_begin(&b);
    }
    mcpx_apu_det_begin(s_params.xgscnt);
    s_state = NP_STATE_ACTIVE;
    if (online_get_diag()) {
        fprintf(stderr, "[netplay] session started (%s, %s path%s)\n", s_tag ? "Tag" : "Single",
                s_active_role == NP_ROLE_JOINER ? "joiner" : "host", s_replaying ? ", replay" : "");
        fflush(stderr);
    }
}

/* ── lockstep from boot (spec section 12) ─────────────────────────────── */

/* Clock bases every boot session starts from (a fixed date, 2026-01-01). The
 * online host sends its own; a local boot session and its replays use these. */
#define BOOT_TICK_MS   1000u
#define BOOT_QPC       0x0000000100000000ull
#define BOOT_FILETIME  134116992000000000ull

static uint32_t save_image_hash(void)
{
    static uint8_t *img;
    const np_game_desc *g = np_game();
    if (!img) img = (uint8_t *)malloc(g->save_len ? g->save_len : 1);
    if (!img) return 0;
    np_game_save_read(img);
    return np_fnv32(NP_FNV32_INIT, img, g->save_len);
}

static void apply_session_profile(void)
{
    const np_game_desc *g = np_game();
    uint8_t *img = (uint8_t *)malloc(g->save_len ? g->save_len : 1);
    if (!img) return;
    np_game_save_read(img);
    np_game_save_unlock_all(img);
    if (s_boot_online) {
        np_game_save_apply(img, s_params.joiner_layout, s_params.joiner_laysel);
    } else {                                           /* local: port 1 keeps this machine's layout */
        uint8_t lay[NP_LAYOUT_MAX], sel = 0;
        np_game_layout_read(1, lay, &sel);
        np_game_save_apply(img, lay, sel);
    }
    free(img);
    if (online_get_diag()) {
        fprintf(stderr, "[netplay] session profile applied at frame %u (unlocked%s)\n", s_frame,
                s_boot_online ? ", joiner layout on port 1" : "");
        fflush(stderr);
    }
}

int netplay_boot_online(int role, int battle)
{
    if (!tables_ok()) return 0;
    np_params_free(&s_params);
    memset(&s_params, 0, sizeof s_params);           /* the provider's handshake fills it at frame 0 */
    s_boot = 1;
    s_boot_online = 1;
    s_boot_armed = 0;
    s_boot_battle = battle ? 1 : 0;
    s_role = role;
    s_armed = 1;
    s_replaying = 0;
    s_rp_loaded = 0;
    s_div_frame = NP_NO_FRAME;
    s_div_region = -1;
    s_keep_all = s_record || online_get_diag();
    s_rec_n = 0;
    s_frame = 0;
    s_tag = 0;
    s_transitions = 0;
    s_seed_va = 0;
    np_game_title_redirect(1);                         /* saves aside until the process exits */
    np_game_session_reset();
    s_state = NP_STATE_ARMED;
    _snprintf(s_msg, sizeof(s_msg) - 1, "Waiting for the other player's game");
    if (online_get_diag()) {
        fprintf(stderr, "[netplay] online boot session requested (%s, %s)\n", role == NP_ROLE_JOINER ? "joiner" : "host",
                s_boot_battle ? "Tag" : "Single");
        fflush(stderr);
    }
    return 1;
}

int netplay_boot_begin(uint32_t seed, const char *replay_path)
{
    const np_game_desc *g = np_game();
    if (!tables_ok()) return 0;
    np_params_free(&s_params);
    memset(&s_params, 0, sizeof s_params);
    if (replay_path) {
        FILE *f = fopen(replay_path, "rb");
        const char *err = np_replay_read(f, &s_rp, g->save_len, sync_total(), (uint32_t)n_hash());
        if (f) fclose(f);
        if (err) {
            _snprintf(s_msg, sizeof(s_msg) - 1, "Boot replay not loaded: %s", err);
            return 0;
        }
        strncpy(s_req_path, replay_path, sizeof s_req_path - 1);
        s_req_path[sizeof s_req_path - 1] = 0;
        s_rp_loaded = 1;
        s_replaying = 1;
        s_active_role = s_rp.role;
        params_copy(&s_params, &s_rp.params);
    } else {
        /* The image and the regions are not loaded yet: zeros, never applied. */
        s_params.seed = seed;
        s_params.tick_ms = BOOT_TICK_MS;
        s_params.qpc = BOOT_QPC;
        s_params.filetime = BOOT_FILETIME;
        s_params.xgscnt = 0;
        s_params.save_len = g->save_len;
        s_params.save = (uint8_t *)calloc(g->save_len ? g->save_len : 1, 1);
        s_params.sync_len = sync_total();
        s_params.sync = (uint8_t *)calloc(s_params.sync_len ? s_params.sync_len : 1, 1);
        if (!s_params.save || !s_params.sync) { np_params_free(&s_params); return 0; }
        s_replaying = 0;
        s_active_role = s_role;
    }
    s_boot = 1;
    s_armed = 1;
    s_div_frame = NP_NO_FRAME;
    s_div_region = -1;
    s_keep_all = s_record || s_replaying || online_get_diag();
    s_rec_n = 0;
    s_frame = 0;
    s_tag = 0;
    s_transitions = 0;
    s_seed_va = 0;                                         /* found at frame 0: no guest code runs before */
    s_boot_armed = 0;                                      /* clocks, chip and seeds go in at frame 0 */
    np_game_title_redirect(1);                             /* saves aside until the process exits */
    np_game_session_reset();
    s_state = NP_STATE_ACTIVE;
    _snprintf(s_msg, sizeof(s_msg) - 1, "%s from boot", s_replaying ? "Replaying" : "Lockstep session");
    if (online_get_diag()) {
        fprintf(stderr, "[netplay] boot session started (%s, seed %08X)\n", s_replaying ? "replay" : "live", s_params.seed);
        fflush(stderr);
    }
    return 1;
}

/* ── per frame ──────────────────────────────────────────────────────── */

/* Menu presentation calls rand() on timers that started before the session,
 * so the seed is reset at every screen change from the session seed and the
 * number of changes so far (the same frame on both machines). */
static void reseed_on_transition(void)
{
    uint32_t screen = np_game_screen_key(), seed;
    if (screen == s_last_screen) return;
    s_last_screen = screen;
    s_transitions++;
    if (s_provider && s_provider->transition && !s_replaying) {
        if (!s_provider->transition(s_transitions)) s_abort = 1;
    }
    seed = s_params.seed ^ (s_transitions * 0x9E3779B1u);
    if (s_seed_va) np_game_write(s_seed_va, &seed, 4);
    np_game_worker_rand_seed(s_params.seed ^ 0x5A5A5A5Au ^ (s_transitions * 0x85EBCA6Bu));
}

static void load_requested_replay(void)
{
    const np_game_desc *g = np_game();
    FILE *f = fopen(s_req_path, "rb");
    const char *err = np_replay_read(f, &s_rp, g->save_len, sync_total(), (uint32_t)n_hash());
    if (f) fclose(f);
    InterlockedExchange(&s_req_replay, 0);
    if (err) {
        s_rp_loaded = 0;
        _snprintf(s_msg, sizeof(s_msg) - 1, "Replay not loaded: %s", err);
        return;
    }
    s_rp_loaded = 1;
    _snprintf(s_msg, sizeof(s_msg) - 1, "Replay ready (%s Battle, %u frames%s) - go to that Versus character select",
              s_rp.tag ? "Tag" : "Single", s_rp.frames,
              s_rp.build_stamp == build_stamp() ? "" : ", recorded on a DIFFERENT build");
    s_armed = 1;
}

void netplay_input_tick(void)
{
    static const np_pad none[2];
    np_pad in[2];
    int p;

    if (s_req_replay && s_state != NP_STATE_ACTIVE) {
        if (tables_ok()) load_requested_replay();
        else InterlockedExchange(&s_req_replay, 0);
    }
    if (s_state == NP_STATE_OFF) {
        if (!s_armed) return;
        s_state = NP_STATE_ARMED;
    }
    if (s_state == NP_STATE_ARMED) {
        if (!s_armed) { s_state = NP_STATE_OFF; np_replay_free(&s_rp); s_rp_loaded = 0; s_boot = s_boot_online = 0; return; }
        if (s_boot_online) {
            /* Restart-on-join: the game holds here, at its first frame-loop
             * tick, while the connection comes back up and the provider's
             * handshake exchanges the start data; the pump keeps the window
             * alive and gives up on a timeout. */
            int r = 0;
            for (;;) {
                if (!s_boot_pump || s_boot_pump() < 0) { r = -1; break; }
                if (s_provider) { r = s_provider->barrier(1, s_boot_battle); if (r != 0) break; }
            }
            if (r < 0) {
                s_boot = s_boot_online = 0;
                s_armed = 0;
                s_state = NP_STATE_OFF;
                _snprintf(s_msg, sizeof(s_msg) - 1, "The online session could not start; playing offline");
                if (online_get_diag()) { fprintf(stderr, "[netplay] online boot session abandoned before frame 0\n"); fflush(stderr); }
                return;
            }
            s_active_role = s_role;
            s_state = NP_STATE_ACTIVE;
            goto frame;
        }
        if (s_mirror && !s_provider) {                     /* one controller drives both cursors */
            static uint32_t packet = 0x1000u;
            np_pad m, none0 = {0};
            int owner = np_game_pause_owner();              /* see mirror_pair() */
            np_game_pad_read(0, &m);
            if (owner == 0) np_game_pad_write(1, &none0, ++packet);
            else if (owner == 1) { np_game_pad_write(1, &m, ++packet); np_game_pad_write(0, &none0, packet); }
            else np_game_pad_write(1, &m, ++packet);
        }
        if (s_provider && !s_rp_loaded) {
            /* Online: the provider says when both sides are ready. While this
             * side waits nobody may move the cursor (that loads a preview and
             * un-settles the barrier). */
            int ab = at_barrier(), r;
            if (ab) for (p = 0; p < 4; p++) np_game_pad_neutral(p);
            r = s_provider->barrier(ab, np_game_is_tag() ? 1 : 0);
            if (r < 0) { s_armed = 0; s_state = NP_STATE_OFF; return; }
            if (r == 0) return;
            s_quiet_frames = 0;
            begin_session();
            goto frame;
        }
        if (!at_barrier()) return;
        s_quiet_frames = 0;
        if (s_rp_loaded && (np_game_is_tag() ? 1 : 0) != s_rp.tag) {
            _snprintf(s_msg, sizeof(s_msg) - 1, "This replay is a %s Battle recording - pick %s Battle",
                      s_rp.tag ? "Tag" : "Single", s_rp.tag ? "Tag" : "Single");
            return;
        }
        begin_session();
    } else if (!s_armed || (!s_boot && np_game_left_versus()) || (s_replaying && s_frame >= s_rp.frames)) {
        record_frame(none);                                /* the state that ended the session */
        end_session(!s_armed ? "disarmed" : s_replaying && s_frame >= s_rp.frames ? "replay finished" : "left Versus");
        return;
    }

frame:
    if (s_boot && s_frame == 0 && !s_boot_armed) {
        /* Frame 0 of a boot session: the first frame-loop tick (the frame loop
         * starts about a second after launch, before the intro; the intro movies
         * are skipped, see netplay_skip_movies). The CRT is up, so the seed word
         * can be found; what start-up left in the regions is captured (record,
         * live) or put back (replay); the screen it starts on is not a
         * transition. */
        xbox_det_bases b;
        s_boot_armed = 1;
        if (s_replaying || s_boot_online) sync_write(s_params.sync);   /* a replay, or the host's (online) */
        else sync_read(s_params.sync);
        np_game_pad_history_clear();                      /* intro presses differ between machines */
        s_seed_va = np_game_rand_seed_va();
        if (s_seed_va) np_game_write(s_seed_va, &s_params.seed, 4);
        np_game_worker_rand_seed(s_params.seed ^ 0x5A5A5A5Au);
        b.tick_ms = s_params.tick_ms;
        b.qpc = s_params.qpc;
        b.filetime = s_params.filetime;
        xbox_det_begin(&b);
        mcpx_apu_det_begin(s_params.xgscnt);
        s_last_screen = np_game_screen_key();
        s_profile_hash = save_image_hash();
        if (online_get_diag()) {
            fprintf(stderr, "[netplay] boot session frame 0 (screen %08X, seed word %s)\n", s_last_screen,
                    s_seed_va ? "set" : "not found");
            fflush(stderr);
        }
    }
    if (s_boot && s_boot_armed) {
        /* A session runs with every character and costume, and the joiner's
         * button layout on port 1: put into the profile image whenever the game
         * changes it (the load at the profile prompt, later saves), the same
         * frame on both machines. (The game's profile pointer 0x87CFA8 is set at
         * its second frame, long before the load, so it cannot say when.) */
        if (save_image_hash() != s_profile_hash) {
            apply_session_profile();
            s_profile_hash = save_image_hash();
        }
    }
    reseed_on_transition();
    if (s_abort) {                                         /* the transition resync did not arrive */
        s_abort = 0;
        record_frame(none);
        end_session("transition data did not arrive");
        return;
    }
    if (s_replaying) {
        in[0] = s_rp.pads[2 * s_frame];
        in[1] = s_rp.pads[2 * s_frame + 1];
    } else if (s_provider) {
        np_pad local;
        np_game_pad_read(0, &local);
        if (!s_provider->exchange(&local, in)) {
            record_frame(none);
            end_session("input exchange ended");
            return;
        }
    } else {
        np_game_pad_read(0, &in[0]);
        if (!s_mirror) np_game_pad_read(1, &in[1]);
        else mirror_pair(in);
    }

    record_frame(in);                                      /* the state entering this frame */
    if (s_provider && !s_replaying) s_provider->digest(s_frame, s_digest);
    if (s_replaying && s_div_frame == NP_NO_FRAME && s_digest != s_rp.digests[s_frame]) {
        const np_frame_rec *r = rec_get(s_frame);
        int k, nh = n_hash();
        s_div_frame = s_frame;
        for (k = 0; r && k < nh; k++) {
            uint32_t want = s_rp.hashes[(size_t)s_frame * s_rp.n_hash + k];
            if (r->region[k] != want) {
                s_div_region = k;
                s_div_rec = want;
                s_div_now = r->region[k];
                break;
            }
        }
    }
    for (p = 0; p < 2; p++) np_game_pad_write(p, &in[p], 0x1000u + s_frame);
    for (p = 2; p < 4; p++) np_game_pad_write(p, &none[0], 0);
    xbox_det_frame();
    mcpx_apu_det_frame();
    s_frame++;
}

void netplay_note_rand(void)
{
    if (s_state != NP_STATE_ACTIVE) return;
    if (np_game_rand_on_worker()) s_rand_workers++;
    else s_rand_game++;
}

uint32_t netplay_forced_pads(void)
{
    if (s_state == NP_STATE_ACTIVE) return NP_SESSION_PADS;
    /* Online lobby, or a one-controller local session: the second player is
     * port 1 from now on (the game needs two pads to enter Versus). */
    if (s_armed && (s_provider || s_mirror)) return NP_SESSION_PADS;
    return 0;
}

uint32_t netplay_filter_pad_mask(uint32_t host_mask)
{
    uint32_t forced = netplay_forced_pads();
    return forced ? forced : host_mask;
}

uint32_t netplay_session_frame(void) { return s_frame; }
int netplay_skip_movies(void) { return s_state == NP_STATE_ACTIVE || s_boot; }
void netplay_set_boot_pump(int (*pump)(void)) { s_boot_pump = pump; }
uint32_t netplay_seed_va(void) { return s_state == NP_STATE_ACTIVE ? s_seed_va : 0; }

/* ── UI ─────────────────────────────────────────────────────────────── */

void netplay_set_armed(int armed, int role)
{
    if (armed && !tables_ok()) { s_armed = 0; return; }
    if (s_state != NP_STATE_ACTIVE) s_role = role;
    s_armed = armed ? 1 : 0;
}

int  netplay_armed(void) { return s_armed; }
void netplay_set_mirror(int on) { s_mirror = on ? 1 : 0; }
int  netplay_mirror(void) { return s_mirror; }
int  netplay_active(void) { return s_state == NP_STATE_ACTIVE; }
void netplay_set_record(int on) { s_record = on ? 1 : 0; }
int  netplay_record(void) { return s_record; }

void netplay_request_replay(const char *path)
{
    if (!path || s_state == NP_STATE_ACTIVE) return;
    strncpy(s_req_path, path, sizeof(s_req_path) - 1);
    s_req_path[sizeof(s_req_path) - 1] = 0;
    InterlockedExchange(&s_req_replay, 1);
}

int netplay_list_replays(char (*names)[128], int max)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("netplay_replay_*.dnr", &fd);
    int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (n < max) {
            strncpy(names[n], fd.cFileName, 127);
            names[n][127] = 0;
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    {   /* newest first: the names carry the time stamp, so a reverse string
         * sort is a reverse time sort (the tab's default pick is index 0) */
        int i, j;
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && strcmp(names[j - 1], names[j]) < 0; j--) {
                char t[128];
                memcpy(t, names[j - 1], 128); memcpy(names[j - 1], names[j], 128); memcpy(names[j], t, 128);
            }
    }
    return n;
}

void netplay_get_status(np_status *out)
{
    if (!out) return;
    out->state = s_state;
    out->role = s_state == NP_STATE_ACTIVE ? s_active_role : s_role;
    out->tag = s_tag;
    out->frame = s_frame;
    out->digest = s_digest;
    out->sessions = s_sessions;
    memcpy(out->last_log, s_last_log, sizeof(out->last_log));
    out->replaying = s_replaying || s_rp_loaded;
    out->replay_frames = s_rp_loaded ? s_rp.frames : 0;
    out->diverged_at = s_div_frame;
    memcpy(out->message, s_msg, sizeof(out->message));
}

/* ── online provider support ────────────────────────────────────────── */

void netplay_set_provider(const np_provider *p)
{
    if (s_state == NP_STATE_ACTIVE) return;                /* never mid-session */
    s_provider = p;
}

int netplay_has_provider(void) { return s_provider != NULL; }

uint32_t netplay_build_stamp(void) { return build_stamp(); }
void netplay_session_set_end_note(const char *note)
{
    strncpy(s_end_note, note ? note : "", sizeof(s_end_note) - 1);
    s_end_note[sizeof(s_end_note) - 1] = 0;
}

void netplay_local_layout(uint8_t layout[NP_LAYOUT_MAX], uint8_t *laysel)
{
    uint8_t sel = 0;
    memset(layout, 0, NP_LAYOUT_MAX);
    np_game_layout_read(0, layout, &sel);
    if (laysel) *laysel = sel;
}

int netplay_params_capture(const uint8_t **blob, uint32_t *len,
                           const uint8_t joiner_layout[NP_LAYOUT_MAX], uint8_t joiner_laysel)
{
    uint32_t n;
    if (s_state == NP_STATE_ACTIVE || !tables_ok()) return 0;
    if (!make_local_params(&s_params, joiner_layout, joiner_laysel)) return 0;
    n = np_params_blob_size(s_params.save_len, s_params.sync_len);
    if (!blob_reserve(n) || np_params_pack(&s_params, s_blob, n) != n) return 0;
    *blob = s_blob;
    *len = n;
    return 1;
}

int netplay_params_load(const uint8_t *blob, uint32_t len)
{
    if (s_state == NP_STATE_ACTIVE || !tables_ok()) return 0;
    return np_params_unpack(&s_params, blob, len, np_game()->save_len, sync_total());
}

int netplay_sync_capture(const uint8_t **blob, uint32_t *len)
{
    uint32_t n = sync_total();
    if (!blob_reserve(n ? n : 1)) return 0;
    sync_read(s_blob);
    *blob = s_blob;
    *len = n;
    return 1;
}

int netplay_sync_apply(const uint8_t *blob, uint32_t len)
{
    if (!blob || len != sync_total()) return 0;
    sync_write(blob);
    return 1;
}

int  netplay_session_region_count(void) { return n_hash(); }
void netplay_session_peek(uint32_t va, void *dst, uint32_t len) { np_game_read(va, dst, len); }
const char *netplay_session_region_name(int k) { return region_name(k); }

int netplay_session_frame_regions(uint32_t frame, uint32_t *out, int max)
{
    const np_frame_rec *r = rec_get(frame);
    int n = n_hash(), k;
    if (!out || max < 1 || !r) return 0;
    if (n > max) n = max;
    for (k = 0; k < n; k++) out[k] = r->region[k];
    return n;
}
