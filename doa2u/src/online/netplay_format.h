/*
 * netplay_format.h - the byte formats of online play, with no guest memory:
 * the per-frame digest, the session start parameters as one blob (sent from
 * host to joiner, and stored in replays), the replay file, and the checks on
 * a fight-state region table. Host-tested (tests/netplay_format_test.c).
 *
 * Everything on the wire or on disk is little-endian and packed by hand, so
 * two builds agree whatever their compilers pad.
 */
#ifndef DOA2U_NETPLAY_FORMAT_H
#define DOA2U_NETPLAY_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP_FNV32_INIT        2166136261u
#define NP_FNV64_INIT        1469598103934665603ull
#define NP_LAYOUT_MAX        16u                 /* bytes of one port's button layout, carried */
#define NP_REPLAY_MAX_FRAMES (60u * 60u * 60u)   /* one hour */

/* One player's pad for one frame: the XINPUT_GAMEPAD part of the guest
 * state (buttons, 8 analog buttons, 4 thumb axes), 18 bytes on the wire. */
#pragma pack(push, 1)
typedef struct {
    uint16_t buttons;
    uint8_t  analog[8];
    int16_t  lx, ly, rx, ry;
} np_pad;
#pragma pack(pop)

/* A block of guest memory that is fight state. */
typedef struct {
    uint32_t    va, len;
    const char *name;
} np_region;

/* Session start parameters. `save` and `sync` are owned (np_params_free). */
typedef struct {
    uint32_t seed;
    uint32_t tick_ms;      /* xbox_det_bases */
    uint64_t qpc;
    uint64_t filetime;
    uint64_t xgscnt;       /* APU sample counter at frame 0 */
    uint32_t save_len;
    uint8_t *save;         /* the in-memory save image */
    uint8_t  joiner_layout[NP_LAYOUT_MAX];
    uint8_t  joiner_laysel;
    uint32_t sync_len;
    uint8_t *sync;         /* the sync regions, concatenated in table order */
} np_params;

uint32_t np_fnv32(uint32_t h, const void *p, uint32_t len);

/* The build identity two games compare before a session: derived from the
 * git commit when the tree was clean (any build of that commit matches),
 * else the exe's link stamp (only the very same exe matches). */
uint32_t np_build_id(const char *commit, int dirty, uint32_t link_stamp);

/* The screen-change check (netplay.c): the transition's index and one hash per
 * sync region. pack returns the length (0 when `cap` is short); unpack returns 1
 * when it loaded; diff returns how many hashes differ and lists up to `max`. */
uint32_t np_rhash_pack(uint32_t index, const uint32_t *h, uint32_t n, uint8_t *dst, uint32_t cap);
int      np_rhash_unpack(const uint8_t *src, uint32_t len, uint32_t *index, uint32_t *h, uint32_t max, uint32_t *n);
int      np_rhash_diff(const uint32_t *a, const uint32_t *b, uint32_t n, int *idx, int max);
/* Folds the per-region hashes of one frame into its digest (order matters). */
uint64_t np_digest_combine(const uint32_t *hashes, int n);

/* 1 when every region is non-empty, inside [lo, hi) and overlaps no other
 * region of the same table; otherwise 0 and `why` says which. */
int      np_regions_check(const np_region *r, int n, uint32_t lo, uint32_t hi, char *why, size_t whylen);
uint32_t np_regions_total(const np_region *r, int n);

/* The start-parameter blob. pack returns the bytes written (0 when `cap` is
 * too small); unpack returns 1 and fills `p` (allocating save and sync) only
 * when the blob is exactly the expected size for these lengths. */
uint32_t np_params_blob_size(uint32_t save_len, uint32_t sync_len);
uint32_t np_params_pack(const np_params *p, uint8_t *dst, uint32_t cap);
int      np_params_unpack(np_params *p, const uint8_t *src, uint32_t len,
                          uint32_t save_len, uint32_t sync_len);
void     np_params_free(np_params *p);

/* A replay: the start parameters, then per frame both pads, the digest and
 * the per-region hashes the recording saw. Arrays are owned (np_replay_free). */
typedef struct {
    uint32_t  build_stamp;
    int       role, tag;
    np_params params;
    uint32_t  n_hash;
    uint32_t  frames;
    np_pad   *pads;       /* frames x 2 */
    uint64_t *digests;    /* frames */
    uint32_t *hashes;     /* frames x n_hash */
} np_replay;

int         np_replay_write(FILE *f, const np_replay *r);   /* 1 on success */
/* NULL when it loaded; otherwise why not. Lengths are what this build expects. */
const char *np_replay_read(FILE *f, np_replay *r, uint32_t save_len, uint32_t sync_len, uint32_t n_hash);
void        np_replay_free(np_replay *r);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_FORMAT_H */
