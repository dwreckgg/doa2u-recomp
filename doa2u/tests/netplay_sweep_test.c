/* netplay_sweep.c: the rolling RAM sweep's batches, its comparison of the other
 * side's hashes with this side's own for the same frame, and its events. Two fake
 * guest memories stand for the two games; the other side's packets arrive late. */
#include "online/netplay_sweep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define LO     0x00010000u
#define BLOCK  0x100u
#define NB     16u
#define HI     (LO + NB * BLOCK)
#define PF     3u                     /* does not divide NB: batches wrap around the end */

static uint8_t s_a[NB * BLOCK], s_b[NB * BLOCK];
static const uint8_t *ptr_a(uint32_t va) { return (va >= LO && va + BLOCK <= HI) ? &s_a[va - LO] : NULL; }
static const uint8_t *ptr_b(uint32_t va) { return (va >= LO && va + BLOCK <= HI) ? &s_b[va - LO] : NULL; }

/* Independent of the module: does frame f's batch hash block k? */
static int covers(uint32_t f, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < PF; i++) if ((f * PF + i) % NB == k) return 1;
    return 0;
}

static np_sweep A, B;
#define LAG 3                         /* the other side's packet for frame f arrives at frame f + LAG */
static uint32_t s_pf[1024][NP_SWEEP_MAX_PER_FRAME], s_pfirst[1024], s_pcount[1024];   /* B's packets */

/* Runs frames [from, to): both sides hash; A receives B's packet LAG frames late.
 * `mutate` runs before a frame's hashing (the game's frame before it). Collects A's events. */
static int run(uint32_t from, uint32_t to, void (*mutate)(uint32_t f), np_sweep_event *ev, int max)
{
    uint32_t f, h[NP_SWEEP_MAX_PER_FRAME], first;
    int n = 0;
    for (f = from; f < to; f++) {
        if (mutate) mutate(f);
        np_sweep_local(&A, f, &first, h);
        s_pcount[f % 1024] = np_sweep_local(&B, f, &s_pfirst[f % 1024], s_pf[f % 1024]);
        if (f >= LAG) {
            uint32_t g = f - LAG;
            n += np_sweep_peer(&A, g, s_pfirst[g % 1024], s_pcount[g % 1024], s_pf[g % 1024], ev + n, max - n);
        }
    }
    return n;
}

static void counter_both(uint32_t f)       /* the game's own per-frame counter, the same on both */
{
    memcpy(&s_a[7 * BLOCK + 8], &f, 4);
    memcpy(&s_b[7 * BLOCK + 8], &f, 4);
}

static void flip_at_50_back_at_80(uint32_t f)
{
    counter_both(f);
    if (f == 50) s_b[5 * BLOCK + 17] ^= 0x40;            /* the joiner's game goes its own way */
    if (f == 80) s_b[5 * BLOCK + 17] ^= 0x40;            /* and comes back */
}

static uint32_t first_covering(uint32_t from, uint32_t k)
{
    uint32_t f = from;
    while (!covers(f, k)) f++;
    return f;
}

