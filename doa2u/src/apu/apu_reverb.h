/* Room reverb (I3DL2) for the APU output: see apu_reverb.cpp. */
#ifndef DOA2U_APU_REVERB_H
#define DOA2U_APU_REVERB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct IXAudio2;
/* Creates the reverb submix and its source voice on an initialised XAudio2. */
int  xa2_reverb_init(struct IXAudio2 *xa2);
void xa2_reverb_shutdown(void);
/* The game's DSI3DL2LISTENER (12 dwords as in guest memory). */
void xa2_reverb_set_i3dl2(const void *params);
/* Non-zero once the voices exist and the game has set its room. */
int  xa2_reverb_ready(void);
/* One buffer of the I3DL2 send (bin 10), stereo, 48 kHz. */
void xa2_reverb_submit(const int16_t *stereo, int num_samples);

#ifdef __cplusplus
}
#endif

#endif
