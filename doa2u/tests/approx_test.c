/* The lifted SSE approximation instructions (RCPSS, RSQRTSS, RCPPS, RSQRTPS in
 * game/recomp/recomp_types.h) give the same bits on every CPU. The hardware's own
 * approximations differ between processor makes and generations, which two PCs in
 * lockstep cannot have; these compute 1/x and 1/sqrt(x) exactly instead (IEEE
 * division and square root are the same everywhere), keeping the hardware's edges. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/recomp/recomp_types.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    static const float xs[] = { 1.0f, 2.0f, 3.0f, 0.1f, 7.25f, 1234.5f, 1e-30f, 3e30f, 0.75f, 65536.0f };
    xmm128_t a, r, s;
    size_t i;

    for (i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        float x = xs[i];
        CHECK(fb(xmm_rcpss(x)) == fb(1.0f / x));
        CHECK(fb(xmm_rcpss(-x)) == fb(1.0f / -x));
        CHECK(fb(xmm_rsqrtss(x)) == fb(1.0f / sqrtf(x)));
    }

    /* the hardware's edges */
    CHECK(fb(xmm_rcpss(0.0f)) == 0x7F800000u);            /* +inf */
    CHECK(fb(xmm_rcpss(-0.0f)) == 0xFF800000u);           /* -inf */
    CHECK(fb(xmm_rsqrtss(0.0f)) == 0x7F800000u);
    CHECK(fb(xmm_rsqrtss(-0.0f)) == 0xFF800000u);
    CHECK(isnan(xmm_rsqrtss(-1.0f)));
    CHECK(fb(xmm_rcpss(1e-40f)) == 0x7F800000u);          /* a denormal input counts as zero */
    CHECK(fb(xmm_rcpss(-1e-40f)) == 0xFF800000u);
    CHECK(fb(xmm_rsqrtss(1e-40f)) == 0x7F800000u);
    CHECK(fb(xmm_rcpss(3e38f)) == 0x00000000u);           /* a denormal result flushes to zero */
    CHECK(fb(xmm_rcpss(-3e38f)) == 0x80000000u);
    CHECK(fb(xmm_rcpss(INFINITY)) == 0x00000000u);
    CHECK(fb(xmm_rsqrtss(INFINITY)) == 0x00000000u);

    /* the four-lane forms are the one-lane forms per lane */
    for (i = 0; i + 4 <= sizeof xs / sizeof xs[0]; i += 2) {
        int k;
        memcpy(a.f, &xs[i], sizeof a.f);
        r = xmm_rcpps(a);
        s = xmm_rsqrtps(a);
        for (k = 0; k < 4; k++) {
            CHECK(fb(r.f[k]) == fb(xmm_rcpss(a.f[k])));
            CHECK(fb(s.f[k]) == fb(xmm_rsqrtss(a.f[k])));
        }
    }

    printf("approx_test: %d failure(s)\n", failures);
    return failures;
}
