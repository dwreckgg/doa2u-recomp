/* 8-point Lagrange interpolation for the APU voice resampler (pure). */
#ifndef DOA2U_APU_RESAMPLE_H
#define DOA2U_APU_RESAMPLE_H

/* Builds the phase table once (safe to call again). */
void  apu_lagrange8_init(void);
/* Exact weights for nodes 0..7 at position 3 + t, t in [0, 1). */
void  apu_lagrange8_weights(double t, float w[8]);
/* The value between h[3] and h[4] at fraction t, from the phase table. */
float apu_lagrange8(const float h[8], double t);

#endif
