/* Frame pacing decisions (pure; the D3D11 present path in d3d8_device.c applies them). */
#ifndef DOA2U_FRAME_PACER_H
#define DOA2U_FRAME_PACER_H

typedef struct {
    int    interval;      /* Present SyncInterval */
    int    tearing;       /* present with DXGI_PRESENT_ALLOW_TEARING */
    int    vsync_clock;   /* the display paces the game (wait on the latency object) */
    double cap_hz;        /* timer rate: the clock, or under vsync a runaway cap */
} PacePlan;

PacePlan  pace_plan(int refresh_hz, int vrr, int tearing_ok);
long long pace_timer_step(long long now, long long *next, long long freq, double hz);

#endif
