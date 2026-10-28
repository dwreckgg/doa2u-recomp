/*
 * netplay_sweep.h - the rolling guest-RAM sweep of an online session (diagnostics).
 *
 * The frame digest covers chosen regions only (the fight blocks only in a fight), so
 * a desync is found late and in the wrong place. The sweep covers all of guest RAM:
 * at every lockstep frame both games hash the same few 64 KB blocks - which ones
 * follows from the frame number alone - at the same point of the frame (the state
 * entering it), keep their own hashes for a while and send them to the other side.
 * Comparing the other side's hashes for a frame with its own for the same frame
 * says which blocks differ, and since when; a block that starts or stops differing
 * is an event. Two copies in sync on one PC differ in about a dozen small ranges
 * (file times and file ids the host wrote into guest memory, stack leftovers), all
 * from the session's first frames, so a block that starts to differ later is a lead.
 *
 * No guest memory here: the caller hands in a pointer lookup. Host-tested
 * (tests/netplay_sweep_test.c).
 */
#ifndef DOA2U_NETPLAY_SWEEP_H
#define DOA2U_NETPLAY_SWEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP_SWEEP_MAX_PER_FRAME 32u     /* blocks hashed per frame, at most */
#define NP_SWEEP_RING          512u    /* frames of own hashes kept for the other side's late packets */

/* The host address of guest `va` (a whole block from there is readable), or NULL
 * when the block is not mapped: it hashes as 0 on both sides. */
typedef const uint8_t *(*np_sweep_ptr_fn)(uint32_t va);

typedef struct {
    uint32_t lo, hi, block, per_frame, nblocks;
    np_sweep_ptr_fn ptr;
    uint32_t ring_frame[NP_SWEEP_RING];               /* NP_SWEEP_NONE: empty slot */
    uint32_t ring_hash[NP_SWEEP_RING][NP_SWEEP_MAX_PER_FRAME];
    uint8_t  *state;                                  /* per block: 0 not compared yet, 1 equal, 2 differs */
    uint32_t *since;                                  /* per block: frame of its last change of state */
    uint32_t compared, late, mismatched;              /* peer frames compared / too old / other batch */
    uint32_t early;                                   /* peer frames that arrived before this side's own */
    uint32_t newest;                                  /* this side's latest hashed frame (NP_SWEEP_NONE: none) */
    uint32_t peer_frame[NP_SWEEP_RING];               /* early peer packets, kept until this side gets there */
    uint32_t peer_first[NP_SWEEP_RING], peer_count[NP_SWEEP_RING];
    uint32_t peer_hash[NP_SWEEP_RING][NP_SWEEP_MAX_PER_FRAME];
} np_sweep;

#define NP_SWEEP_NONE 0xFFFFFFFFu

typedef struct {
    uint32_t va;          /* the block's first address */
    uint32_t frame;       /* the frame whose hashes showed it */
    int      differs;     /* 1: it differs from now on; 0: equal again */
} np_sweep_event;

/* [lo, hi) in blocks of `block` bytes (hi - lo a multiple of block), `per_frame`
 * blocks a frame. 1 on success. */
int  np_sweep_init(np_sweep *s, uint32_t lo, uint32_t hi, uint32_t block, uint32_t per_frame, np_sweep_ptr_fn ptr);
void np_sweep_free(np_sweep *s);

/* The blocks of `frame`: the first index and how many (they wrap around the end). */
uint32_t np_sweep_batch(const np_sweep *s, uint32_t frame, uint32_t *first);
/* This side at `frame`: hashes the frame's blocks, keeps them, and copies them out
 * for the other side. Returns the count (the first block index in *first). */
uint32_t np_sweep_local(np_sweep *s, uint32_t frame, uint32_t *first, uint32_t *hashes);
/* The other side's hashes for `frame`: compared with this side's own for the same
 * frame (ignored when that frame is no longer kept, or the batch is not the same).
 * Writes up to `max` events; returns how many. */
int  np_sweep_peer(np_sweep *s, uint32_t frame, uint32_t first, uint32_t count, const uint32_t *hashes,
                   np_sweep_event *ev, int max);
/* After np_sweep_local(frame): compares the other side's packet for that frame if
 * it came early (np_sweep_peer kept it). Writes up to `max` events; returns how many. */
int  np_sweep_pending(np_sweep *s, uint32_t frame, np_sweep_event *ev, int max);
/* The 32-bit hash of `len` bytes (a multiple of 8). */
uint32_t np_sweep_hash(const uint8_t *p, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_SWEEP_H */
