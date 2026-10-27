/* Frame pacing decisions (see frame_pacer.h).
 *
 * DOA2U runs one game frame per Xbox vblank, so it wants 60 frames a second
 * shown evenly. On a display whose refresh is a multiple of 60 (60, 59.94,
 * 120, 240 Hz) the display itself is the best clock: wait until the swap
 * chain can take a frame, present on every 1st/2nd/4th refresh, never tear.
 * At other rates (75, 144, 165 Hz) 60 frames cannot land on refreshes evenly,
 * so a 60 Hz timer is the clock and the frame is shown at the next refresh;
 * with variable refresh (G-Sync/FreeSync) it is presented at once with
 * tearing allowed and the display refreshes when the frame arrives. */
#include "frame_pacer.h"

PacePlan pace_plan(int refresh_hz, int vrr, int tearing_ok)
{
    PacePlan p;
    const int k = (refresh_hz + 30) / 60;          /* nearest multiple of 60 */
    p.interval = 0;
    p.tearing = 0;
    p.vsync_clock = 0;
    p.cap_hz = 60.0;
    if (vrr) {
        p.tearing = tearing_ok != 0;
        return p;
    }
    /* 59.94 Hz is reported as 59, 119.88 as 119: within one hertz per multiple. */
    if (refresh_hz > 0 && (k == 1 || k == 2 || k == 4) &&
        refresh_hz - 60 * k <= k && 60 * k - refresh_hz <= k) {
        p.interval = k;
        p.vsync_clock = 1;
        p.cap_hz = 61.0;                           /* only stops a runaway (occluded, minimised) */
    }
    return p;
}

/* One step of a fixed-rate clock in performance-counter ticks: returns how long
 * to wait before this frame and advances *next by one period from the boundary
 * (not from now, so the rate stays exact). A frame more than a period behind
 * resyncs to now instead of racing to catch up. */
long long pace_timer_step(long long now, long long *next, long long freq, double hz)
{
    const long long period = (long long)((double)freq / hz + 0.5);
    long long wait = 0;
    if (*next == 0 || now - *next > period) *next = now;
    if (now < *next) wait = *next - now;
    *next += period;
    return wait;
}
