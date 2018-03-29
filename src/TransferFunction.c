/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "LinearAlgebra.h"
#include "Macros.h"
#include "TransferFunction.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

// Enable to do thorough logging of the nonlinear regression to stderr
#if 0
    #include <stdio.h>
    #define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
    #define LOG(...) do {} while(false)
#endif

#define LOG_TF(tf)                                              \
    LOG("[%.25g %.25g %.25g %.25g]\n",                          \
        tf->g, tf->a, tf->b, tf->e)

#define LOG_VEC(v)                                              \
    LOG("[%.25g %.25g %.25g %.25g]\n",                          \
        v.vals[0], v.vals[1], v.vals[2], v.vals[3])

#define LOG_MTX(m)                                              \
    LOG("| %.25g %.25g %.25g %.25g |\n"                         \
        "| %.25g %.25g %.25g %.25g |\n"                         \
        "| %.25g %.25g %.25g %.25g |\n"                         \
        "| %.25g %.25g %.25g %.25g |\n",                        \
        m.vals[0][0], m.vals[0][1], m.vals[0][2], m.vals[0][3], \
        m.vals[1][0], m.vals[1][1], m.vals[1][2], m.vals[1][3], \
        m.vals[2][0], m.vals[2][1], m.vals[2][2], m.vals[2][3], \
        m.vals[3][0], m.vals[3][1], m.vals[3][2], m.vals[3][3])

typedef struct {
    double g;
    double a;
    double b;
    double d;
    double e;
} TF_Nonlinear;

#if defined(__clang__) || defined(__GNUC__)
    #define small_memcpy __builtin_memcpy
#else
    #define small_memcpy memcpy
#endif

static float approx_log2f(float x) {
    // The first approximation of log2(x) is its exponent 'e', minus 127.
    int32_t bits;
    small_memcpy(&bits, &x, sizeof(bits));

    float e = (float)bits * (1.0f / (1<<23));

    // If we use the mantissa too we can refine the error signficantly.
    int32_t m_bits = (bits & 0x007fffff) | 0x3f000000;
    float m;
    small_memcpy(&m, &m_bits, sizeof(m));

    return (e - 124.225514990f
              -   1.498030302f*m
              -   1.725879990f/(0.3520887068f + m));
}

static float approx_exp2f(float x) {
    float fract = x - floorf(x);

    float fbits = (1.0f * (1<<23)) * (x + 121.274057500f
                                        -   1.490129070f*fract
                                        +  27.728023300f/(4.84252568f - fract));
    if (fbits > INT_MAX) {
        return INFINITY;
    }
    int32_t bits = (int32_t)fbits;
    small_memcpy(&x, &bits, sizeof(x));
    return x;
}

static float approx_powf(float x, float y) {
    // Handling all the integral powers first increases our precision a little.
    float r = 1.0f;
    while (y >= 1.0f) {
        r *= x;
        y -= 1.0f;
    }

    return (x == 0) || (x == 1) ? x : r * approx_exp2f(approx_log2f(x) * y);
}

static double approx_log2d(double x) {
    return (double)approx_log2f((float)x);
}

static double approx_exp2d(double x) {
    return (double)approx_exp2f((float)x);
}

static double approx_powd(double x, double y) {
    (void)approx_powf;
    // Handling all the integral powers first increases our precision a little.
    double r = 1.0;
    while (y >= 1.0) {
        r *= x;
        y -= 1.0;
    }

    return (x == 0) || (x == 1) ? x : r * approx_exp2d(approx_log2d(x) * y);
}

float skcms_TransferFunction_eval(const skcms_TransferFunction* fn, float x) {
    float sign = x < 0 ? -1.0f : 1.0f;
    x *= sign;

    return sign * (x < fn->d ? fn->c * x + fn->f
                             : approx_powf(fn->a * x + fn->b, fn->g) + fn->e);
}

static double TF_Nonlinear_eval(const TF_Nonlinear* fn, double x) {
    // We strive to never allow negative ax+b, but values can drift slightly. Guard against NaN.
    double base = fmax(fn->a * x + fn->b, 0.0);
    return approx_powd(base, fn->g) + fn->e;
}

