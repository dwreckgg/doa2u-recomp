/* Stereo output mix of the APU's mix bins (pure; used by apu_dsp.c). */
#ifndef DOA2U_APU_DOWNMIX_H
#define DOA2U_APU_DOWNMIX_H

#define APU_DOWNMIX_BINS 10   /* bins 0..9 take part: FL FR C LFE SL SR, XTLK FL FR BL BR */

/* bins: one sample per bin; gain: per-bin headroom undo (1 << submix headroom);
 * xtlk_gain: headroom undo for the crosstalk bins 6..9 (1 << HRTF headroom). */
void apu_stereo_downmix(const float bins[APU_DOWNMIX_BINS], const float gain[APU_DOWNMIX_BINS],
                        float xtlk_gain, float *left, float *right);

#endif
