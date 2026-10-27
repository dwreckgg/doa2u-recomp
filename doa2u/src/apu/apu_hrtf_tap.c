/* HRTF filter tap decoding (see apu_hrtf_tap.h). Format from retail
 * single-tap measurements: 01h = 1/2048, 20h = 1/64, 40h = 1/16, 7Fh = 31/32,
 * sign-magnitude. Read as two's complement / 128 instead, 40h came out eight
 * times too loud and the taps had to be normalised, which low-passed every
 * 3D voice. */
#include "apu_hrtf_tap.h"

float apu_hrtf_tap(unsigned char byte)
{
    const unsigned e = (byte >> 4) & 7u, m = byte & 15u;
    const float mag = e ? (float)((16u + m) << (e - 1u)) / 2048.0f : (float)m / 2048.0f;
    return (byte & 0x80u) ? -mag : mag;
}