// Evaluate the gradient of the nonlinear component of fn
static void tf_eval_gradient_nonlinear(const TF_Nonlinear* fn,
                                       double x,
                                       double* d_fn_d_A_at_x,
                                       double* d_fn_d_B_at_x,
                                       double* d_fn_d_E_at_x,
                                       double* d_fn_d_G_at_x) {
    double base = fn->a * x + fn->b;
    if (base > 0.0) {
        *d_fn_d_A_at_x = fn->g * x * approx_powd(base, fn->g - 1.0);
        *d_fn_d_B_at_x = fn->g * approx_powd(base, fn->g - 1.0);
        *d_fn_d_E_at_x = 1.0;
        // Scale by 1/log_2(e)
        *d_fn_d_G_at_x = approx_powd(base, fn->g) * approx_log2d(base) * 0.69314718;
    } else {
        *d_fn_d_A_at_x = 0.0;
        *d_fn_d_B_at_x = 0.0;
        *d_fn_d_E_at_x = 0.0;
        *d_fn_d_G_at_x = 0.0;
    }
}

// Take one Gauss-Newton step updating A, B, E, and G, given D.
static bool tf_gauss_newton_step_nonlinear(skcms_TableFunc* t, const void* ctx, int start, int n,
                                           TF_Nonlinear* fn, double* error_Linfty_after) {
    LOG("tf_gauss_newton_step_nonlinear (%d, %d)\n", start, n);
    LOG("fn: "); LOG_TF(fn);

    // Let ne_lhs be the left hand side of the normal equations, and let ne_rhs
    // be the right hand side. Zero the diagonal [sic] of |ne_lhs| and all of |ne_rhs|.
    skcms_Matrix4x4 ne_lhs;
    skcms_Vector4 ne_rhs;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            ne_lhs.vals[row][col] = 0;
        }
        ne_rhs.vals[row] = 0;
    }

    // Add the contributions from each sample to the normal equations.
    for (int i = start; i < n; ++i) {
        double xi = i / (n - 1.0);
        LOG("%d (%.25g)\n", i, xi);

        // Let J be the gradient of fn with respect to parameters A, B, E, and G,
        // evaulated at this point.
        skcms_Vector4 J;
        tf_eval_gradient_nonlinear(fn, xi, &J.vals[0], &J.vals[1], &J.vals[2], &J.vals[3]);
        LOG("J: "); LOG_VEC(J);

        // Let r be the residual at this point;
        double r = (double)t(i, ctx) - TF_Nonlinear_eval(fn, xi);
        LOG("r: %.25g\n", r);

        // Update the normal equations left hand side with the outer product of J
        // with itself.
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                ne_lhs.vals[row][col] += J.vals[row] * J.vals[col];
            }

            // Update the normal equations right hand side the product of J with the
            // residual
            ne_rhs.vals[row] += J.vals[row] * r;
        }
        LOG("LHS/RHS:\n"); LOG_MTX(ne_lhs); LOG_VEC(ne_rhs);
    }

    // Note that if G = 1, then the normal equations will be singular
    // (because when G = 1, B and E are equivalent parameters).
    // To avoid problems, fix E (row/column 3) in these circumstances.
    double kEpsilonForG = 1.0 / 1024.0;
    if (fabs(fn->g - 1.0) < kEpsilonForG) {
        LOG("G ~= 1, pinning E\n");
        for (int row = 0; row < 4; ++row) {
            double value = (row == 2) ? 1.0 : 0.0;
            ne_lhs.vals[row][2] = value;
            ne_lhs.vals[2][row] = value;
        }
        ne_rhs.vals[2] = 0.0;
    }

    // Solve the normal equations.
    skcms_Matrix4x4 ne_lhs_inv;
    if (!skcms_Matrix4x4_invert(&ne_lhs, &ne_lhs_inv)) {
        return false;
    }
    LOG("LHS Inverse:\n"); LOG_MTX(ne_lhs_inv);

    skcms_Vector4 step = skcms_Matrix4x4_Vector4_mul(&ne_lhs_inv, &ne_rhs);
    LOG("step: "); LOG_VEC(step);

    // Update the transfer function.
    fn->a += step.vals[0];
    fn->b += step.vals[1];
    fn->e += step.vals[2];
    fn->g += step.vals[3];

    // A should always be positive.
    fn->a = fmax(fn->a, 0.0);

    // Ensure that fn be defined at D.
    if (fn->a * fn->d + fn->b < 0.0) {
        LOG("AD+B = %.25g, ", fn->a * fn->d + fn->b);
        fn->b = -fn->a * fn->d;
        LOG("B -> %.25g\n", fn->b);
    }

    // Compute the Linfinity error.
    *error_Linfty_after = 0;
    for (int i = start; i < n; ++i) {
        double xi = i / (n - 1.0);
        double error = fabs((double)t(i, ctx) - TF_Nonlinear_eval(fn, xi));
        *error_Linfty_after = fmax(error, *error_Linfty_after);
    }

    return true;
}

