/* Host test for the frame pacer's pure decisions (src/d3d/frame_pacer.c). */
#include <stdio.h>
#include "d3d/frame_pacer.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void plan_cases(void)
{
    PacePlan p;
    /* 60 Hz and 59.94 Hz (reported as 59): the display is the clock, vsync every refresh. */
    p = pace_plan(60, 0, 1); CHECK(p.vsync_clock && p.interval == 1 && !p.tearing);
    p = pace_plan(59, 0, 1); CHECK(p.vsync_clock && p.interval == 1 && !p.tearing);
    /* 120 / 240 Hz: every 2nd / 4th refresh. */
    p = pace_plan(120, 0, 1); CHECK(p.vsync_clock && p.interval == 2);
    p = pace_plan(119, 0, 0); CHECK(p.vsync_clock && p.interval == 2);
    p = pace_plan(240, 0, 1); CHECK(p.vsync_clock && p.interval == 4);
    /* 144 / 165 / 75 Hz are not multiples of 60: the 60 Hz timer is the clock. */
    p = pace_plan(144, 0, 1); CHECK(!p.vsync_clock && p.interval == 0 && !p.tearing);
    p = pace_plan(165, 0, 1); CHECK(!p.vsync_clock && p.interval == 0 && !p.tearing);
    p = pace_plan(75, 0, 1);  CHECK(!p.vsync_clock && p.interval == 0);
    /* Variable refresh: timer clock, presented at once with tearing allowed (the display follows). */
    p = pace_plan(144, 1, 1); CHECK(!p.vsync_clock && p.interval == 0 && p.tearing);
    p = pace_plan(60, 1, 1);  CHECK(!p.vsync_clock && p.interval == 0 && p.tearing);
    /* Variable refresh asked for but the system cannot present with tearing: no tearing flag. */
    p = pace_plan(144, 1, 0); CHECK(!p.vsync_clock && p.interval == 0 && !p.tearing);
    /* Unknown refresh: timer. */
    p = pace_plan(0, 0, 1);   CHECK(!p.vsync_clock && p.interval == 0);
    /* The timer is a 60 Hz clock, and under vsync only a runaway cap that never beats the display. */
    p = pace_plan(144, 0, 1); CHECK(p.cap_hz == 60.0);
    p = pace_plan(59, 0, 1);  CHECK(p.cap_hz > 60.0 && p.cap_hz < 62.0);
}

static void timer_cases(void)
{
    const long long f = 600000;           /* ticks per second: one 60 Hz period = 10000 */
    long long next = 0, wait;
    /* First call starts the clock at now: no wait. */
    wait = pace_timer_step(1000, &next, f, 60.0);
    CHECK(wait == 0 && next == 1000 + 10000);
    /* Early by 4000 ticks: wait them, next advances one period from the boundary. */
    wait = pace_timer_step(7000, &next, f, 60.0);
    CHECK(wait == 4000 && next == 21000);
    /* Slightly late (within a period): no wait, the boundary grid is kept. */
    wait = pace_timer_step(21500, &next, f, 60.0);
    CHECK(wait == 0 && next == 31000);
    /* More than a period behind (a hitch): resync to now rather than racing to catch up. */
    wait = pace_timer_step(60000, &next, f, 60.0);
    CHECK(wait == 0 && next == 70000);
}

int main(void)
{
    plan_cases();
    timer_cases();
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
