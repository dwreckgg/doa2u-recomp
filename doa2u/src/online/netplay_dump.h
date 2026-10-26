/*
 * netplay_dump.h - guest RAM snapshots for finding addresses, and the page
 * comparison behind the dirty-page counter. No guest memory here: the
 * caller hands in byte ranges (netplay_diag.c reads the guest). Host-tested
 * (tests/netplay_dump_test.c); tools/netplay/memdiff.py reads the files.
 *
 * Dump file: a 64-byte header, then the bytes of guest [lo, hi).
 *   0  "DOA2DMP1"   8  version (1)   12 lo   16 hi
 *   20 session frame (0xFFFFFFFF outside a session)   24 pad-builder calls
 *   28 tag, 32 bytes, NUL-padded   60 reserved (0)
 * All little-endian.
 */
#ifndef DOA2U_NETPLAY_DUMP_H
#define DOA2U_NETPLAY_DUMP_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP_DUMP_HEADER 64u
#define NP_DUMP_TAG    32u
#define NP_PAGE        4096u

typedef struct {
    uint32_t lo, hi, frame, builder_calls;
    char     tag[NP_DUMP_TAG + 1];
} np_dump_info;

/* Writes the header for `info` (tag truncated to 32 bytes). 1 on success. */
int np_dump_write_header(FILE *f, const np_dump_info *info);
/* Reads and checks a header. 1 on success. */
int np_dump_read_header(FILE *f, np_dump_info *info);

/* Counts the 4 KB pages that differ between `a` and `b` (both `len` bytes,
 * len a multiple of 4096, guest address `va` at offset 0). `bounds` holds
 * nb+1 ascending guest addresses; a changed page whose address lies in
 * [bounds[i], bounds[i+1]) adds one to counts[i]. Returns the total. */
uint32_t np_pages_diff(const uint8_t *a, const uint8_t *b, uint32_t len, uint32_t va,
                       const uint32_t *bounds, int nb, uint32_t *counts);

/* The class query behind the address hunt. bufs[i] (len bytes at guest
 * `va`) belongs to class cls[i] (0..nclass-1). A hit is an aligned value of
 * `width` bytes (1, 2 or 4) that is the same in every buffer of a class and
 * different between every two classes. Calls out() per hit until `limit`
 * hits (0 = no limit); returns the number of hits. */
typedef void (*np_class_hit)(uint32_t va, const uint32_t *values, int nclass, void *ctx);
uint32_t np_dump_classes(const uint8_t *const *bufs, const int *cls, int nbufs, int nclass,
                         uint32_t len, uint32_t va, int width, uint32_t limit,
                         np_class_hit out, void *ctx);

/* Every guest address in buf (len bytes at `va`) where needle starts; calls
 * out(va) per hit, returns the number of hits. */
uint32_t np_dump_find(const uint8_t *buf, uint32_t len, uint32_t va,
                      const uint8_t *needle, uint32_t nlen,
                      void (*out)(uint32_t va, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_DUMP_H */