// Solve for A, B, E, and G, given D. The initial value of |fn| is the
// point from which iteration starts.
static bool tf_solve_nonlinear(skcms_TableFunc* t, const void* ctx, int start, int n,
                               TF_Nonlinear* fn) {
    // Take a maximum of 16 Gauss-Newton steps.
    enum { kNumSteps = 16 };

    // The L-infinity error after each step.
    double step_error[kNumSteps] = { 0 };
    int step = 0;
    for (;; ++step) {
        // If the normal equations are singular, we can't continue.
        if (!tf_gauss_newton_step_nonlinear(t, ctx, start, n, fn, &step_error[step])) {
            return false;
        }

        // If the error is inf or nan, we are clearly not converging.
        if (isnan(step_error[step]) || isinf(step_error[step])) {
            return false;
        }

        // Stop if our error is tiny.
        double kEarlyOutTinyErrorThreshold = (1.0 / 16.0) / 256.0;
        if (step_error[step] < kEarlyOutTinyErrorThreshold) {
            break;
        }

        // Stop if our error is not changing, or changing in the wrong direction.
        if (step > 1) {
            // If our error is is huge for two iterations, we're probably not in the
            // region of convergence.
            if (step_error[step] > 1.0 && step_error[step - 1] > 1.0) {
                return false;
            }

            // If our error didn't change by ~1%, assume we've converged as much as we
            // are going to.
            const double kEarlyOutByPercentChangeThreshold = 32.0 / 256.0;
            const double kMinimumPercentChange = 1.0 / 128.0;
            double percent_change =
                fabs(step_error[step] - step_error[step - 1]) / step_error[step];
            if (percent_change < kMinimumPercentChange &&
                step_error[step] < kEarlyOutByPercentChangeThreshold) {
                break;
            }
        }
        if (step == kNumSteps - 1) {
            break;
        }
    }

    // Declare failure if our error is obviously too high.
    double kDidNotConvergeThreshold = 64.0 / 256.0;
    if (step_error[step] > kDidNotConvergeThreshold) {
        return false;
    }

    // We've converged to a reasonable solution. If some of the parameters are
    // extremely close to 0 or 1, set them to 0 or 1.
    const double kRoundEpsilon = 1.0 / 1024.0;
    if (fabs(fn->a - 1.0) < kRoundEpsilon) {
        fn->a = 1.0;
    }
    if (fabs(fn->b) < kRoundEpsilon) {
        fn->b = 0;
    }
    if (fabs(fn->e) < kRoundEpsilon) {
        fn->e = 0;
    }
    if (fabs(fn->g - 1.0) < kRoundEpsilon) {
        fn->g = 1.0;
    }

    return true;
}

