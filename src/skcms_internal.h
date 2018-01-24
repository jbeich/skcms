/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// skcms_internal.h contains internal shared types and functions called across translation units

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A row-major 4x4 matrix (ie vals[row][col])
typedef struct {
    float vals[4][4];
} skcms_Matrix4x4;

typedef struct {
    float vals[4];
} skcms_Vector4;


float skcms_TransferFunction_eval(const skcms_TransferFunction*, float);
float skcms_TransferFunction_evalUnclamped(const skcms_TransferFunction*, float);

bool skcms_TransferFunction_invert(const skcms_TransferFunction*, skcms_TransferFunction*);

bool skcms_TransferFunction_approximate(skcms_TransferFunction*,
                                        const float* x, const float* t, size_t n);

#ifdef __cplusplus
}
#endif
