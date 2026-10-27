/* netplay_format.c - see netplay_format.h. */
#include "netplay_format.h"
#include <stdlib.h>
#include <string.h>

#define BLOB_MAGIC "DOA2PRM1"
#define RP_MAGIC   "DOA2REP1"
#define RP_VERSION 1u
#define PAD_WIRE   18u

uint32_t np_build_id(const char *commit, int dirty, uint32_t link_stamp)
{
    uint32_t len = 0, id;
    if (!commit || dirty) return link_stamp;
    while (commit[len]) {
        char c = commit[len];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return link_stamp;
        len++;
    }
    if (len < 7) return link_stamp;
    id = np_fnv32(NP_FNV32_INIT, commit, len);
    return id ? id : 1u;
}

uint32_t np_fnv32(uint32_t h, const void *p, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i;
    for (i = 0; i < len; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

uint64_t np_digest_combine(const uint32_t *hashes, int n)
{
    uint64_t total = NP_FNV64_INIT;
    int k;
    for (k = 0; k < n; k++) { total ^= hashes[k]; total *= 1099511628211ull; }
    return total;
}

int np_regions_check(const np_region *r, int n, uint32_t lo, uint32_t hi, char *why, size_t whylen)
{
    int i, j;
    for (i = 0; i < n; i++) {
        if (!r[i].name || r[i].len == 0 || r[i].va < lo || r[i].va >= hi || r[i].len > hi - r[i].va) {
            if (why) snprintf(why, whylen, "region %d (%s) is empty or outside %08X..%08X",
                              i, r[i].name ? r[i].name : "?", lo, hi);
            return 0;
        }
        for (j = 0; j < i; j++) {
            if (r[i].va < r[j].va + r[j].len && r[j].va < r[i].va + r[i].len) {
                if (why) snprintf(why, whylen, "regions %s and %s overlap", r[j].name, r[i].name);
                return 0;
            }
        }
    }
    return 1;
}

uint32_t np_regions_total(const np_region *r, int n)
{
    uint32_t t = 0;
    int i;
    for (i = 0; i < n; i++) t += r[i].len;
    return t;
}

static void put32(uint8_t *d, uint32_t v) { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); d[2] = (uint8_t)(v >> 16); d[3] = (uint8_t)(v >> 24); }
static void put64(uint8_t *d, uint64_t v) { put32(d, (uint32_t)v); put32(d + 4, (uint32_t)(v >> 32)); }
static uint32_t get32(const uint8_t *s) { return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24); }
static uint64_t get64(const uint8_t *s) { return (uint64_t)get32(s) | ((uint64_t)get32(s + 4) << 32); }

uint32_t np_params_blob_size(uint32_t save_len, uint32_t sync_len)
{
    return 8 + 4 + 4 + 8 + 8 + 8 + 4 + save_len + NP_LAYOUT_MAX + 1 + 4 + sync_len;
}

uint32_t np_params_pack(const np_params *p, uint8_t *dst, uint32_t cap)
{
    uint32_t need = np_params_blob_size(p->save_len, p->sync_len);
    uint8_t *d = dst;
    if (!dst || cap < need) return 0;
    memcpy(d, BLOB_MAGIC, 8);                       d += 8;
    put32(d, p->seed);                              d += 4;
    put32(d, p->tick_ms);                           d += 4;
    put64(d, p->qpc);                               d += 8;
    put64(d, p->filetime);                          d += 8;
    put64(d, p->xgscnt);                            d += 8;
    put32(d, p->save_len);                          d += 4;
    if (p->save_len) memcpy(d, p->save, p->save_len);
    d += p->save_len;
    memcpy(d, p->joiner_layout, NP_LAYOUT_MAX);     d += NP_LAYOUT_MAX;
    *d++ = p->joiner_laysel;
    put32(d, p->sync_len);                          d += 4;
    if (p->sync_len) memcpy(d, p->sync, p->sync_len);
    return need;
}

int np_params_unpack(np_params *p, const uint8_t *src, uint32_t len, uint32_t save_len, uint32_t sync_len)
{
    const uint8_t *s = src;
    np_params t;
    memset(&t, 0, sizeof(t));
    if (!src || len != np_params_blob_size(save_len, sync_len) || memcmp(s, BLOB_MAGIC, 8)) return 0;
    s += 8;
    t.seed = get32(s);       s += 4;
    t.tick_ms = get32(s);    s += 4;
    t.qpc = get64(s);        s += 8;
    t.filetime = get64(s);   s += 8;
    t.xgscnt = get64(s);     s += 8;
    if (get32(s) != save_len) return 0;
    s += 4;
    t.save_len = save_len;
    t.save = (uint8_t *)malloc(save_len ? save_len : 1);
    if (!t.save) return 0;
    if (save_len) memcpy(t.save, s, save_len);
    s += save_len;
    memcpy(t.joiner_layout, s, NP_LAYOUT_MAX);    s += NP_LAYOUT_MAX;
    t.joiner_laysel = *s++;
    if (get32(s) != sync_len) { free(t.save); return 0; }
    s += 4;
    t.sync_len = sync_len;
    t.sync = (uint8_t *)malloc(sync_len ? sync_len : 1);
    if (!t.sync) { free(t.save); return 0; }
    if (sync_len) memcpy(t.sync, s, sync_len);
    np_params_free(p);
    *p = t;
    return 1;
}

void np_params_free(np_params *p)
{
    if (!p) return;
    free(p->save);
    free(p->sync);
    p->save = p->sync = NULL;
    p->save_len = p->sync_len = 0;
}

