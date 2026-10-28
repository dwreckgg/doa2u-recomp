/* The deterministic math (README.md): stands in for musl's internal <features.h> and is included first in every file
 * here (XbeRecomp.cmake). It brings in the C runtime's <math.h> before anything is renamed, then gives every function
 * and table of these files a detmath_ name, so they sit beside the C runtime's own and the executable chooses which
 * one the game code gets. */
#ifndef DETMATH_FEATURES_H
#define DETMATH_FEATURES_H

#include <float.h>
#include <math.h>
#include <stdint.h>

/* the same instructions on every PC: no fused multiply-add, written out or contracted */
#undef __FP_FAST_FMA
#ifdef _MSC_VER
/* DOA2U: built by MSVC (cl) rather than Clang. cl has no __builtin_expect and spells the
 * contraction switch differently; it accepts the C99 compound literals libm.h uses in /std:c11. */
#define __builtin_expect(x, y) (x)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

#define hidden

#define sin detmath_sin
#define cos detmath_cos
#define tan detmath_tan
#define asin detmath_asin
#define acos detmath_acos
#define atan detmath_atan
#define atan2 detmath_atan2
#define exp2 detmath_exp2
#define log detmath_log
#define log2 detmath_log2
#define log10 detmath_log10
#define pow detmath_pow
#define __sin detmath_k_sin
#define __cos detmath_k_cos
#define __tan detmath_k_tan
#define __rem_pio2 detmath_rem_pio2
#define __rem_pio2_large detmath_rem_pio2_large
#define __math_divzero detmath_math_divzero
#define __math_invalid detmath_math_invalid
#define __math_oflow detmath_math_oflow
#define __math_uflow detmath_math_uflow
#define __math_xflow detmath_math_xflow
#define __exp_data detmath_exp_data
#define __log_data detmath_log_data
#define __log2_data detmath_log2_data
#define __pow_log_data detmath_pow_log_data

/* <math.h> declared the C runtime's: these are the ones the files here call each other by */
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double exp2(double);
double log(double);
double log2(double);
double log10(double);
double pow(double, double);

#endif
