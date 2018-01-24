/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <math.h>
#include <stdio.h>

#define USE_DOUBLE

static float sk_pow(float b, float e) {
#ifdef USE_DOUBLE
    return (float)pow((double)b, (double)e);
#else
    return powf(b, e);
#endif
}

static float sk_log(float x) {
#ifdef USE_DOUBLE
    return (float)log((double)x);
#else
    return logf(x);
#endif
}

float skcms_TransferFunction_evalUnclamped(const skcms_TransferFunction* fn, float x) {
    if (x < fn->d) {
        return fn->c * x + fn->f;
    }
    return sk_pow(fn->a * x + fn->b, fn->g) + fn->e;
}

float skcms_TransferFunction_eval(const skcms_TransferFunction* fn, float x) {
    float fn_at_x_unclamped = skcms_TransferFunction_evalUnclamped(fn, x);
    return fminf(fmaxf(fn_at_x_unclamped, 0.f), 1.f);
}

// Evaluate the gradient of the nonlinear component of fn
static void tf_eval_gradient_nonlinear(const skcms_TransferFunction* fn,
                                       float x,
                                       float* d_fn_d_A_at_x,
                                       float* d_fn_d_B_at_x,
                                       float* d_fn_d_E_at_x,
                                       float* d_fn_d_G_at_x) {
    float base = fn->a * x + fn->b;
    if (base > 0.f) {
        *d_fn_d_A_at_x = fn->g * x * sk_pow(base, fn->g - 1.f);
        *d_fn_d_B_at_x = fn->g * sk_pow(base, fn->g - 1.f);
        *d_fn_d_E_at_x = 1.f;
        *d_fn_d_G_at_x = sk_pow(base, fn->g) * sk_log(base);
    } else {
        *d_fn_d_A_at_x = 0.f;
        *d_fn_d_B_at_x = 0.f;
        *d_fn_d_E_at_x = 0.f;
        *d_fn_d_G_at_x = 0.f;
    }
}

static bool is_matrix_finite(const skcms_Matrix4x4* mtx) {
    float accumulator = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            accumulator *= mtx->vals[r][c];
        }
    }
    return isfinite(accumulator);
}

static bool invert_matrix(skcms_Matrix4x4* dst, const skcms_Matrix4x4* src) {
    double a00 = (double)src->vals[0][0],
           a01 = (double)src->vals[1][0],
           a02 = (double)src->vals[2][0],
           a03 = (double)src->vals[3][0],
           a10 = (double)src->vals[0][1],
           a11 = (double)src->vals[1][1],
           a12 = (double)src->vals[2][1],
           a13 = (double)src->vals[3][1],
           a20 = (double)src->vals[0][2],
           a21 = (double)src->vals[1][2],
           a22 = (double)src->vals[2][2],
           a23 = (double)src->vals[3][2],
           a30 = (double)src->vals[0][3],
           a31 = (double)src->vals[1][3],
           a32 = (double)src->vals[2][3],
           a33 = (double)src->vals[3][3];

    double b00 = a00 * a11 - a01 * a10,
           b01 = a00 * a12 - a02 * a10,
           b02 = a00 * a13 - a03 * a10,
           b03 = a01 * a12 - a02 * a11,
           b04 = a01 * a13 - a03 * a11,
           b05 = a02 * a13 - a03 * a12,
           b06 = a20 * a31 - a21 * a30,
           b07 = a20 * a32 - a22 * a30,
           b08 = a20 * a33 - a23 * a30,
           b09 = a21 * a32 - a22 * a31,
           b10 = a21 * a33 - a23 * a31,
           b11 = a22 * a33 - a23 * a32;

    // Calculate the determinant
    double det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;

    double invdet = 1.0 / det;
    // If det is zero, we want to return false. However, we also want to return false
    // if 1/det overflows to infinity (i.e. det is denormalized). Both of these are
    // handled by checking that 1/det is finite.
    if (!isfinite(invdet)) {
        return false;
    }

    b00 *= invdet;
    b01 *= invdet;
    b02 *= invdet;
    b03 *= invdet;
    b04 *= invdet;
    b05 *= invdet;
    b06 *= invdet;
    b07 *= invdet;
    b08 *= invdet;
    b09 *= invdet;
    b10 *= invdet;
    b11 *= invdet;

    dst->vals[0][0] = (float)(a11 * b11 - a12 * b10 + a13 * b09);
    dst->vals[1][0] = (float)(a02 * b10 - a01 * b11 - a03 * b09);
    dst->vals[2][0] = (float)(a31 * b05 - a32 * b04 + a33 * b03);
    dst->vals[3][0] = (float)(a22 * b04 - a21 * b05 - a23 * b03);
    dst->vals[0][1] = (float)(a12 * b08 - a10 * b11 - a13 * b07);
    dst->vals[1][1] = (float)(a00 * b11 - a02 * b08 + a03 * b07);
    dst->vals[2][1] = (float)(a32 * b02 - a30 * b05 - a33 * b01);
    dst->vals[3][1] = (float)(a20 * b05 - a22 * b02 + a23 * b01);
    dst->vals[0][2] = (float)(a10 * b10 - a11 * b08 + a13 * b06);
    dst->vals[1][2] = (float)(a01 * b08 - a00 * b10 - a03 * b06);
    dst->vals[2][2] = (float)(a30 * b04 - a31 * b02 + a33 * b00);
    dst->vals[3][2] = (float)(a21 * b02 - a20 * b04 - a23 * b00);
    dst->vals[0][3] = (float)(a11 * b07 - a10 * b09 - a12 * b06);
    dst->vals[1][3] = (float)(a00 * b09 - a01 * b07 + a02 * b06);
    dst->vals[2][3] = (float)(a31 * b01 - a30 * b03 - a32 * b00);
    dst->vals[3][3] = (float)(a20 * b03 - a21 * b01 + a22 * b00);

    return is_matrix_finite(dst);
}

