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
#include <math.h>

typedef struct {
    double g;
    double a;
    double b;
    double d;
    double e;
} TF_Nonlinear;

float skcms_TransferFunction_evalUnclamped(const skcms_TransferFunction* fn, float x) {
    float sign = x < 0 ? -1.0f : 1.0f;
    x *= sign;

    return sign * (x < fn->d ? fn->c * x + fn->f
                             : powf(fn->a * x + fn->b, fn->g) + fn->e);
}

float skcms_TransferFunction_eval(const skcms_TransferFunction* fn, float x) {
    float t = skcms_TransferFunction_evalUnclamped(fn, x);
    return fminf(fmaxf(t, 0.0f), 1.0f);
}

static double TF_Nonlinear_evalUnclamped(const TF_Nonlinear* fn, float x) {
    float sign = x < 0 ? -1.0f : 1.0f;
    x *= sign;
    return (double)sign * pow(fn->a * (double)x + fn->b, fn->g) + fn->e;
}

static double TF_Nonlinear_eval(const TF_Nonlinear* fn, float x) {
    double t = TF_Nonlinear_evalUnclamped(fn, x);
    return fmin(fmax(t, 0.0), 1.0);
}

// Evaluate the gradient of the nonlinear component of fn
static void tf_eval_gradient_nonlinear(const TF_Nonlinear* fn,
                                       float x,
                                       double* d_fn_d_A_at_x,
                                       double* d_fn_d_B_at_x,
                                       double* d_fn_d_E_at_x,
                                       double* d_fn_d_G_at_x) {
    double base = fn->a * (double)x + fn->b;
    if (base > 0.0) {
        *d_fn_d_A_at_x = fn->g * (double)x * pow(base, fn->g - 1.0);
        *d_fn_d_B_at_x = fn->g * pow(base, fn->g - 1.0);
        *d_fn_d_E_at_x = 1.0;
        *d_fn_d_G_at_x = pow(base, fn->g) * log(base);
    } else {
        *d_fn_d_A_at_x = 0.0;
        *d_fn_d_B_at_x = 0.0;
        *d_fn_d_E_at_x = 0.0;
        *d_fn_d_G_at_x = 0.0;
    }
}

