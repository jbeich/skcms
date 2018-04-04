/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "LinearAlgebra.h"

static bool Matrix4x4_isfinite(const skcms_Matrix4x4* m) {
    for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) {
        if (!isfinite_(m->vals[r][c])) {
            return false;
        }
    }
    return true;
}

bool skcms_Matrix4x4_invert(const skcms_Matrix4x4* src, skcms_Matrix4x4* dst) {
    double a00 = src->vals[0][0],
           a01 = src->vals[1][0],
           a02 = src->vals[2][0],
           a03 = src->vals[3][0],
           a10 = src->vals[0][1],
           a11 = src->vals[1][1],
           a12 = src->vals[2][1],
           a13 = src->vals[3][1],
           a20 = src->vals[0][2],
           a21 = src->vals[1][2],
           a22 = src->vals[2][2],
           a23 = src->vals[3][2],
           a30 = src->vals[0][3],
           a31 = src->vals[1][3],
           a32 = src->vals[2][3],
           a33 = src->vals[3][3];

    double b00 = a00*a11 - a01*a10,
           b01 = a00*a12 - a02*a10,
           b02 = a00*a13 - a03*a10,
           b03 = a01*a12 - a02*a11,
           b04 = a01*a13 - a03*a11,
           b05 = a02*a13 - a03*a12,
           b06 = a20*a31 - a21*a30,
           b07 = a20*a32 - a22*a30,
           b08 = a20*a33 - a23*a30,
           b09 = a21*a32 - a22*a31,
           b10 = a21*a33 - a23*a31,
           b11 = a22*a33 - a23*a32;

    double determinant = b00*b11
                       - b01*b10
                       + b02*b09
                       + b03*b08
                       - b04*b07
                       + b05*b06;

    if (determinant == 0) {
        return false;
    }

    double invdet = 1.0 / determinant;
    if (!isfinite_(invdet)) {
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

    dst->vals[0][0] = ( a11*b11 - a12*b10 + a13*b09 );
    dst->vals[1][0] = ( a02*b10 - a01*b11 - a03*b09 );
    dst->vals[2][0] = ( a31*b05 - a32*b04 + a33*b03 );
    dst->vals[3][0] = ( a22*b04 - a21*b05 - a23*b03 );
    dst->vals[0][1] = ( a12*b08 - a10*b11 - a13*b07 );
    dst->vals[1][1] = ( a00*b11 - a02*b08 + a03*b07 );
    dst->vals[2][1] = ( a32*b02 - a30*b05 - a33*b01 );
    dst->vals[3][1] = ( a20*b05 - a22*b02 + a23*b01 );
    dst->vals[0][2] = ( a10*b10 - a11*b08 + a13*b06 );
    dst->vals[1][2] = ( a01*b08 - a00*b10 - a03*b06 );
    dst->vals[2][2] = ( a30*b04 - a31*b02 + a33*b00 );
    dst->vals[3][2] = ( a21*b02 - a20*b04 - a23*b00 );
    dst->vals[0][3] = ( a11*b07 - a10*b09 - a12*b06 );
    dst->vals[1][3] = ( a00*b09 - a01*b07 + a02*b06 );
    dst->vals[2][3] = ( a31*b01 - a30*b03 - a32*b00 );
    dst->vals[3][3] = ( a20*b03 - a21*b01 + a22*b00 );

    return Matrix4x4_isfinite(dst);
}

bool skcms_Matrix3x3_invert(const skcms_Matrix3x3* src, skcms_Matrix3x3* dst) {
    // TODO: I am being _very_ lazy.
    skcms_Matrix4x4 m = {{
        { (double)src->vals[0][0], (double)src->vals[0][1], (double)src->vals[0][2], 0.0 },
        { (double)src->vals[1][0], (double)src->vals[1][1], (double)src->vals[1][2], 0.0 },
        { (double)src->vals[2][0], (double)src->vals[2][1], (double)src->vals[2][2], 0.0 },
        {                     0.0,                     0.0,                     0.0, 1.0 },
    }};

    skcms_Matrix4x4 inv;
    if (!skcms_Matrix4x4_invert(&m, &inv)) {
        return false;
    }

    *dst = (skcms_Matrix3x3){{
        { (float)inv.vals[0][0], (float)inv.vals[0][1], (float)inv.vals[0][2] },
        { (float)inv.vals[1][0], (float)inv.vals[1][1], (float)inv.vals[1][2] },
        { (float)inv.vals[2][0], (float)inv.vals[2][1], (float)inv.vals[2][2] },
    }};
    return true;
}

skcms_Vector4 skcms_Matrix4x4_Vector4_mul(const skcms_Matrix4x4* m, const skcms_Vector4* v) {
    skcms_Vector4 dst = {{0,0,0,0}};
    for (int row = 0; row < 4; ++row) {
        dst.vals[row] = m->vals[row][0] * v->vals[0]
                      + m->vals[row][1] * v->vals[1]
                      + m->vals[row][2] * v->vals[2]
                      + m->vals[row][3] * v->vals[3];
    }
    return dst;
}
