/* Host test for the HRTF tap decoding (src/apu/apu_hrtf_tap.c): retail single-tap
 * measurements (xemu PR 3105): sign bit 7, exponent bits 6:4, mantissa bits 3:0,
 * (16 + m) * 2^(e - 1) / 2048, or m / 2048 when e = 0; not normalised. */
#include <math.h>
#include <stdio.h>
#include "apu/apu_hrtf_tap.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define EQ(a, b) (fabsf((a) - (b)) < 1e-7f)

int main(void)
{
    CHECK(EQ(apu_hrtf_tap(0x00), 0.0f));
    CHECK(EQ(apu_hrtf_tap(0x01), 1.0f / 2048.0f));
    CHECK(EQ(apu_hrtf_tap(0x0F), 15.0f / 2048.0f));
    CHECK(EQ(apu_hrtf_tap(0x10), 16.0f / 2048.0f));      /* e = 1: (16 + 0) * 2^0 */
    CHECK(EQ(apu_hrtf_tap(0x20), 1.0f / 64.0f));
    CHECK(EQ(apu_hrtf_tap(0x40), 1.0f / 16.0f));
    CHECK(EQ(apu_hrtf_tap(0x7F), 31.0f / 32.0f));
    CHECK(EQ(apu_hrtf_tap(0x81), -1.0f / 2048.0f));
    CHECK(EQ(apu_hrtf_tap(0xC0), -1.0f / 16.0f));
    CHECK(EQ(apu_hrtf_tap(0x80), 0.0f));
    /* Monotonic in magnitude across the whole positive range. */
    {
        int v;
        for (v = 1; v < 0x80; v++) CHECK(apu_hrtf_tap((unsigned char)v) > apu_hrtf_tap((unsigned char)(v - 1)));
    }
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
