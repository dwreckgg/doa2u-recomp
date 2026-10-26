/* netplay_dump.c: the dump header round trip and the page comparison. */
#include "online/netplay_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void test_header(void)
{
    np_dump_info in, out;
    FILE *f = fopen("netplay_dump_test.bin", "w+b");
    memset(&in, 0, sizeof in);
    memset(&out, 0, sizeof out);
    in.lo = 0x00000000u; in.hi = 0x05000000u; in.frame = 1234; in.builder_calls = 99;
    strcpy(in.tag, "charsel_single_with_a_tag_longer");           /* the full 32 bytes */
    CHECK(np_dump_write_header(f, &in) == 1);
    CHECK(ftell(f) == 64);
    rewind(f);
    CHECK(np_dump_read_header(f, &out) == 1);
    CHECK(out.lo == 0 && out.hi == 0x05000000u && out.frame == 1234 && out.builder_calls == 99);
    CHECK(strcmp(out.tag, "charsel_single_with_a_tag_longer") == 0);
    rewind(f);
    fputc('X', f);
    rewind(f);
    CHECK(np_dump_read_header(f, &out) == 0);                   /* not a dump */
    fclose(f);
    remove("netplay_dump_test.bin");
}

static void test_pages(void)
{
    const uint32_t len = 8 * 4096, va = 0x00100000u;
    uint8_t *a = (uint8_t *)calloc(len, 1), *b = (uint8_t *)calloc(len, 1);
    uint32_t bounds[3] = { 0x00100000u, 0x00103000u, 0x00108000u }, counts[2];
    CHECK(np_pages_diff(a, b, len, va, bounds, 2, counts) == 0);
    CHECK(counts[0] == 0 && counts[1] == 0);
    b[0] = 1;                    /* page 0: first byte */
    b[3 * 4096 - 1] = 1;         /* page 2: last byte, still the first bucket */
    b[3 * 4096] = 1;             /* page 3: first byte of the second bucket */
    b[3 * 4096 + 100] = 1;       /* same page again: counted once */
    b[7 * 4096 + 4095] = 1;      /* page 7: the last byte of the range */
    CHECK(np_pages_diff(a, b, len, va, bounds, 2, counts) == 4);
    CHECK(counts[0] == 2 && counts[1] == 2);
    free(a);
    free(b);
}

static uint32_t s_hit_va[8], s_hit_v0[8], s_hit_v1[8];
static int s_hits;
static void on_class(uint32_t va, const uint32_t *values, int nclass, void *ctx)
{
    (void)nclass; (void)ctx;
    if (s_hits < 8) { s_hit_va[s_hits] = va; s_hit_v0[s_hits] = values[0]; s_hit_v1[s_hits] = values[1]; }
    s_hits++;
}
static void on_find(uint32_t va, void *ctx) { (void)ctx; if (s_hits < 8) s_hit_va[s_hits] = va; s_hits++; }

static void test_classes(void)
{
    /* class 0 = two dumps at Single select, class 1 = one dump at Tag select */
    uint8_t s1[16], s2[16], t1[16];
    const uint8_t *bufs[3] = { s1, s2, t1 };
    int cls[3] = { 0, 0, 1 };
    memset(s1, 0, 16); memset(s2, 0, 16); memset(t1, 0, 16);
    s1[3] = 0; s2[3] = 0; t1[3] = 1;           /* the tag flag: a hit */
    s1[5] = 7; s2[5] = 8; t1[5] = 9;           /* differs inside class 0: no hit */
    s1[9] = 4; s2[9] = 4; t1[9] = 4;           /* same everywhere: no hit */
    s1[12] = 0x34; s2[12] = 0x34; t1[12] = 0x35; s1[13] = s2[13] = t1[13] = 0x12;
    s_hits = 0;
    CHECK(np_dump_classes(bufs, cls, 3, 2, 16, 0x1000, 1, 0, on_class, NULL) == 2);
    CHECK(s_hit_va[0] == 0x1003 && s_hit_v0[0] == 0 && s_hit_v1[0] == 1);
    CHECK(s_hit_va[1] == 0x100C && s_hit_v0[1] == 0x34 && s_hit_v1[1] == 0x35);
    s_hits = 0;
    CHECK(np_dump_classes(bufs, cls, 3, 2, 16, 0x1000, 2, 0, on_class, NULL) == 2);   /* words at +2, +12 */
    CHECK(s_hit_va[0] == 0x1002 && s_hit_v0[0] == 0x0000 && s_hit_v1[0] == 0x0100);
    CHECK(s_hit_va[1] == 0x100C && s_hit_v0[1] == 0x1234 && s_hit_v1[1] == 0x1235);
    s_hits = 0;
    CHECK(np_dump_classes(bufs, cls, 3, 2, 16, 0x1000, 1, 1, on_class, NULL) == 1);   /* limit */
    CHECK(np_dump_classes(bufs, cls, 3, 2, 16, 0x1000, 3, 0, NULL, NULL) == 0);       /* bad width */
}

static void test_find(void)
{
    uint8_t buf[32];
    const uint8_t needle[3] = { 0xDE, 0xAD, 0x01 };
    memset(buf, 0xDE, sizeof buf);
    memcpy(buf + 5, needle, 3);
    memcpy(buf + 29, needle, 3);              /* at the very end */
    s_hits = 0;
    CHECK(np_dump_find(buf, 32, 0x2000, needle, 3, on_find, NULL) == 2);
    CHECK(s_hit_va[0] == 0x2005 && s_hit_va[1] == 0x201D);
    CHECK(np_dump_find(buf, 32, 0x2000, needle, 0, on_find, NULL) == 0);
}

int main(void)
{
    test_header();
    test_pages();
    test_classes();
    test_find();
    printf("netplay_dump_test: %d failure(s)\n", failures);
    return failures;
}
