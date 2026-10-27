/* netplay_handoff.c - see netplay_handoff.h.
 *
 * Layout (little-endian): "DNH1", version u16, build_stamp u32, role u8,
 * battle u8, delay u8, local_port u16, peer_ip[4], peer_port u16, token u32,
 * seed u32, joiner_layout[NP_LAYOUT_MAX], joiner_laysel u8, n_files u32, then
 * per file: name length u16, the name (no terminator), data length u32, the
 * data. Every length is checked before it is used, and the bytes must end
 * exactly where the last file does. */
#include "netplay_handoff.h"

#include <stdlib.h>
#include <string.h>

#define HANDOFF_VERSION 1u

static int comp_char_ok(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

int np_handoff_name_ok(const char *name)
{
    const char *p, *start;
    size_t len;
    int comps = 0;
    if (!name) return 0;
    len = strlen(name);
    if (len == 0 || len >= NP_HANDOFF_NAME_MAX) return 0;
    if (strncmp(name, "UserData/", 9) != 0 && strncmp(name, "TitleData/", 10) != 0) return 0;
    for (start = p = name;; p++) {
        if (*p == '/' || *p == 0) {
            size_t n = (size_t)(p - start);
            if (n == 0) return 0;                                 /* empty component, trailing '/' */
            if ((n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.')) return 0;
            comps++;
            if (*p == 0) break;
            start = p + 1;
        } else if (!comp_char_ok(*p)) {
            return 0;                                             /* '\\', ':', spaces, anything else */
        }
    }
    return comps >= 2 && comps <= 3;                              /* folder [/ profile folder] / file */
}

int np_handoff_add_file(np_handoff *h, const char *name, const void *data, uint32_t len)
{
    np_handoff_file *f;
    if (!h || h->n_files >= NP_HANDOFF_MAX_FILES || len > NP_HANDOFF_FILE_MAX || !np_handoff_name_ok(name)) return 0;
    f = &h->files[h->n_files];
    f->data = (uint8_t *)malloc(len ? len : 1);
    if (!f->data) return 0;
    if (len) memcpy(f->data, data, len);
    f->len = len;
    strcpy(f->name, name);
    h->n_files++;
    return 1;
}

void np_handoff_free(np_handoff *h)
{
    uint32_t i;
    if (!h) return;
    for (i = 0; i < h->n_files && i < NP_HANDOFF_MAX_FILES; i++) {
        free(h->files[i].data);
        h->files[i].data = NULL;
        h->files[i].len = 0;
    }
    h->n_files = 0;
}

/* ── writer ─────────────────────────────────────────────────────────── */

typedef struct { uint8_t *p; uint32_t n, cap; } wbuf;

static void put(wbuf *w, const void *src, uint32_t len)
{
    if (w->p && w->n + len <= w->cap) memcpy(w->p + w->n, src, len);
    w->n += len;
}
static void put8(wbuf *w, uint8_t v) { put(w, &v, 1); }
static void put16(wbuf *w, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(w, b, 2); }
static void put32(wbuf *w, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    put(w, b, 4);
}

uint32_t np_handoff_pack(const np_handoff *h, uint8_t *dst, uint32_t cap)
{
    wbuf w;
    uint32_t i;
    if (!h || h->n_files > NP_HANDOFF_MAX_FILES) return 0;
    w.p = NULL; w.n = 0; w.cap = 0;                               /* first pass: the size */
    for (;;) {
        put(&w, "DNH1", 4);
        put16(&w, HANDOFF_VERSION);
        put32(&w, h->build_stamp);
        put8(&w, h->role);
        put8(&w, h->battle);
        put8(&w, h->delay);
        put16(&w, h->local_port);
        put(&w, h->peer_ip, 4);
        put16(&w, h->peer_port);
        put32(&w, h->token);
        put32(&w, h->seed);
        put(&w, h->joiner_layout, NP_LAYOUT_MAX);
        put8(&w, h->joiner_laysel);
        put32(&w, h->n_files);
        for (i = 0; i < h->n_files; i++) {
            uint16_t nl = (uint16_t)strlen(h->files[i].name);
            put16(&w, nl);
            put(&w, h->files[i].name, nl);
            put32(&w, h->files[i].len);
            put(&w, h->files[i].data, h->files[i].len);
        }
        if (w.p || !dst) return w.n;                              /* written, or only sized */
        if (cap < w.n) return 0;
        w.p = dst; w.cap = cap; w.n = 0;
    }
}

/* ── reader ─────────────────────────────────────────────────────────── */

typedef struct { const uint8_t *p; uint32_t n, at; int bad; } rbuf;

static const uint8_t *take(rbuf *r, uint32_t len)
{
    const uint8_t *q;
    if (r->bad || len > r->n - r->at) { r->bad = 1; return NULL; }
    q = r->p + r->at;
    r->at += len;
    return q;
}
static uint8_t get8(rbuf *r) { const uint8_t *q = take(r, 1); return q ? q[0] : 0; }
static uint16_t get16(rbuf *r) { const uint8_t *q = take(r, 2); return q ? (uint16_t)(q[0] | (q[1] << 8)) : 0; }
static uint32_t get32(rbuf *r)
{
    const uint8_t *q = take(r, 4);
    return q ? (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24) : 0;
}

const char *np_handoff_unpack(np_handoff *h, const uint8_t *src, uint32_t len)
{
    rbuf r;
    const uint8_t *q;
    uint32_t n, i;
    if (!h) return "no destination";
    memset(h, 0, sizeof *h);
    if (!src) return "no data";
    r.p = src; r.n = len; r.at = 0; r.bad = 0;
    q = take(&r, 4);
    if (!q || memcmp(q, "DNH1", 4) != 0) return "not a session hand-off";
    if (get16(&r) != HANDOFF_VERSION) return "a different hand-off version";
    h->build_stamp = get32(&r);
    h->role = get8(&r);
    h->battle = get8(&r);
    h->delay = get8(&r);
    h->local_port = get16(&r);
    q = take(&r, 4);
    if (q) memcpy(h->peer_ip, q, 4);
    h->peer_port = get16(&r);
    h->token = get32(&r);
    h->seed = get32(&r);
    q = take(&r, NP_LAYOUT_MAX);
    if (q) memcpy(h->joiner_layout, q, NP_LAYOUT_MAX);
    h->joiner_laysel = get8(&r);
    n = get32(&r);
    if (r.bad) return "the hand-off is cut short";
    if (h->role > NP_HANDOFF_ROLE_JOINER) return "an unknown role";
    if (n > NP_HANDOFF_MAX_FILES) return "too many files";
    for (i = 0; i < n; i++) {
        char name[NP_HANDOFF_NAME_MAX];
        uint16_t nl = get16(&r);
        uint32_t dl;
        const uint8_t *nm, *data;
        if (r.bad || nl == 0 || nl >= NP_HANDOFF_NAME_MAX) { np_handoff_free(h); return r.bad ? "the hand-off is cut short" : "a bad file name"; }
        nm = take(&r, nl);
        if (!nm) { np_handoff_free(h); return "the hand-off is cut short"; }
        memcpy(name, nm, nl);
        name[nl] = 0;
        if (memchr(name, 0, nl) || !np_handoff_name_ok(name)) { np_handoff_free(h); return "a refused file name"; }
        dl = get32(&r);
        if (r.bad) { np_handoff_free(h); return "the hand-off is cut short"; }
        if (dl > NP_HANDOFF_FILE_MAX) { np_handoff_free(h); return "a file too large"; }
        data = take(&r, dl);
        if (!data) { np_handoff_free(h); return "the hand-off is cut short"; }
        if (!np_handoff_add_file(h, name, data, dl)) { np_handoff_free(h); return "out of memory"; }
    }
    if (r.at != r.n) { np_handoff_free(h); return "extra bytes after the hand-off"; }
    return NULL;
}
