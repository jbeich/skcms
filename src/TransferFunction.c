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

float skcms_TransferFunction_evalUnclamped(const skcms_TransferFunction* fn, float x) {
    // TODO: absf() and copysignf() to allow negative x?
    assert (x >= 0);

    if (x < fn->d) {
        return fn->c * x + fn->f;
    }
    return powf(fn->a * x + fn->b, fn->g) + fn->e;
}

float skcms_TransferFunction_eval(const skcms_TransferFunction* fn, float x) {
    float t = skcms_TransferFunction_evalUnclamped(fn, x);
    return fminf(fmaxf(t, 0.0f), 1.0f);
}

// Evaluate the gradient of the nonlinear component of fn
static void tf_eval_gradient_nonlinear(const skcms_TransferFunction* fn,
                                       float x,
                                       float* d_fn_d_A_at_x,
                                       float* d_fn_d_B_at_x,
                                       float* d_fn_d_E_at_x,
                                       float* d_fn_d_G_at_x) {
    float base = fn->a * x + fn->b;
    if (base > 0.0f) {
        *d_fn_d_A_at_x = fn->g * x * powf(base, fn->g - 1.0f);
        *d_fn_d_B_at_x = fn->g * powf(base, fn->g - 1.0f);
        *d_fn_d_E_at_x = 1.0f;
        *d_fn_d_G_at_x = powf(base, fn->g) * logf(base);
    } else {
        *d_fn_d_A_at_x = 0.0f;
        *d_fn_d_B_at_x = 0.0f;
        *d_fn_d_E_at_x = 0.0f;
        *d_fn_d_G_at_x = 0.0f;
    }
}