// Take one Gauss-Newton step updating A, B, E, and G, given D.
static bool tf_gauss_newton_step_nonlinear(skcms_TableFunc* t, const void* ctx, int start, int n,
                                           TF_Nonlinear* fn, float* error_Linfty_after) {
    // Let ne_lhs be the left hand side of the normal equations, and let ne_rhs
    // be the right hand side. Zero the diagonal [sic] of |ne_lhs| and all of |ne_rhs|.
    skcms_Matrix4x4d ne_lhs;
    skcms_Vector4d ne_rhs;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            ne_lhs.vals[row][col] = 0;
        }
        ne_rhs.vals[row] = 0;
    }

    // Add the contributions from each sample to the normal equations.
    for (int i = start; i < n; ++i) {
        float xi = i / (n - 1.0f);
        // Let J be the gradient of fn with respect to parameters A, B, E, and G,
        // evaulated at this point.
        skcms_Vector4d J;
        tf_eval_gradient_nonlinear(fn, xi, &J.vals[0], &J.vals[1], &J.vals[2], &J.vals[3]);
        // Let r be the residual at this point;
        double r = (double)t(i, n, ctx) - TF_Nonlinear_eval(fn, xi);

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
    }

    // Note that if G = 1, then the normal equations will be singular
    // (because when G = 1, B and E are equivalent parameters).
    // To avoid problems, fix E (row/column 3) in these circumstances.
    double kEpsilonForG = 1.0 / 1024.0;
    if (fabs(fn->g - 1.0) < kEpsilonForG) {
        for (int row = 0; row < 4; ++row) {
            double value = (row == 2) ? 1.0 : 0.0;
            ne_lhs.vals[row][2] = value;
            ne_lhs.vals[2][row] = value;
        }
        ne_rhs.vals[2] = 0.0;
    }

    // Solve the normal equations.
    skcms_Matrix4x4d ne_lhs_inv;
    if (!skcms_Matrix4x4d_invert(&ne_lhs, &ne_lhs_inv)) {
        return false;
    }
    skcms_Vector4d step = skcms_Matrix4x4d_Vector4d_mul(&ne_lhs_inv, &ne_rhs);

    // Update the transfer function.
    fn->a += step.vals[0];
    fn->b += step.vals[1];
    fn->e += step.vals[2];
    fn->g += step.vals[3];

    // A should always be positive.
    fn->a = fmax(fn->a, 0.0);

    // Ensure that fn be defined at D.
    if (fn->a * fn->d + fn->b < 0.0) {
        fn->b = -fn->a * fn->d;
    }

    // Compute the Linfinity error.
    *error_Linfty_after = 0;
    for (int i = start; i < n; ++i) {
        float xi = i / (n - 1.0f);
        float error = fabsf(t(i, n, ctx) - (float)TF_Nonlinear_eval(fn, xi));
        *error_Linfty_after = fmaxf(error, *error_Linfty_after);
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
    float step_error[kNumSteps] = { 0 };
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
        float kEarlyOutTinyErrorThreshold = (1.0f / 16.0f) / 256.0f;
        if (step_error[step] < kEarlyOutTinyErrorThreshold) {
            break;
        }

        // Stop if our error is not changing, or changing in the wrong direction.
        if (step > 1) {
            // If our error is is huge for two iterations, we're probably not in the
            // region of convergence.
            if (step_error[step] > 1.0f && step_error[step - 1] > 1.0f) {
                return false;
            }

            // If our error didn't change by ~1%, assume we've converged as much as we
            // are going to.
            const float kEarlyOutByPercentChangeThreshold = 32.0f / 256.0f;
            const float kMinimumPercentChange = 1.0f / 128.0f;
            float percent_change =
                fabsf(step_error[step] - step_error[step - 1]) / step_error[step];
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
    float kDidNotConvergeThreshold = 64.0f / 256.0f;
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
    const float y0 = t(0, n, ctx);

    int lin_points = 1;
    float slope_min = -1E6F;
    float slope_max = 1E6F;
    for (int i = 1; i < n; ++i, ++lin_points) {
        float xi = i * x_scale;
        float yi = t(i, n, ctx);
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
    fn->d = nextafterf((lin_points - 1) * x_scale, 2.0f);
    // If the linear portion only covers a very small fraction of points, omit it entirely.
    if (fn->d < 1.0f / 64.0f) {
        fn->c = 0.0f;
        fn->d = 0.0f;
        fn->f = 0.0f;
        lin_points = 0;
    } else {
        fn->f = y0;
        fn->c = (slope_min + slope_max) * 0.5f;
    }

    // If the entire data set was linear, move the coefficients to the nonlinear portion with
    // G == 1. This lets use a canonical representation with D == 0.
    if (lin_points == n) {
        fn->g = 1;
        fn->b = fn->f;
        fn->a = fn->c;
        fn->c = fn->d = fn->e = fn->f = 0;
    } else {
        // Do a nonlinear regression on the nonlinear segment. Use a number of guesses for the
        // initial value of G, because not all values will converge.
        // TODO: Estimate starting g by examining shape of remaining points (endpoints + median)?
        bool nonlinear_fit_converged = false;
        double initial_gammas[] = { 2.2, 2.4, 1.0, 3.0, 0.5 };
        for (int i = 0; i < ARRAY_COUNT(initial_gammas); ++i) {
            TF_Nonlinear tf = { initial_gammas[i], 1, 0, (double)fn->d, 0 };
            if (tf_solve_nonlinear(t, ctx, lin_points, n, &tf)) {
                nonlinear_fit_converged = true;
                fn->g = (float)tf.g;
                fn->a = (float)tf.a;
                fn->b = (float)tf.b;
                fn->e = (float)tf.e;
                break;
            }
        }
        if (!nonlinear_fit_converged) {
            return false;
        }
    }

    if (max_error) {
        *max_error = 0;
        for (int i = 0; i < n; ++i) {
            float xi = i * x_scale;
            float fn_of_xi = skcms_TransferFunction_eval(fn, xi);
            float error_at_xi = fabsf(t(i, n, ctx) - fn_of_xi);
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
        if (fabsf(l_at_d - n_at_d) > 0.00015f) {
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