int main(void)
{
    np_sweep_event ev[64];
    uint32_t first, n, f, i;
    int k;

    for (i = 0; i < sizeof s_a; i++) s_a[i] = s_b[i] = (uint8_t)(i * 7 + 3);
    CHECK(np_sweep_init(&A, LO, HI, BLOCK, PF, ptr_a));
    CHECK(np_sweep_init(&B, LO, HI, BLOCK, PF, ptr_b));

    /* batches: frame f takes PF blocks from (f * PF) mod NB, wrapping */
    for (f = 0; f < 40; f++) {
        n = np_sweep_batch(&A, f, &first);
        CHECK(n == PF && first == (f * PF) % NB);
    }

    /* in sync, the other side's hashes three frames late, a block that changes every
     * frame on both: comparing against the wrong frame would see it differ */
    k = run(0, 50, counter_both, ev, 64);
    CHECK(k == 0);
    CHECK(A.compared == 50 - LAG && A.late == 0 && A.mismatched == 0);

    /* the joiner's block 5 differs from frame 50 to 79: one event when the first
     * batch after the change covers it, one when the first batch after the
     * repair does, nothing in between however often it is compared */
    k = run(50, 120, flip_at_50_back_at_80, ev, 64);
    if (k != 2) fprintf(stderr, "events: %d\n", k);
    CHECK(k == 2);
    CHECK(k >= 1 && ev[0].va == LO + 5 * BLOCK && ev[0].differs == 1 && ev[0].frame == first_covering(50, 5));
    CHECK(k >= 2 && ev[1].va == LO + 5 * BLOCK && ev[1].differs == 0 && ev[1].frame == first_covering(80, 5));
    CHECK(A.state[5] == 1);
    CHECK(A.since[5] == first_covering(80, 5));
    {   /* UDP may deliver an old packet again, or late: older news never undoes newer */
        uint32_t g = first_covering(50, 5);                          /* B's packet that showed the difference */
        CHECK(np_sweep_peer(&A, g, s_pfirst[g % 1024], s_pcount[g % 1024], s_pf[g % 1024], ev, 64) == 0);
        CHECK(A.state[5] == 1);
    }

    /* the other side ahead: its packet for frame f arrives before this side has
     * hashed f (measured: about one packet in ten in a loopback session). It is
     * kept and compared when this side gets there, with the same events. */
    {
        np_sweep E, F;
        uint32_t h[NP_SWEEP_MAX_PER_FRAME], hf[NP_SWEEP_MAX_PER_FRAME], ff, cf;
        int ke = 0;
        CHECK(np_sweep_init(&E, LO, HI, BLOCK, PF, ptr_a));
        CHECK(np_sweep_init(&F, LO, HI, BLOCK, PF, ptr_b));
        for (f = 200; f < 300; f++) {
            if (f == 230) s_b[9 * BLOCK + 3] ^= 0x10;          /* B's block 9 differs from frame 230 on */
            cf = np_sweep_local(&F, f, &ff, hf);               /* B hashes f and sends it ... */
            ke += np_sweep_peer(&E, f, ff, cf, hf, ev + ke, 64 - ke);   /* ... A gets it before its own f */
            np_sweep_local(&E, f, &first, h);
            ke += np_sweep_pending(&E, f, ev + ke, 64 - ke);
        }
        s_b[9 * BLOCK + 3] ^= 0x10;
        CHECK(E.compared == 100 && E.late == 0 && E.early == 100);
        CHECK(ke == 1 && ev[0].va == LO + 9 * BLOCK && ev[0].differs == 1 && ev[0].frame == first_covering(230, 9));
        /* a packet older than the frames kept is late, not early, also when its slot
         * of the ring is still empty or holds a newer frame (this side at 900 now) */
        for (f = 300; f <= 900; f++) np_sweep_local(&E, f, &first, h);
        n = np_sweep_batch(&E, 100, &first);
        CHECK(np_sweep_peer(&E, 100, first, n, h, ev, 64) == 0);
        CHECK(E.late == 1 && E.early == 100);
        np_sweep_free(&E);
        np_sweep_free(&F);
    }

    /* a packet older than the frames kept is dropped, and so is one for another batch */
    {
        uint32_t h[NP_SWEEP_MAX_PER_FRAME] = { 0 }, late0 = A.late, mism0 = A.mismatched;
        CHECK(np_sweep_peer(&A, 119 - NP_SWEEP_RING - 5, 0, PF, h, ev, 64) == 0 && A.late == late0 + 1);
        n = np_sweep_batch(&A, 118, &first);
        CHECK(np_sweep_peer(&A, 118, (first + 1) % NB, n, h, ev, 64) == 0 && A.mismatched == mism0 + 1);
        CHECK(np_sweep_peer(&A, 118, first, n - 1, h, ev, 64) == 0 && A.mismatched == mism0 + 2);
    }

    /* the hash sees every byte position (a change anywhere in a block counts) */
    {
        uint8_t blk[BLOCK];
        uint32_t h0, pos;
        for (i = 0; i < BLOCK; i++) blk[i] = (uint8_t)i;
        h0 = np_sweep_hash(blk, BLOCK);
        for (pos = 0; pos < BLOCK; pos++) {
            blk[pos] ^= 1;
            CHECK(np_sweep_hash(blk, BLOCK) != h0);
            blk[pos] ^= 1;
        }
        CHECK(np_sweep_hash(blk, BLOCK) == h0);
    }

    /* a block with no memory behind it hashes as 0 on both sides */
    {
        np_sweep C;
        uint32_t h[NP_SWEEP_MAX_PER_FRAME];
        CHECK(np_sweep_init(&C, LO, HI + 2 * BLOCK, BLOCK, PF, ptr_a));   /* the last two blocks: unmapped */
        n = np_sweep_local(&C, 6, &first, h);                             /* blocks 0, 1, 2 of 18 */
        CHECK(n == PF && first == 0 && C.nblocks == NB + 2);
        n = np_sweep_local(&C, 5, &first, h);                             /* blocks 15, 16, 17 */
        CHECK(n == PF && first == 15 && h[0] != 0 && h[1] == 0 && h[2] == 0);
        np_sweep_free(&C);
    }

    /* refusals: a range that is not whole blocks, too many blocks a frame */
    {
        np_sweep D;
        CHECK(!np_sweep_init(&D, LO, HI + 1, BLOCK, PF, ptr_a));
        CHECK(!np_sweep_init(&D, LO, HI, BLOCK, NP_SWEEP_MAX_PER_FRAME + 1, ptr_a));
        CHECK(!np_sweep_init(&D, LO, HI, BLOCK, NB + 1, ptr_a));       /* more a frame than there are blocks */
    }

    np_sweep_free(&A);
    np_sweep_free(&B);
    printf("netplay_sweep_test: %d failure(s)\n", failures);
    return failures;
}
