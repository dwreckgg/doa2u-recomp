/* netplay_format.c: digest, region checks, start-parameter blob, replay file. */
#include "online/netplay_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void fill(uint8_t *b, uint32_t n, uint8_t start)
{
    uint32_t i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(start + 7 * i);
}

/* Every field distinct and non-zero, so a swapped or dropped field shows. */
static void make_params(np_params *p, uint32_t save_len, uint32_t sync_len)
{
    memset(p, 0, sizeof(*p));
    p->seed = 0x11223344u;
    p->tick_ms = 0x55667788u;
    p->qpc = 0x0102030405060708ull;
    p->filetime = 0x1112131415161718ull;
    p->xgscnt = 0x2122232425262728ull;
    p->save_len = save_len;
    p->save = (uint8_t *)malloc(save_len);
    fill(p->save, save_len, 3);
    fill(p->joiner_layout, NP_LAYOUT_MAX, 100);
    p->joiner_laysel = 9;
    p->sync_len = sync_len;
    p->sync = (uint8_t *)malloc(sync_len);
    fill(p->sync, sync_len, 50);
}

static int params_equal(const np_params *a, const np_params *b)
{
    return a->seed == b->seed && a->tick_ms == b->tick_ms && a->qpc == b->qpc &&
           a->filetime == b->filetime && a->xgscnt == b->xgscnt &&
           a->save_len == b->save_len && !memcmp(a->save, b->save, a->save_len) &&
           !memcmp(a->joiner_layout, b->joiner_layout, NP_LAYOUT_MAX) &&
           a->joiner_laysel == b->joiner_laysel && a->sync_len == b->sync_len &&
           !memcmp(a->sync, b->sync, a->sync_len);
}

static void test_digest(void)
{
    uint32_t h12[2] = { 1, 2 }, h21[2] = { 2, 1 };
    CHECK(np_fnv32(NP_FNV32_INIT, "", 0) == 0x811C9DC5u);
    CHECK(np_fnv32(NP_FNV32_INIT, "a", 1) == 0xE40C292Cu);          /* FNV-1a reference value */
    CHECK(np_digest_combine(h12, 0) == NP_FNV64_INIT);
    CHECK(np_digest_combine(h12, 2) != np_digest_combine(h21, 2));  /* order matters */
}

static void test_regions(void)
{
    char why[128];
    np_region ok[3] = { { 0x1000, 0x10, "a" }, { 0x1010, 0x08, "b" }, { 0x2000, 4, "c" } };   /* a and b touch */
    np_region empty[1] = { { 0x1000, 0, "e" } };
    np_region below[1] = { { 0x0FFF, 4, "lo" } };
    np_region past[1] = { { 0x4FFF, 2, "hi" } };
    np_region overlap[2] = { { 0x1000, 0x10, "a" }, { 0x100F, 4, "b" } };
    CHECK(np_regions_check(ok, 3, 0x1000, 0x5000, why, sizeof why) == 1);
    CHECK(np_regions_total(ok, 3) == 0x1C);
    CHECK(np_regions_check(empty, 1, 0x1000, 0x5000, why, sizeof why) == 0);
    CHECK(np_regions_check(below, 1, 0x1000, 0x5000, why, sizeof why) == 0);
    CHECK(np_regions_check(past, 1, 0x1000, 0x5000, why, sizeof why) == 0);
    CHECK(np_regions_check(overlap, 2, 0x1000, 0x5000, why, sizeof why) == 0);
    CHECK(strstr(why, "a and b overlap") != NULL);
}

