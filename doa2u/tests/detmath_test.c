/* third_party/detmath through src/game/detmath_redirect.h, compiled the way the game code is
 * (the redirect force-included): the game's sin/cos/tan/atan2/log/pow reach musl, the results
 * are correct, and their bits are the pinned ones below on every PC. Run this on both players'
 * PCs: a PC that prints a failure here would desync online. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

/* Calls written exactly as the game code writes them; the forced-included redirect turns
 * them into detmath_* calls. The CRT's own are reached through function pointers, which
 * the function-like macros do not touch. */
static double game_sin(double x)            { return sin(x); }
static double game_cos(double x)            { return cos(x); }
static double game_tan(double x)            { return tan(x); }
static double game_atan2(double y, double x) { return atan2(y, x); }
static double game_log(double x)            { return log(x); }
static double game_pow(double x, double y)  { return pow(x, y); }
static double (*crt_sin)(double) = sin;
static double (*crt_cos)(double) = cos;
static double (*crt_tan)(double) = tan;
static double (*crt_atan2)(double, double) = atan2;
static double (*crt_log)(double) = log;
static double (*crt_pow)(double, double) = pow;

static int near(double a, double b)   /* within two units in the last place */
{
    int64_t d = (int64_t)bits(a) - (int64_t)bits(b);
    return d >= -2 && d <= 2;
}

/* Pinned results (musl 1.2.5): the same on any PC, CPU and Windows release. */
static const struct { int fn; double x, y; uint64_t want; } k_pin[] = {
#include "detmath_pins.inc"
};

int main(void)
{
    static const double xs[] = { 0.5, -0.5, 1.0, 2.5, 3.14159265358979, 1.0e-8, 100.0, -731.25, 1.0e6, 1.0e22 };
    static const double ps[] = { 0.25, 1.5, 2.0, 7.0, 1e-3, 123.456 };
    size_t i, j;

    for (i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        double x = xs[i];
        /* the redirect reaches musl */
        CHECK(bits(game_sin(x)) == bits(detmath_sin(x)));
        CHECK(bits(game_cos(x)) == bits(detmath_cos(x)));
        CHECK(bits(game_tan(x)) == bits(detmath_tan(x)));
        CHECK(bits(game_atan2(x, 0.75)) == bits(detmath_atan2(x, 0.75)));
        /* and musl is right */
        CHECK(near(detmath_sin(x), crt_sin(x)));
        CHECK(near(detmath_cos(x), crt_cos(x)));
        CHECK(near(detmath_tan(x), crt_tan(x)));
        CHECK(near(detmath_atan2(x, 0.75), crt_atan2(x, 0.75)));
    }
    for (i = 0; i < sizeof ps / sizeof ps[0]; i++) {
        CHECK(bits(game_log(ps[i])) == bits(detmath_log(ps[i])));
        CHECK(near(detmath_log(ps[i]), crt_log(ps[i])));
        for (j = 0; j < sizeof ps / sizeof ps[0]; j++) {
            CHECK(bits(game_pow(ps[i], ps[j])) == bits(detmath_pow(ps[i], ps[j])));
            CHECK(near(detmath_pow(ps[i], ps[j]), crt_pow(ps[i], ps[j])));
        }
    }

    for (i = 0; i < sizeof k_pin / sizeof k_pin[0]; i++) {
        double r = 0;
        switch (k_pin[i].fn) {
        case 0: r = detmath_sin(k_pin[i].x); break;
        case 1: r = detmath_cos(k_pin[i].x); break;
        case 2: r = detmath_tan(k_pin[i].x); break;
        case 3: r = detmath_atan2(k_pin[i].x, k_pin[i].y); break;
        case 4: r = detmath_log(k_pin[i].x); break;
        case 5: r = detmath_pow(k_pin[i].x, k_pin[i].y); break;
        }
        if (bits(r) != k_pin[i].want) {
            fprintf(stderr, "pin %u: fn %d (%.17g, %.17g) gave %016llX, want %016llX\n", (unsigned)i, k_pin[i].fn,
                    k_pin[i].x, k_pin[i].y, (unsigned long long)bits(r), (unsigned long long)k_pin[i].want);
            ++failures;
        }
    }
    printf("detmath_test: %d failure(s), %u pinned results\n", failures, (unsigned)(sizeof k_pin / sizeof k_pin[0]));
    return failures;
}
