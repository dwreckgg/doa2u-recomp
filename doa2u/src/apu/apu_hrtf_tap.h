/* HRTF filter tap decoding (pure). */
#ifndef DOA2U_APU_HRTF_TAP_H
#define DOA2U_APU_HRTF_TAP_H

/* One HRIR tap as the MCPX voice processor reads it: sign bit 7, a 3-bit
 * exponent (bits 6:4) and a 4-bit mantissa (bits 3:0); not normalised. */
float apu_hrtf_tap(unsigned char byte);

#endif