static void test_blob(void)
{
    np_params p, q;
    uint32_t n = np_params_blob_size(37, 5), w;
    uint8_t *blob = (uint8_t *)malloc(n + 1);
    memset(&q, 0, sizeof(q));
    make_params(&p, 37, 5);
    CHECK(n == 65u + 37u + 5u);
    CHECK(np_params_pack(&p, blob, n - 1) == 0);                    /* too small */
    w = np_params_pack(&p, blob, n);
    CHECK(w == n);
    CHECK(np_params_unpack(&q, blob, n, 37, 5) == 1);
    CHECK(params_equal(&p, &q));
    CHECK(np_params_unpack(&q, blob, n - 1, 37, 5) == 0);           /* truncated */
    CHECK(np_params_unpack(&q, blob, n, 37, 6) == 0);               /* this build has other regions */
    CHECK(np_params_unpack(&q, blob, n, 36, 5) == 0);               /* or another save size */
    blob[0] = 'X';
    CHECK(np_params_unpack(&q, blob, n, 37, 5) == 0);               /* not a blob */
    CHECK(params_equal(&p, &q));                                    /* failures leave it alone */
    np_params_free(&p);
    np_params_free(&q);
    free(blob);
}

static void test_replay(void)
{
    np_replay r, back;
    FILE *f;
    long size;
    uint32_t i;
    char path[] = "netplay_format_test.dnr";
    memset(&r, 0, sizeof(r));
    memset(&back, 0, sizeof(back));
    make_params(&r.params, 20, 6);
    r.build_stamp = 0xABCDEF01u; r.role = 1; r.tag = 1;
    r.n_hash = 4; r.frames = 3;
    r.pads = (np_pad *)calloc(6, sizeof(np_pad));
    r.digests = (uint64_t *)calloc(3, sizeof(uint64_t));
    r.hashes = (uint32_t *)calloc(12, sizeof(uint32_t));
    for (i = 0; i < 6; i++) { r.pads[i].buttons = (uint16_t)(0x100 + i); r.pads[i].lx = (int16_t)(-1000 - (int)i); r.pads[i].analog[7] = (uint8_t)i; }
    for (i = 0; i < 3; i++) r.digests[i] = 0x1000000000000000ull + i;
    for (i = 0; i < 12; i++) r.hashes[i] = 0xC0DE0000u + i;

    f = fopen(path, "wb");
    CHECK(np_replay_write(f, &r) == 1);
    fclose(f);

    f = fopen(path, "rb");
    CHECK(np_replay_read(f, &back, 20, 6, 4) == NULL);
    fclose(f);
    CHECK(back.build_stamp == r.build_stamp && back.role == 1 && back.tag == 1);
    CHECK(params_equal(&r.params, &back.params));
    CHECK(back.frames == 3 && back.n_hash == 4);
    CHECK(!memcmp(back.pads, r.pads, 6 * sizeof(np_pad)));
    CHECK(!memcmp(back.digests, r.digests, 3 * sizeof(uint64_t)));
    CHECK(!memcmp(back.hashes, r.hashes, 12 * sizeof(uint32_t)));

    f = fopen(path, "rb");
    CHECK(strcmp(np_replay_read(f, &back, 20, 6, 5), "replay was recorded with different state regions") == 0);
    CHECK(back.frames == 0 && back.pads == NULL);                   /* a failed read leaves nothing */
    fclose(f);
    f = fopen(path, "rb");
    CHECK(strcmp(np_replay_read(f, &back, 20, 7, 4), "replay was recorded with different state regions") == 0);
    fclose(f);

    /* truncated: drop the last byte */
    f = fopen(path, "rb");
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    {
        uint8_t *all = (uint8_t *)malloc((size_t)size);
        fread(all, 1, (size_t)size, f);
        fclose(f);
        f = fopen(path, "wb");
        fwrite(all, 1, (size_t)size - 1, f);
        fclose(f);
        f = fopen(path, "rb");
        CHECK(strcmp(np_replay_read(f, &back, 20, 6, 4), "replay file is truncated") == 0);
        fclose(f);
        all[0] = 'Z';
        f = fopen(path, "wb");
        fwrite(all, 1, (size_t)size, f);
        fclose(f);
        f = fopen(path, "rb");
        CHECK(strcmp(np_replay_read(f, &back, 20, 6, 4), "not a DOA2U replay file") == 0);
        fclose(f);
        free(all);
    }
    remove(path);
    np_replay_free(&r);
    np_replay_free(&back);
}

int main(void)
{
    test_digest();
    test_regions();
    test_blob();
    test_replay();
    printf("netplay_format_test: %d failure(s)\n", failures);
    return failures;
}