static void mul_matrix_vec(skcms_Vector4* dst, const skcms_Matrix4x4* m, const skcms_Vector4* v) {
    for (int row = 0; row < 4; ++row) {
        dst->vals[row] = m->vals[row][0] * v->vals[0] +
                         m->vals[row][1] * v->vals[1] +
                         m->vals[row][2] * v->vals[2] +
                         m->vals[row][3] * v->vals[3];
    }
}

// Take one Gauss-Newton step updating A, B, E, and G, given D.
static bool tf_gauss_newton_step_nonlinear(skcms_TransferFunction* fn,
                                           float* error_Linfty_after,
                                           const float* x,
                                           const float* t,
                                           size_t n) {
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
    for (size_t i = 0; i < n; ++i) {
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
    float kEpsilonForG = 1.f / 1024.f;
    if (fabsf(fn->g - 1.f) < kEpsilonForG) {
        for (int row = 0; row < 4; ++row) {
            float value = (row == 2) ? 1.f : 0.f;
            ne_lhs.vals[row][2] = value;
            ne_lhs.vals[2][row] = value;
        }
        ne_rhs.vals[2] = 0.f;
    }

    // Solve the normal equations.
    skcms_Matrix4x4 ne_lhs_inv;
    if (!invert_matrix(&ne_lhs_inv, &ne_lhs)) {
        return false;
    }
    skcms_Vector4 step;
    mul_matrix_vec(&step, &ne_lhs_inv, &ne_rhs);

    // Update the transfer function.
    fn->a += step.vals[0];
    fn->b += step.vals[1];
    fn->e += step.vals[2];
    fn->g += step.vals[3];

    // A should always be positive.
    fn->a = fmaxf(fn->a, 0.f);

    // Ensure that fn be defined at D.
    if (fn->a * fn->d + fn->b < 0.f) {
        fn->b = -fn->a * fn->d;
    }

    // Compute the Linfinity error.
    *error_Linfty_after = 0;
    for (size_t i = 0; i < n; ++i) {
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
                               size_t n) {
    // Take a maximum of 8 Gauss-Newton steps.
    enum { kNumSteps = 8 };

    // The L-infinity error after each step.
    float step_error[kNumSteps] = { 0 };
    size_t step = 0;
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
        float kEarlyOutTinyErrorThreshold = (1.f / 16.f) / 256.f;
        if (step_error[step] < kEarlyOutTinyErrorThreshold) {
            break;
        }

        // Stop if our error is not changing, or changing in the wrong direction.
        if (step > 1) {
            // If our error is is huge for two iterations, we're probably not in the
            // region of convergence.
            if (step_error[step] > 1.f && step_error[step - 1] > 1.f) {
                return false;
            }

            // If our error didn't change by ~1%, assume we've converged as much as we
            // are going to.
            const float kEarlyOutByPercentChangeThreshold = 32.f / 256.f;
            const float kMinimumPercentChange = 1.f / 128.f;
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
    float kDidNotConvergeThreshold = 64.f / 256.f;
    if (step_error[step] > kDidNotConvergeThreshold) {
        return false;
    }

    // We've converged to a reasonable solution. If some of the parameters are
    // extremely close to 0 or 1, set them to 0 or 1.
    const float kRoundEpsilon = 1.f / 1024.f;
    if (fabsf(fn->a - 1.f) < kRoundEpsilon) {
        fn->a = 1.f;
    }
    if (fabsf(fn->b) < kRoundEpsilon) {
        fn->b = 0;
    }
    if (fabsf(fn->e) < kRoundEpsilon) {
        fn->e = 0;
    }
    if (fabsf(fn->g - 1.f) < kRoundEpsilon) {
        fn->g = 1.f;
    }
    return true;
}

bool skcms_TransferFunction_approximate(skcms_TransferFunction* fn,
                                        const float* x,
                                        const float* t,
                                        size_t n) {
    // First, guess at a value of D. Assume that the nonlinear segment applies
    // to all x >= 0.15. This is generally a safe assumption (D is usually less
    // than 0.1).
    const float kLinearSegmentMaximum = 0.15f;
    fn->d = kLinearSegmentMaximum;

    // Do a nonlinear regression on the nonlinear segment. Use a number of guesses
    // for the initial value of G, because not all values will converge.
    bool nonlinear_fit_converged = false;
    {
        enum { kNumInitialGammas = 4 };
        float initial_gammas[kNumInitialGammas] = { 2.2f, 1.f, 3.f, 0.5f };
        for (size_t i = 0; i < kNumInitialGammas; ++i) {
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
        for (size_t i = 0; i < n; ++i) {
            if (x[i] < fn->d) {
                continue;
            }
            float error_at_xi = fabsf(t[i] - skcms_TransferFunction_eval(fn, x[i]));
            max_error_in_nonlinear_fit = fmaxf(max_error_in_nonlinear_fit, error_at_xi);
        }

        // Now find the maximum x value where this nonlinear fit is no longer
        // accurate, no longer defined, or no longer nonnegative.
        fn->d = 0.f;
        float max_x_where_nonlinear_does_not_fit = -1.f;
        for (size_t i = 0; i < n; ++i) {
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
        fn->d = 1.f;
        for (size_t i = 0; i < n; ++i) {
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
    return true;
}
