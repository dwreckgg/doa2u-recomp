/* Force-included first into the game code (the recompiled sources and recomp_manual.c, see
 * CMakeLists.txt): its transcendental math comes from third_party/detmath, which gives the
 * same bits on every PC, instead of the C runtime, whose results depend on the Windows
 * release and the processor (README there). Online play needs both games to compute the
 * same frame. sqrt, fmod and fabs are exact by definition and stay the C runtime's. */
#ifndef DOA2U_DETMATH_REDIRECT_H
#define DOA2U_DETMATH_REDIRECT_H

#include <math.h>
#include "detmath.h"

#define sin(x)      detmath_sin(x)
#define cos(x)      detmath_cos(x)
#define tan(x)      detmath_tan(x)
#define asin(x)     detmath_asin(x)
#define acos(x)     detmath_acos(x)
#define atan(x)     detmath_atan(x)
#define atan2(y, x) detmath_atan2(y, x)
#define exp2(x)     detmath_exp2(x)
#define log(x)      detmath_log(x)
#define log2(x)     detmath_log2(x)
#define log10(x)    detmath_log10(x)
#define pow(x, y)   detmath_pow(x, y)

#endif
