/* Stereo output mix of the APU's mix bins (see apu_downmix.h).
 *
 * The Xbox's encode processor mixes stereo as
 *   L = FL + 0.707 C + 0.707 SL,   R = FR + 0.707 C + 0.707 SR
 * and drops the LFE bin. On top of that come the crosstalk speaker bins
 * (DSMIXBIN_XTLK_FRONT/BACK LEFT/RIGHT) every 3D voice - sound effects and
 * speech - is sent to. Each bin is first scaled back up by the headroom its
 * voices were mixed with, so the game's own balance holds. */
#include "apu_downmix.h"

#define MINUS_3DB 0.70710678f

static float clip(float v) { return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v; }

void apu_stereo_downmix(const float bins[APU_DOWNMIX_BINS], const float gain[APU_DOWNMIX_BINS],
                        float xtlk_gain, float *left, float *right)
{
    const float centre = bins[2] * gain[2] * MINUS_3DB;
    const float l = bins[0] * gain[0] + centre + bins[4] * gain[4] * MINUS_3DB + (bins[6] + bins[8]) * xtlk_gain;
    const float r = bins[1] * gain[1] + centre + bins[5] * gain[5] * MINUS_3DB + (bins[7] + bins[9]) * xtlk_gain;
    *left = clip(l);
    *right = clip(r);
}
