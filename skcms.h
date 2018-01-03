/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// skcms.h contains the entire public API for skcms.

#include <stdbool.h>
#include <stddef.h>

// A column-major 3x3 matrix,
// | vals[0] vals[3] vals[6] |
// | vals[1] vals[4] vals[7] |
// | vals[2] vals[5] vals[8] |
typedef struct {
    float vals[9];
} skcms_Matrix3x3;


// A transfer function mapping encoded values to linear values,
// represented by this 7-parameter piecewise function:
//
//   linear = sign(encoded) *  (c*|encoded| + f)       , 0 <= |encoded| < d
//          = sign(encoded) * ((a*|encoded| + b)^g + e), d <= |encoded|
//
// (A simple gamma transfer function sets g to gamma and a to 1.)
typedef struct {
    float g, a,b,c,d,e,f;
} skcms_TransferFunction;


typedef struct {
    void* placeholder;
} skcms_ICCProfile;

// Parse an ICC profile and return true if possible, otherwise return false.
bool skcms_ICCProfile_parse(skcms_ICCProfile*, const void*, size_t);

// If this profile's gamut can be represented by a 3x3 transform to XYZD50,
// set it (if non-NULL) and return true, otherwise return false.
bool skcms_ICCProfile_toXYZD50(const skcms_ICCProfile*, skcms_Matrix3x3*);

// If the transfer functions for all color channels in this profile are
// identical and can be represented by a single skcms_TransferFunction, set it
// (if non-NULL) and return true, otherwise return false.
bool skcms_ICCProfile_getTransferFunction(const skcms_ICCProfile*, skcms_TransferFunction*);

// TODO: read table-based transfer functions
