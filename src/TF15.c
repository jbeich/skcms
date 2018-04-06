/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "GaussNewton.h"
#include "PortableMath.h"
#include "TransferFunction.h"
#include <limits.h>
#include <string.h>

// Evaluating skcms_TF15{A,B,C,D} at x:
//   f(x) = Ax^5 + Bx^4 + Cx^3 + Dx^2 + (1 - A - B - C - D)x
//
//   ∂f/∂A = x^5 - x
//   ∂f/∂B = x^4 - x
//   ∂f/∂C = x^3 - x
//   ∂f/∂D = x^2 - x

static float eval_15(float x, const float P[4]) {
    return P[0]*x*x*x*x*x
         + P[1]*x*x*x*x
         + P[2]*x*x*x
         + P[3]*x*x
         + (1 - P[0] - P[1] - P[2] - P[3]) * x;
}
static void grad_15(float x, float dfdP[4]) {
    dfdP[0] = x*x*x*x*x - x;
    dfdP[1] = x*x*x*x   - x;
    dfdP[2] = x*x*x     - x;
    dfdP[3] = x*x       - x;
}

static float eval_curve(float x, const void* vctx) {
    const skcms_Curve* curve = (const skcms_Curve*)vctx;

    if (curve->table_entries == 0) {
        return skcms_TransferFunction_eval(&curve->parametric, x);
    }

    // TODO: today we should always hit an entry exactly, but if that changes, lerp?
    int ix = (int)( x * (curve->table_entries - 1) );

    if (curve->table_8) {
        return curve->table_8[ix] * (1/255.0f);
    } else {
        uint16_t be;
        memcpy(&be, curve->table_16 + 2*ix, 2);

        uint16_t le = ((be << 8) | (be >> 8)) & 0xffff;
        return le * (1/65535.0f);
    }
}

bool skcms_ApproximateCurve15(const skcms_Curve* curve, skcms_TF15* approx, float* max_error) {
    // Start a guess at skcms_TF15{0,0,1}, i.e. f(x) = x^2, i.e. gamma = 2.
    // TODO: guess better somehow, like we do in skcms_ApproximateCurve()?
    float P[4] = { 0,0,1, 0 };

    if (curve->table_entries > (uint32_t)INT_MAX) {
        // That's just crazy.
        return false;
    }
    const int N = curve->table_entries == 0 ? 257 /*TODO: tune?*/
                                            : (int)curve->table_entries;

    for (int i = 0; i < 3/*TODO: Tune???*/; i++) {
        if (!skcms_gauss_newton_step(eval_curve, curve,
                                     eval_15, grad_15,
                                     P, 0,1,N,
                                     NULL)) {
            return false;
        }
    }

    *max_error = 0;
    for (int i = 0; i < N; i++) {
        float x = i * (1.0f / (N-1));

        float err = fabsf_( eval_curve(x, curve) - eval_15(x, P) );
        if (err > *max_error) {
            *max_error = err;
        }
    }
    approx->A = P[0];
    approx->B = P[1];
    approx->C = P[2];
    approx->D = P[3];
    return true;
}
