/* netplay_dump.c - see netplay_dump.h. */
#include "netplay_dump.h"
#include <string.h>

#define DUMP_MAGIC   "DOA2DMP1"
#define DUMP_VERSION 1u

static void put32(uint8_t *d, uint32_t v) { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); d[2] = (uint8_t)(v >> 16); d[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *s) { return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24); }

int np_dump_write_header(FILE *f, const np_dump_info *info)
{
    uint8_t h[NP_DUMP_HEADER];
    size_t n;
    if (!f || !info) return 0;
    memset(h, 0, sizeof h);
    memcpy(h, DUMP_MAGIC, 8);
    put32(h + 8, DUMP_VERSION);
    put32(h + 12, info->lo);
    put32(h + 16, info->hi);
    put32(h + 20, info->frame);
    put32(h + 24, info->builder_calls);
    n = strlen(info->tag);
    memcpy(h + 28, info->tag, n > NP_DUMP_TAG ? NP_DUMP_TAG : n);
    return fwrite(h, 1, sizeof h, f) == sizeof h;
}

int np_dump_read_header(FILE *f, np_dump_info *info)
{
    uint8_t h[NP_DUMP_HEADER];
    if (!f || !info || fread(h, 1, sizeof h, f) != sizeof h) return 0;
    if (memcmp(h, DUMP_MAGIC, 8) || get32(h + 8) != DUMP_VERSION) return 0;
    info->lo = get32(h + 12);
    info->hi = get32(h + 16);
    info->frame = get32(h + 20);
    info->builder_calls = get32(h + 24);
    memcpy(info->tag, h + 28, NP_DUMP_TAG);
    info->tag[NP_DUMP_TAG] = 0;
    return info->hi > info->lo;
}

uint32_t np_pages_diff(const uint8_t *a, const uint8_t *b, uint32_t len, uint32_t va,
                       const uint32_t *bounds, int nb, uint32_t *counts)
{
    uint32_t off, total = 0;
    int i;
    for (i = 0; i < nb; i++) counts[i] = 0;
    for (off = 0; off + NP_PAGE <= len; off += NP_PAGE) {
        uint32_t page_va = va + off;
        if (!memcmp(a + off, b + off, NP_PAGE)) continue;
        total++;
        for (i = 0; i < nb; i++)
            if (page_va >= bounds[i] && page_va < bounds[i + 1]) { counts[i]++; break; }
    }
    return total;
}

static uint32_t value_at(const uint8_t *b, uint32_t off, int width)
{
    if (width == 1) return b[off];
    if (width == 2) return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8);
    return get32(b + off);
}

uint32_t np_dump_classes(const uint8_t *const *bufs, const int *cls, int nbufs, int nclass,
                         uint32_t len, uint32_t va, int width, uint32_t limit,
                         np_class_hit out, void *ctx)
{
    uint32_t off, hits = 0, values[16];
    int seen[16], i, j, ok;
    if (nclass < 1 || nclass > 16 || (width != 1 && width != 2 && width != 4)) return 0;
    for (off = 0; off + (uint32_t)width <= len; off += (uint32_t)width) {
        for (j = 0; j < nclass; j++) seen[j] = 0;
        ok = 1;
        for (i = 0; i < nbufs && ok; i++) {
            uint32_t v = value_at(bufs[i], off, width);
            int c = cls[i];
            if (!seen[c]) { seen[c] = 1; values[c] = v; }
            else if (values[c] != v) ok = 0;
        }
        for (i = 0; i < nclass && ok; i++) if (!seen[i]) ok = 0;     /* an empty class matches nothing */
        for (i = 0; i < nclass && ok; i++)
            for (j = i + 1; j < nclass && ok; j++)
                if (values[i] == values[j]) ok = 0;
        if (!ok) continue;
        hits++;
        if (out) out(va + off, values, nclass, ctx);
        if (limit && hits >= limit) break;
    }
    return hits;
}

uint32_t np_dump_find(const uint8_t *buf, uint32_t len, uint32_t va,
                      const uint8_t *needle, uint32_t nlen,
                      void (*out)(uint32_t va, void *ctx), void *ctx)
{
    uint32_t off, hits = 0;
    if (!nlen || nlen > len) return 0;
    for (off = 0; off + nlen <= len; off++) {
        if (buf[off] != needle[0] || memcmp(buf + off, needle, nlen)) continue;
        hits++;
        if (out) out(va + off, ctx);
    }
    return hits;
}
