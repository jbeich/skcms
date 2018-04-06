/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "GaussNewton.h"

// Evaluating skcms_TF23{A,B} at x:
//   f(x) == Ax^3 + Bx^2 + (1-A-B)
//
//   ∂f/∂A = x^3 - 1
//   ∂f/∂B = x^2 - 1

static float eval_23(float x, const float P[4]) {
    return P[0]*x*x*x
         + P[1]*x*x
         + (1 - P[0] - P[1]);
}
static void grad_23(float x, float dfdP[4]) {
    dfdp[0] = x*x*x - 1;
    dfdp[1] = x*x   - 1;
}


static float eval_curve(float x, const void* vctx) {
    const skcms_Curve* curve = (const skcms_Curve*)vctx;
    // TODO
}

bool skcms_ApproximateCurve23(const skcms_Curve* curve, skcms_TF23* approx, float* max_error) {
    // Start a guess at skcms_TF23{0,1}, i.e. f(x) = x^2, i.e. gamma = 2.
    float P[4] = { 0, 1, 0, 0 };

    for (int i = 0; i < 10; i++) {

        //if (skcms_gauss_newton_step(
    }

    (void)curve;
    (void)approx;
    (void)max_error;
    return false;
}
