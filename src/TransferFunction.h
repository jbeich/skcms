/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// TransferFunction.h contains skcms-private APIs for working with skcms_TransferFunction.

#include <stdbool.h>

float skcms_TransferFunction_eval(const skcms_TransferFunction*, float);

// Fit c,d,f parametercs of an skcms_TransferFunction within a given tolerance
// to first 0 < L ≤ N points on an skcms_Curve, returning L.
int skcms_fit_linear(const skcms_Curve*, int N, float tol, skcms_TransferFunction*);

bool skcms_TransferFunction_invert(const skcms_TransferFunction*, skcms_TransferFunction*);