// Take one Gauss-Newton step updating A, B, E, and G, given D.
static bool tf_gauss_newton_step_nonlinear(skcms_TransferFunction* fn,
                                           float* error_Linfty_after,
                                           const float* x,
                                           const float* t,
                                           int n) {
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
    for (int i = 0; i < n; ++i) {
        // Ignore points in the linear segment.
        if (x[i] < fn->d) {
            continue;
        }

        // Let J be the gradient of fn with respect to parameters A, B, E, and G,
        // evaulated at this point.
        skcms_Vector4 J;
        tf_eval_gradient_nonlinear(fn, x[i], &J.vals[0], &J.vals[1], &J.vals[2], &J.vals[3]);
        // Let r be the residual at this point;
        float r = t[i] - skcms_TransferFunction_eval(fn, x[i]);

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
    float kEpsilonForG = 1.0f / 1024.0f;
    if (fabsf(fn->g - 1.0f) < kEpsilonForG) {
        for (int row = 0; row < 4; ++row) {
            float value = (row == 2) ? 1.0f : 0.0f;
            ne_lhs.vals[row][2] = value;
            ne_lhs.vals[2][row] = value;
        }
        ne_rhs.vals[2] = 0.0f;
    }

    // Solve the normal equations.
    skcms_Matrix4x4 ne_lhs_inv;
    if (!skcms_Matrix4x4_invert(&ne_lhs, &ne_lhs_inv)) {
        return false;
    }
    skcms_Vector4 step = skcms_Matrix4x4_Vector4_mul(&ne_lhs_inv, &ne_rhs);

    // Update the transfer function.
    fn->a += step.vals[0];
    fn->b += step.vals[1];
    fn->e += step.vals[2];
    fn->g += step.vals[3];

    // A should always be positive.
    fn->a = fmaxf(fn->a, 0.0f);

    // Ensure that fn be defined at D.
    if (fn->a * fn->d + fn->b < 0.0f) {
        fn->b = -fn->a * fn->d;
    }

    // Compute the Linfinity error.
    *error_Linfty_after = 0;
    for (int i = 0; i < n; ++i) {
        if (x[i] >= fn->d) {
            float error = fabsf(t[i] - skcms_TransferFunction_eval(fn, x[i]));
            *error_Linfty_after = fmaxf(error, *error_Linfty_after);
        }
    }

    return true;
}

// Solve for A, B, E, and G, given D. The initial value of |fn| is the
// point from which iteration starts.
static bool tf_solve_nonlinear(skcms_TransferFunction* fn,
                               const float* x,
                               const float* t,
                               int n) {
    // Take a maximum of 16 Gauss-Newton steps.
    enum { kNumSteps = 16 };

    // The L-infinity error after each step.
    float step_error[kNumSteps] = { 0 };
    int step = 0;
    for (;; ++step) {
        // If the normal equations are singular, we can't continue.
        if (!tf_gauss_newton_step_nonlinear(fn, &step_error[step], x, t, n)) {
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
    const float kRoundEpsilon = 1.0f / 1024.0f;
    if (fabsf(fn->a - 1.0f) < kRoundEpsilon) {
        fn->a = 1.0f;
    }
    if (fabsf(fn->b) < kRoundEpsilon) {
        fn->b = 0;
    }
    if (fabsf(fn->e) < kRoundEpsilon) {
        fn->e = 0;
    }
    if (fabsf(fn->g - 1.0f) < kRoundEpsilon) {
        fn->g = 1.0f;
    }
    return true;
}

bool skcms_TransferFunction_approximate(skcms_TransferFunction* fn,
                                        const float* x,
                                        const float* t,
                                        int n,
                                        float* max_error) {
    // First, guess at a value of D. Assume that the nonlinear segment applies
    // to all x >= 0.15. This is generally a safe assumption (D is usually less
    // than 0.1).
    const float kLinearSegmentMaximum = 0.15f;
    fn->d = kLinearSegmentMaximum;

    // Do a nonlinear regression on the nonlinear segment. Use a number of guesses
    // for the initial value of G, because not all values will converge.
    bool nonlinear_fit_converged = false;
    {
        float initial_gammas[] = { 2.2f, 1.0f, 3.0f, 0.5f };
        for (int i = 0; i < ARRAY_COUNT(initial_gammas); ++i) {
            fn->g = initial_gammas[i];
            fn->a = 1;
            fn->b = 0;
            fn->c = 1;
            fn->e = 0;
            fn->f = 0;
            if (tf_solve_nonlinear(fn, x, t, n)) {
                nonlinear_fit_converged = true;
                break;
            }
        }
    }
    if (!nonlinear_fit_converged) {
        return false;
    }

    // Now walk back D from our initial guess to the point where our nonlinear
    // fit no longer fits (or all the way to 0 if it fits).
    {
        // Find the L-infinity error of this nonlinear fit (using our old D value).
        float max_error_in_nonlinear_fit = 0;
        for (int i = 0; i < n; ++i) {
            if (x[i] < fn->d) {
                continue;
            }
            float error_at_xi = fabsf(t[i] - skcms_TransferFunction_eval(fn, x[i]));
            max_error_in_nonlinear_fit = fmaxf(max_error_in_nonlinear_fit, error_at_xi);
        }

        // Now find the maximum x value where this nonlinear fit is no longer
        // accurate, no longer defined, or no longer nonnegative.
        fn->d = 0.0f;
        float max_x_where_nonlinear_does_not_fit = -1.0f;
        for (int i = 0; i < n; ++i) {
            if (x[i] >= kLinearSegmentMaximum) {
                continue;
            }

            // The nonlinear segment is only undefined when A * x + B is
            // nonnegative.
            float fn_at_xi = -1;
            if (fn->a * x[i] + fn->b >= 0) {
                fn_at_xi = skcms_TransferFunction_evalUnclamped(fn, x[i]);
            }

            // If the value is negative (or undefined), say that the fit was bad.
            bool nonlinear_fits_xi = true;
            if (fn_at_xi < 0) {
                nonlinear_fits_xi = false;
            }

            // Compute the error, and define "no longer accurate" as "has more than
            // 10% more error than the maximum error in the fit segment".
            if (nonlinear_fits_xi) {
                float error_at_xi = fabsf(t[i] - fn_at_xi);
                if (error_at_xi > 1.1f * max_error_in_nonlinear_fit) {
                    nonlinear_fits_xi = false;
                }
            }

            if (!nonlinear_fits_xi) {
                max_x_where_nonlinear_does_not_fit =
                    fmaxf(max_x_where_nonlinear_does_not_fit, x[i]);
            }
        }

        // Now let D be the highest sample of x that is above the threshold where
        // the nonlinear segment does not fit.
        fn->d = 1.0f;
        for (int i = 0; i < n; ++i) {
            if (x[i] > max_x_where_nonlinear_does_not_fit) {
                fn->d = fminf(fn->d, x[i]);
            }
        }
    }

    // Compute the linear segment, now that we have our definitive D.
    if (fn->d <= 0) {
        // If this has no linear segment, don't try to solve for one.
        fn->c = 1;
        fn->f = 0;
    } else {
        // Set the linear portion such that it go through the origin and be
        // continuous with the nonlinear segment.
        float fn_at_D = skcms_TransferFunction_eval(fn, fn->d);
        fn->c = fn_at_D / fn->d;
        fn->f = 0;
    }

    if (max_error) {
        *max_error = 0;
        for (int i = 0; i < n; ++i) {
            float fn_of_xi = skcms_TransferFunction_eval(fn, x[i]);
            float error_at_xi = fabsf(t[i] - fn_of_xi);
            *max_error = fmaxf(*max_error, error_at_xi);
        }
    }

    return true;
}

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
        if (fabsf(l_at_d - n_at_d) > 0.0001f) {
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

bool skcms_IsSRGB(const skcms_TransferFunction* tf) {
    return tf->g == 157286 / 65536.0f
        && tf->a ==  62119 / 65536.0f
        && tf->b ==   3417 / 65536.0f
        && tf->c ==   5072 / 65536.0f
        && tf->d ==   2651 / 65536.0f
        && tf->e ==      0 / 65536.0f
        && tf->f ==      0 / 65536.0f;
}