bool skcms_TransferFunction_approximate(skcms_TableFunc* t, const void* ctx, int n,
                                        skcms_TransferFunction* fn, float* max_error) {
    if (n < 2) {
        return false;
    }

    // Idea: We fit the first N points to the linear portion of the TF. We always construct the
    // line to pass through the first point. We walk along the points, and find the minimum and
    // maximum slope of the line before the error would exceed kLinearTolerance. Once the range
    // [slope_min, slope_max] would be empty, we can't add any more points, so we're done.

    // Assume that the values started out as .16 fixed point, and we want them to be almost exactly
    // linear in that representation.
    const float kLinearTolerance = 0.5f / 65535.0f;
    const float x_scale = 1.0f / (n - 1);
    const float y0 = t(0, ctx);

    int lin_points = 1;
    float slope_min = -1E6F;
    float slope_max = 1E6F;
    for (int i = 1; i < n; ++i, ++lin_points) {
        float xi = i * x_scale;
        float yi = t(i, ctx);
        float slope_max_i = (yi + kLinearTolerance - y0) / xi;
        float slope_min_i = (yi - kLinearTolerance - y0) / xi;
        if (slope_max_i < slope_min || slope_max < slope_min_i) {
            // Slope intervals no longer overlap.
            break;
        }
        slope_max = fminf(slope_max, slope_max_i);
        slope_min = fmaxf(slope_min, slope_min_i);
    }

    // Compute parameters for the linear portion.
    // Pick D so that all points we found above are included (this requires nudging).
    fn->d = nextafterf((lin_points - 1) * x_scale, INFINITY);
    fn->f = y0;
    fn->c = (slope_min + slope_max) * 0.5f;

    // If the entire data set was linear, move the coefficients to the nonlinear portion with
    // G == 1. This lets use a canonical representation with D == 0.
    if (lin_points == n) {
        fn->g = 1;
        fn->b = fn->f;
        fn->a = fn->c;
        fn->c = fn->d = fn->e = fn->f = 0;
    } else {
        // Do a nonlinear regression on the nonlinear segment. Include the 'D' point in the
        // nonlinear regression, so the two pieces are more likely to line up.
        int start = lin_points > 0 ? lin_points - 1 : 0;

        // We need G to be in right vicinity, or the regression may not converge. Solve exactly for
        // for midpoint of the nonlinear range, assuming B = E = 0 & A = 1.
        int mid = (start + n) / 2;
        double mid_x = mid / (n - 1.0);
        double mid_y = (double)t(mid, ctx);
        double mid_g = log(mid_y) / log(mid_x);
        TF_Nonlinear tf = { mid_g, 1, 0, (double)(start * x_scale), 0 };

        if (tf_solve_nonlinear(t, ctx, start, n, &tf)) {
            fn->g = (float)tf.g;
            fn->a = (float)tf.a;
            fn->b = (float)tf.b;
            fn->e = (float)tf.e;
        } else {
            return false;
        }
    }

    if (max_error) {
        *max_error = 0;
        for (int i = 0; i < n; ++i) {
            float xi = i * x_scale;
            float fn_of_xi = skcms_TransferFunction_eval(fn, xi);
            float error_at_xi = fabsf(t(i, ctx) - fn_of_xi);
            *max_error = fmaxf(*max_error, error_at_xi);
        }
    }

    return true;
}

// TODO: Adjust logic here? This still assumes that purely linear inputs will have D > 1, which
// we never generate. It also emits inverted linear using the same formulation. Standardize on
// G == 1 here, too?
bool skcms_TransferFunction_invert(const skcms_TransferFunction* src, skcms_TransferFunction* dst) {
    // Original equation is:       y = (ax + b)^g + e   for x >= d
    //                             y = cx + f           otherwise
    //
    // so 1st inverse is:          (y - e)^(1/g) = ax + b
    //                             x = ((y - e)^(1/g) - b) / a
    //
    // which can be re-written as: x = (1/a)(y - e)^(1/g) - b/a
    //                             x = ((1/a)^g)^(1/g) * (y - e)^(1/g) - b/a
    //                             x = ([(1/a)^g]y + [-((1/a)^g)e]) ^ [1/g] + [-b/a]
    //
    // and 2nd inverse is:         x = (y - f) / c
    // which can be re-written as: x = [1/c]y + [-f/c]
    //
    // and now both can be expressed in terms of the same parametric form as the
    // original - parameters are enclosed in square brackets.
    skcms_TransferFunction fn_inv = { 0, 0, 0, 0, 0, 0, 0 };

    // Reject obviously malformed inputs
    if (!isfinite(src->a + src->b + src->c + src->d + src->e + src->f + src->g)) {
        return false;
    }

    bool has_nonlinear = (src->d <= 1);
    bool has_linear = (src->d > 0);

    // Is the linear section decreasing or not invertible?
    if (has_linear && src->c <= 0) {
        return false;
    }

    // Is the nonlinear section decreasing or not invertible?
    if (has_nonlinear && (src->a <= 0 || src->g <= 0)) {
        return false;
    }

    // If both segments are present, they need to line up
    if (has_linear && has_nonlinear) {
        float l_at_d = src->c * src->d + src->f;
        float n_at_d = powf(src->a * src->d + src->b, src->g) + src->e;
        if (fabsf(l_at_d - n_at_d) > (1 / 512.0f)) {
            return false;
        }
    }

    // Invert linear segment
    if (has_linear) {
        fn_inv.c = 1.0f / src->c;
        fn_inv.f = -src->f / src->c;
    }

    // Invert nonlinear segment
    if (has_nonlinear) {
        fn_inv.g = 1.0f / src->g;
        fn_inv.a = powf(1.0f / src->a, src->g);
        fn_inv.b = -fn_inv.a * src->e;
        fn_inv.e = -src->b / src->a;
    }

    if (!has_linear) {
        fn_inv.d = 0;
    } else if (!has_nonlinear) {
        // Any value larger than 1 works
        fn_inv.d = 2.0f;
    } else {
        fn_inv.d = src->c * src->d + src->f;
    }

    *dst = fn_inv;
    return true;
}
