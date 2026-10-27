/* Host test for the APU voice resampler's interpolator (src/apu/apu_resample.c). */
#include <math.h>
#include <stdio.h>
#include "apu/apu_resample.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    float h[8];
    int i, k;
    apu_lagrange8_init();
    /* The weights at any phase sum to one (DC passes unchanged). */
    for (k = 0; k < 16; k++) {
        float w[8], sum = 0;
        apu_lagrange8_weights(k / 16.0, w);
        for (i = 0; i < 8; i++) sum += w[i];
        CHECK(fabsf(sum - 1.0f) < 1e-5f);
    }
    /* Phase 0 is the sample at node 3 exactly. */
    for (i = 0; i < 8; i++) h[i] = (float)(i * i) - 3.0f * i + 1.0f;
    CHECK(fabsf(apu_lagrange8(h, 0.0) - h[3]) < 1e-5f);
    /* A cubic through the nodes is reproduced between nodes 3 and 4. */
    for (i = 0; i < 8; i++) h[i] = 0.01f * i * i * i - 0.1f * i * i + 0.2f * i - 0.3f;
    for (k = 0; k < 8; k++) {
        const double x = 3.0 + k / 8.0;
        const double want = 0.01 * x * x * x - 0.1 * x * x + 0.2 * x - 0.3;
        CHECK(fabs(apu_lagrange8(h, k / 8.0) - want) < 2e-4);
    }
    /* A tone at 0.2 of the input rate: over 20x closer than linear (about 43x in theory;
     * the gain shrinks toward Nyquist, about 3x at 0.35). */
    {
        double err8 = 0, errl = 0;
        for (k = 0; k < 64; k++) {
            const double t = (k + 0.5) / 64.0, f = 0.2 * 2.0 * 3.14159265358979;
            const double want = sin(f * (3.0 + t));
            for (i = 0; i < 8; i++) h[i] = (float)sin(f * i);
            err8 += fabs(apu_lagrange8(h, t) - want);
            errl += fabs(h[3] + (h[4] - h[3]) * t - want);
        }
        CHECK(err8 * 20.0 < errl);
    }
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
