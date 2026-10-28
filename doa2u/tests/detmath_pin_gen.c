/* Prints tests/detmath_pins.inc: the bits third_party/detmath gives for a spread of inputs
 * (run once, the output committed; detmath_test checks every PC reproduces them).
 *   detmath_pin_gen > ../../tests/detmath_pins.inc */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "detmath.h"

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static void pin(int fn, double x, double y, double r)
{
    printf("    { %d, %a, %a, 0x%016llXull },\n", fn, x, y, (unsigned long long)bits(r));
}

int main(void)
{
    int i;
    for (i = -40; i <= 40; i++) {                       /* angles across several turns */
        double x = i * 0.39269908169872414 + 0.001 * i;
        pin(0, x, 0, detmath_sin(x));
        pin(1, x, 0, detmath_cos(x));
        if (i % 4) pin(2, x, 0, detmath_tan(x));
    }
    {
        static const double big[] = { 1e6, 123456.789, 1e15, 1e22, 1e300, -5e9, 3.0e-300, 7.5e-9 };
        for (i = 0; i < 8; i++) { pin(0, big[i], 0, detmath_sin(big[i])); pin(1, big[i], 0, detmath_cos(big[i])); }
    }
    for (i = 0; i < 24; i++) {                          /* atan2 round the circle */
        double a = i * 0.2617993877991494, y = (i - 12) * 0.37, x = 1.25 - i * 0.11;
        (void)a;
        pin(3, y, x, detmath_atan2(y, x));
    }
    for (i = 1; i <= 24; i++) {
        double x = i * i * 0.0625 + i * 1e-4;
        pin(4, x, 0, detmath_log(x));
        pin(5, x, 0.5 + i * 0.125, detmath_pow(x, 0.5 + i * 0.125));
        pin(5, 1.0 + i * 0.01, -3.0 + i * 0.25, detmath_pow(1.0 + i * 0.01, -3.0 + i * 0.25));
    }
    return 0;
}
