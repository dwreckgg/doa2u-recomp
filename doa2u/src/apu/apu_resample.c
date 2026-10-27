/* 8-point Lagrange interpolation for the APU voice resampler (see
 * apu_resample.h). The MCPX voice processor interpolates with an 8-point
 * Lagrange polynomial; linear interpolation loses several dB in the top
 * octave and aliases more. Weights come from a 1024-phase table built once
 * (nearest phase), so a sample costs eight multiply-adds per channel. */
#include "apu_resample.h"

#define PHASES 1024
static float g_w[PHASES][8];
static int g_ready;

void apu_lagrange8_weights(double t, float w[8])
{
    const double x = 3.0 + t;
    int j, m;
    for (j = 0; j < 8; j++) {
        double p = 1.0;
        for (m = 0; m < 8; m++)
            if (m != j) p *= (x - m) / (double)(j - m);
        w[j] = (float)p;
    }
}

void apu_lagrange8_init(void)
{
    int k;
    if (g_ready) return;
    for (k = 0; k < PHASES; k++) apu_lagrange8_weights((double)k / PHASES, g_w[k]);
    g_ready = 1;
}

float apu_lagrange8(const float h[8], double t)
{
    int k = (int)(t * PHASES + 0.5);
    const float *w;
    if (k < 0) k = 0;
    if (k >= PHASES) return h[4];                 /* t rounds up to the next node */
    w = g_w[k];
    return h[0] * w[0] + h[1] * w[1] + h[2] * w[2] + h[3] * w[3] +
           h[4] * w[4] + h[5] * w[5] + h[6] * w[6] + h[7] * w[7];
}