static int w32(FILE *f, uint32_t v) { uint8_t b[4]; put32(b, v); return fwrite(b, 1, 4, f) == 4; }
static int w64(FILE *f, uint64_t v) { uint8_t b[8]; put64(b, v); return fwrite(b, 1, 8, f) == 8; }

static int wpad(FILE *f, const np_pad *p)
{
    uint8_t b[PAD_WIRE];
    b[0] = (uint8_t)p->buttons; b[1] = (uint8_t)(p->buttons >> 8);
    memcpy(b + 2, p->analog, 8);
    b[10] = (uint8_t)p->lx; b[11] = (uint8_t)((uint16_t)p->lx >> 8);
    b[12] = (uint8_t)p->ly; b[13] = (uint8_t)((uint16_t)p->ly >> 8);
    b[14] = (uint8_t)p->rx; b[15] = (uint8_t)((uint16_t)p->rx >> 8);
    b[16] = (uint8_t)p->ry; b[17] = (uint8_t)((uint16_t)p->ry >> 8);
    return fwrite(b, 1, PAD_WIRE, f) == PAD_WIRE;
}

static void rpad(const uint8_t *b, np_pad *p)
{
    p->buttons = (uint16_t)(b[0] | (b[1] << 8));
    memcpy(p->analog, b + 2, 8);
    p->lx = (int16_t)(b[10] | (b[11] << 8));
    p->ly = (int16_t)(b[12] | (b[13] << 8));
    p->rx = (int16_t)(b[14] | (b[15] << 8));
    p->ry = (int16_t)(b[16] | (b[17] << 8));
}

int np_replay_write(FILE *f, const np_replay *r)
{
    uint32_t blen = np_params_blob_size(r->params.save_len, r->params.sync_len), i, k;
    uint8_t *blob = (uint8_t *)malloc(blen);
    int ok;
    if (!f || !blob) { free(blob); return 0; }
    np_params_pack(&r->params, blob, blen);
    ok = fwrite(RP_MAGIC, 1, 8, f) == 8 && w32(f, RP_VERSION) && w32(f, r->build_stamp) &&
         w32(f, (uint32_t)r->role) && w32(f, (uint32_t)r->tag) && w32(f, blen) &&
         fwrite(blob, 1, blen, f) == blen && w32(f, r->n_hash) && w32(f, r->frames);
    free(blob);
    for (i = 0; ok && i < r->frames; i++) {
        ok = wpad(f, &r->pads[2 * i]) && wpad(f, &r->pads[2 * i + 1]) && w64(f, r->digests[i]);
        for (k = 0; ok && k < r->n_hash; k++) ok = w32(f, r->hashes[(size_t)i * r->n_hash + k]);
    }
    return ok;
}

void np_replay_free(np_replay *r)
{
    if (!r) return;
    np_params_free(&r->params);
    free(r->pads);
    free(r->digests);
    free(r->hashes);
    memset(r, 0, sizeof(*r));
}

const char *np_replay_read(FILE *f, np_replay *r, uint32_t save_len, uint32_t sync_len, uint32_t n_hash)
{
    uint8_t h[28], *blob = NULL, fb[PAD_WIRE * 2 + 8];
    uint32_t blen, i, k;
    const char *err = NULL;
    np_replay_free(r);
    if (!f) return "cannot open the replay file";
#define RD(p, n) do { if (fread((p), 1, (n), f) != (size_t)(n)) { err = "replay file is truncated"; goto out; } } while (0)
    RD(h, 24);
    if (memcmp(h, RP_MAGIC, 8)) { err = "not a DOA2U replay file"; goto out; }
    if (get32(h + 8) != RP_VERSION) { err = "unsupported replay version"; goto out; }
    r->build_stamp = get32(h + 12);
    r->role = (int)get32(h + 16);
    r->tag = (int)get32(h + 20);
    RD(h, 4);
    blen = get32(h);
    if (blen != np_params_blob_size(save_len, sync_len)) { err = "replay was recorded with different state regions"; goto out; }
    blob = (uint8_t *)malloc(blen);
    if (!blob) { err = "out of memory"; goto out; }
    RD(blob, blen);
    if (!np_params_unpack(&r->params, blob, blen, save_len, sync_len)) { err = "replay start parameters are damaged"; goto out; }
    RD(h, 8);
    if (get32(h) != n_hash) { err = "replay was recorded with different state regions"; goto out; }
    r->n_hash = n_hash;
    r->frames = get32(h + 4);
    if (r->frames == 0 || r->frames > NP_REPLAY_MAX_FRAMES) { err = "replay has no frames"; goto out; }
    r->pads = (np_pad *)malloc((size_t)r->frames * 2 * sizeof(np_pad));
    r->digests = (uint64_t *)malloc((size_t)r->frames * sizeof(uint64_t));
    r->hashes = (uint32_t *)malloc((size_t)r->frames * (n_hash ? n_hash : 1) * sizeof(uint32_t));
    if (!r->pads || !r->digests || !r->hashes) { err = "out of memory"; goto out; }
    for (i = 0; i < r->frames; i++) {
        RD(fb, sizeof(fb));
        rpad(fb, &r->pads[2 * i]);
        rpad(fb + PAD_WIRE, &r->pads[2 * i + 1]);
        r->digests[i] = get64(fb + 2 * PAD_WIRE);
        for (k = 0; k < n_hash; k++) {
            RD(h, 4);
            r->hashes[(size_t)i * n_hash + k] = get32(h);
        }
    }
#undef RD
out:
    free(blob);
    if (err) np_replay_free(r);
    return err;
}
