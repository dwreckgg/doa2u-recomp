/* netplay_sweep.c - see netplay_sweep.h. */
#include <stdlib.h>
#include <string.h>
#include "netplay_sweep.h"

/* One xxHash64-style lane over 8-byte words: every round is a bijection of the
 * word for a given state and of the state for a given word, so a change in any
 * byte changes the 64-bit result; about 0.1 ms a megabyte. */
uint32_t np_sweep_hash(const uint8_t *p, uint32_t len)
{
    const uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull;
    uint64_t h = 2870177450012600261ull ^ len, w;
    uint32_t i;
    for (i = 0; i + 8 <= len; i += 8) {
        memcpy(&w, p + i, 8);
        h += w * P2;
        h = (h << 31) | (h >> 33);
        h *= P1;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    return (uint32_t)(h ^ (h >> 32));
}

int np_sweep_init(np_sweep *s, uint32_t lo, uint32_t hi, uint32_t block, uint32_t per_frame, np_sweep_ptr_fn ptr)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    if (!block || hi <= lo || (hi - lo) % block || !per_frame || per_frame > NP_SWEEP_MAX_PER_FRAME) return 0;
    s->lo = lo; s->hi = hi; s->block = block; s->per_frame = per_frame; s->ptr = ptr;
    s->nblocks = (hi - lo) / block;
    if (per_frame > s->nblocks) return 0;
    s->state = (uint8_t *)calloc(s->nblocks, 1);
    s->since = (uint32_t *)calloc(s->nblocks, sizeof(uint32_t));
    if (!s->state || !s->since) { np_sweep_free(s); return 0; }
    for (i = 0; i < NP_SWEEP_RING; i++) s->ring_frame[i] = s->peer_frame[i] = NP_SWEEP_NONE;
    s->newest = NP_SWEEP_NONE;
    return 1;
}

void np_sweep_free(np_sweep *s)
{
    free(s->state);
    free(s->since);
    s->state = NULL;
    s->since = NULL;
}

uint32_t np_sweep_batch(const np_sweep *s, uint32_t frame, uint32_t *first)
{
    *first = (uint32_t)(((uint64_t)frame * s->per_frame) % s->nblocks);
    return s->per_frame;
}

uint32_t np_sweep_local(np_sweep *s, uint32_t frame, uint32_t *first, uint32_t *hashes)
{
    uint32_t n = np_sweep_batch(s, frame, first), i, *keep = s->ring_hash[frame % NP_SWEEP_RING];
    for (i = 0; i < n; i++) {
        uint32_t k = (*first + i) % s->nblocks;
        const uint8_t *p = s->ptr ? s->ptr(s->lo + k * s->block) : NULL;
        keep[i] = p ? np_sweep_hash(p, s->block) : 0;
        hashes[i] = keep[i];
    }
    s->ring_frame[frame % NP_SWEEP_RING] = frame;
    s->newest = frame;
    return n;
}

static int compare(np_sweep *s, uint32_t frame, uint32_t first, uint32_t count, const uint32_t *hashes,
                   np_sweep_event *ev, int max)
{
    uint32_t slot = frame % NP_SWEEP_RING, f0, n, i;
    int nev = 0;
    n = np_sweep_batch(s, frame, &f0);
    if (first != f0 || count != n) { s->mismatched++; return 0; }    /* another configuration */
    s->compared++;
    for (i = 0; i < n; i++) {
        uint32_t k = (f0 + i) % s->nblocks;
        uint8_t now = hashes[i] != s->ring_hash[slot][i] ? 2 : 1;
        if (s->state[k] == now) continue;
        if (s->state[k] != 0 && frame <= s->since[k]) continue;      /* older news, out of order */
        if (!(s->state[k] == 0 && now == 1) && nev < max) {         /* first sight of an equal block: silent */
            ev[nev].va = s->lo + k * s->block;
            ev[nev].frame = frame;
            ev[nev].differs = now == 2;
            nev++;
        }
        s->state[k] = now;
        s->since[k] = frame;
    }
    return nev;
}

int np_sweep_peer(np_sweep *s, uint32_t frame, uint32_t first, uint32_t count, const uint32_t *hashes,
                  np_sweep_event *ev, int max)
{
    uint32_t slot = frame % NP_SWEEP_RING;
    if (s->ring_frame[slot] == frame) return compare(s, frame, first, count, hashes, ev, max);
    if ((s->newest == NP_SWEEP_NONE || frame > s->newest) && count <= NP_SWEEP_MAX_PER_FRAME &&
        frame - (s->newest == NP_SWEEP_NONE ? 0 : s->newest) < NP_SWEEP_RING) {
        s->peer_frame[slot] = frame;                                 /* ahead of this side: kept */
        s->peer_first[slot] = first;
        s->peer_count[slot] = count;
        memcpy(s->peer_hash[slot], hashes, count * sizeof(uint32_t));
        s->early++;
        return 0;
    }
    s->late++;                                                       /* that frame is no longer kept */
    return 0;
}

int np_sweep_pending(np_sweep *s, uint32_t frame, np_sweep_event *ev, int max)
{
    uint32_t slot = frame % NP_SWEEP_RING;
    if (s->peer_frame[slot] != frame || s->ring_frame[slot] != frame) return 0;
    s->peer_frame[slot] = NP_SWEEP_NONE;
    return compare(s, frame, s->peer_first[slot], s->peer_count[slot], s->peer_hash[slot], ev, max);
}
