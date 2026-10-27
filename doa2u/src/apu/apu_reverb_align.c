/* See apu_reverb.h: apu_reverb_pad. */
#include "apu_reverb.h"

int apu_reverb_pad(int dry_queued, int wet_queued)
{
    int pad = dry_queued - 1 - wet_queued;
    int room = (APU_REVERB_BUFS - 2) - wet_queued;   /* the ring keeps one slot free, one is the new buffer */
    if (pad > room) pad = room;
    return pad > 0 ? pad : 0;
}
