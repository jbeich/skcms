/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "GaussNewton.h"
#include "LinearAlgebra.h"
#include <assert.h>

bool skcms_gauss_newton_step(float (*     t)(float x, const void*), const void* t_ctx,
                             float (*     f)(float x, const float    P[4]),
                             void  (*grad_f)(float x,       float dfdP[4]),
                             float P[4],
                             const float x0, const float x1, const int N,
                             void  (*fixup)(skcms_Matrix4x4*, const float P[4])) {
    // We'll sample x from the range [x0,x1] (both inclusive) N times with even spacing.
    //
    // We want to do P' = P + (Jf^T Jf)^-1 Jf^T r(P),
    //   where r(P) is the residual vector t(x) - f(x,P)
    //   and Jf is the Jacobian matrix of f(), ∂r/∂P.
    //
    // Let's review the shape of each of these expressions:
    //   r(P)   is [N x 1], a column vector with one entry per value of x tested
    //   Jf     is [N x 4], a matrix with an entry for each (x,P) pair
    //   Jf^T   is [4 x N], the transpose of Jf
    //
    //   Jf^T Jf   is [4 x N] * [N x 4] == [4 x 4], a 4x4 matrix,
    //                                              and so is its inverse (Jf^T Jf)^-1
    //   Jf^T r(P) is [4 x N] * [N x 1] == [4 x 1], a column vector with the same shape as P
    //
    // Our implementation strategy to get to the final ∆P is
    //   1) evaluate Jf^T Jf,   call that lhs
    //   2) evaluate Jf^T r(P), call that rhs
    //   3) invert lhs
    //   4) multiply inverse lhs by rhs
    //
    // This is a friendly implementation strategy because we don't have to have any
    // buffers that scale with N, and equally nice don't have to perform any matrix
    // operations that are variable size.
    //
    // Other implementation strategies could trade this off, e.g. evaluating the
    // pseudoinverse of Jf ( (Jf^T Jf)^-1 Jf^T ) directly, then multiplying that by
    // the residuals.  That would probably require implementing singular value
    // decomposition, and would create a [4 x N] matrix to be multiplied by the
    // [N x 1] residual vector, but on the upside I think that'd eliminate the
    // possibility of this skcms_gauss_newton_step() function ever failing.

    // 0) start off with lhs and rhs safely zeroed.
    skcms_Matrix4x4 lhs = {{ {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} }};
    skcms_Vector4   rhs = {  {0,0,0,0} };

    // 1,2) evaluate lhs and evaluate rhs
    //   We want to evaluate Jf only once, but both lhs and rhs involve Jf^T,
    //   so we'll have to update lhs and rhs at the same time.
    float dx = (x1-x0)/(N-1);
    for (int i = 0; i < N; i++) {
        float x = x0 + i*dx;

        float resid = t(x,t_ctx) - f(x,P);

        float dfdP[4] = {0,0,0,0};
        grad_f(x, dfdP);

        // TODO: allow a bias(x) function?
        // As-is, this bias(x) can be folded into t(x), f(x), and grad_f(x).
    #if 0
        float b = bias(x, bias_ctx);
        resid   *= b;
        dfdP[0] *= b;
        dfdP[1] *= b;
        dfdP[2] *= b;
        dfdP[3] *= b;
    #endif

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                lhs.vals[r][c] += dfdP[r] * dfdP[c];
            }
            rhs.vals[r] += dfdP[r] * resid;
        }
    }

    // 3) invert lhs
    if (fixup) {
        fixup(&lhs, P);
    }
    skcms_Matrix4x4 lhs_inv;
    if (!skcms_Matrix4x4_invert(&lhs, &lhs_inv)) {
        return false;
    }

    // 4) multiply inverse lhs by rhs
    skcms_Vector4 dP = skcms_Matrix4x4_Vector4_mul(&lhs_inv, &rhs);
    P[0] += dP.vals[0];
    P[1] += dP.vals[1];
    P[2] += dP.vals[2];
    P[3] += dP.vals[3];
    return true;
}
