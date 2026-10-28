/* Math with the same bits on every PC (README.md): the functions the game's state depends on, from musl, built into
 * the executable. The C runtime's own differ between Windows releases and processors, which online play between two
 * PCs cannot have. */
#ifndef DETMATH_H
#define DETMATH_H

#ifdef __cplusplus
extern "C" {
#endif

double detmath_sin(double x);
double detmath_cos(double x);
double detmath_tan(double x);
double detmath_asin(double x);
double detmath_acos(double x);
double detmath_atan(double x);
double detmath_atan2(double y, double x);
double detmath_exp2(double x);
double detmath_log(double x);
double detmath_log2(double x);
double detmath_log10(double x);
double detmath_pow(double x, double y);

#ifdef __cplusplus
}
#endif

#endif
