/* Host test for the APU's stereo output mix (src/apu/apu_downmix.c). */
#include <math.h>
#include <stdio.h>
#include "apu/apu_downmix.h"

static int failures;
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-5f)
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void mix_one(int bin, float v, float *l, float *r)
{
    float bins[APU_DOWNMIX_BINS] = {0};
    float gain[APU_DOWNMIX_BINS];
    int i;
    for (i = 0; i < APU_DOWNMIX_BINS; i++) gain[i] = 1.0f;
    bins[bin] = v;
    apu_stereo_downmix(bins, gain, 1.0f, l, r);
}

int main(void)
{
    float l, r;
    float bins[APU_DOWNMIX_BINS] = {0}, gain[APU_DOWNMIX_BINS];
    int i;
    /* Front pair: straight through. */
    mix_one(0, 0.5f, &l, &r); CHECK(NEAR(l, 0.5f) && NEAR(r, 0.0f));
    mix_one(1, 0.5f, &l, &r); CHECK(NEAR(l, 0.0f) && NEAR(r, 0.5f));
    /* Centre: -3 dB into both sides (the hardware's stereo program). */
    mix_one(2, 0.5f, &l, &r); CHECK(NEAR(l, 0.5f * 0.70710678f) && NEAR(r, 0.5f * 0.70710678f));
    /* LFE: dropped in stereo. */
    mix_one(3, 0.5f, &l, &r); CHECK(NEAR(l, 0.0f) && NEAR(r, 0.0f));
    /* Surround pair: -3 dB into its own side. */
    mix_one(4, 0.5f, &l, &r); CHECK(NEAR(l, 0.5f * 0.70710678f) && NEAR(r, 0.0f));
    mix_one(5, 0.5f, &l, &r); CHECK(NEAR(l, 0.0f) && NEAR(r, 0.5f * 0.70710678f));
    /* Crosstalk speaker bins (3D voices): front and back pairs, unchanged. */
    mix_one(6, 0.25f, &l, &r); CHECK(NEAR(l, 0.25f) && NEAR(r, 0.0f));
    mix_one(8, 0.25f, &l, &r); CHECK(NEAR(l, 0.25f) && NEAR(r, 0.0f));
    mix_one(7, 0.25f, &l, &r); CHECK(NEAR(l, 0.0f) && NEAR(r, 0.25f));
    mix_one(9, 0.25f, &l, &r); CHECK(NEAR(l, 0.0f) && NEAR(r, 0.25f));
    /* Headroom: each bin is scaled back up by its own gain; crosstalk by the HRTF gain. */
    for (i = 0; i < APU_DOWNMIX_BINS; i++) gain[i] = 1.0f;
    gain[2] = 4.0f;
    bins[2] = 0.1f; bins[6] = 0.05f;
    apu_stereo_downmix(bins, gain, 2.0f, &l, &r);
    CHECK(NEAR(l, 0.4f * 0.70710678f + 0.1f) && NEAR(r, 0.4f * 0.70710678f));
    /* Clipped to [-1, 1]. */
    mix_one(0, 3.0f, &l, &r); CHECK(NEAR(l, 1.0f));
    mix_one(1, -3.0f, &l, &r); CHECK(NEAR(r, -1.0f));
    /* 5.1: FL FR C LFE BL BR straight through, XTLK front pair into FL/FR, back pair into BL/BR. */
    {
        float b6[APU_DOWNMIX_BINS] = {0}, g6[APU_DOWNMIX_BINS], out[6];
        int c, k;
        for (c = 0; c < APU_DOWNMIX_BINS; c++) g6[c] = 1.0f;
        for (c = 0; c < 6; c++) {
            for (k = 0; k < APU_DOWNMIX_BINS; k++) b6[k] = 0.0f;
            b6[c] = 0.25f;
            apu_surround_mix(b6, g6, 1.0f, out);
            for (k = 0; k < 6; k++) CHECK(NEAR(out[k], k == c ? 0.25f : 0.0f));
        }
        for (c = 0; c < APU_DOWNMIX_BINS; c++) b6[c] = 0.0f;
        b6[6] = 0.1f; b6[7] = 0.2f; b6[8] = 0.3f; b6[9] = 0.4f;
        apu_surround_mix(b6, g6, 2.0f, out);
        CHECK(NEAR(out[0], 0.2f) && NEAR(out[1], 0.4f) && NEAR(out[2], 0.0f) && NEAR(out[3], 0.0f));
        CHECK(NEAR(out[4], 0.6f) && NEAR(out[5], 0.8f));
        b6[0] = 5.0f; apu_surround_mix(b6, g6, 1.0f, out); CHECK(NEAR(out[0], 1.0f));
    }
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
