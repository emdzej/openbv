/* openbv: the bits of musl's internal libm.h that sinf, cosf and their kernels use, so the reference
   driver computes trigonometry exactly as the wasm client (wasi-libc's musl) does. The functions are
   renamed musl_sinf and musl_cosf to stay out of the host libm's way. */
#ifndef OPENBV_MUSL_LIBM_H
#define OPENBV_MUSL_LIBM_H
#include <stdint.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

#define cosf musl_cosf
#define sinf musl_sinf
#ifdef __cplusplus
extern "C" {
#endif
float musl_cosf(float);
float musl_sinf(float);
#ifdef __cplusplus
}
#endif

typedef double double_t;
#define EPS DBL_EPSILON
#define predict_false(x) (x)
#define FORCE_EVAL(x) do { volatile float __v = (x); (void)__v; } while (0)
#define GET_FLOAT_WORD(w, d) do { union { float f; uint32_t i; } __u; __u.f = (d); (w) = __u.i; } while (0)

float __cosdf(double);
float __sindf(double);
int __rem_pio2f(float, double *);
/* only for |x| >= 2^28*pi/2, which the game never passes */
static inline int __rem_pio2_large(double *x, double *y, int e0, int nx, int prec) { (void)x; (void)y; (void)e0; (void)nx; (void)prec; abort(); }
#endif
