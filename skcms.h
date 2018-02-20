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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A row-major 3x3 matrix (ie vals[row][col])
typedef struct {
    float vals[3][3];
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

// Return true if the skcms_TransferFunction is the sRGB transfer function,
// within an unspecified tolerance.
bool skcms_IsSRGB(const skcms_TransferFunction*);

// Unified representation of 'curv' or 'para' tag data, or a 1D table from 'mft1' or 'mft2'
typedef struct {
    skcms_TransferFunction parametric;
    const uint8_t*         table_8;
    const uint8_t*         table_16;
    uint32_t               table_size;
} skcms_Curve;

typedef struct {
    // Optional: 1D curves, followed by an N-dimensional CLUT.
    // If input_channels == 0, these curves and CLUT are skipped,
    // Otherwise, input_channels must be in [1, 4].
    uint32_t        input_channels;
    skcms_Curve     input_curves[4];
    uint32_t        grid_points;
    const void*     grid;

    // Optional: 1D curves, followed by a color matrix.
    // If matrix_channels == 0, these curves and matrix are skipped,
    // Otherwise, matrix_channels must be 3.
    int             matrix_channels;
    skcms_Curve     matrix_curves[3];
    skcms_Matrix3x3 matrix;

    // Required: 1D curves. Always present, and output_channels must be 3.
    uint32_t        output_channels;
    skcms_Curve     output_curves[3];
} skcms_A2B;

typedef struct {
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
} skcms_ICCDateTime;

typedef struct {
    const uint8_t* buffer;

    uint32_t size;
    uint32_t cmm_type;
    uint32_t version;
    uint32_t profile_class;
    uint32_t data_color_space;
    uint32_t pcs;
    skcms_ICCDateTime creation_date_time;
    uint32_t signature;
    uint32_t platform;
    uint32_t flags;
    uint32_t device_manufacturer;
    uint32_t device_model;
    uint64_t device_attributes;
    uint32_t rendering_intent;
    float illuminant_X;
    float illuminant_Y;
    float illuminant_Z;
    uint32_t creator;
    uint8_t profile_id[16];
    uint32_t tag_count;

    // skcms_Parse() will set commonly-used fields for you when possible:

    // If the transfer functions for all color channels in this profile are
    // identical and can be represented by a single skcms_TransferFunction,
    // skcms_Parse() sets tf to that function and has_tf to true.
    bool                   has_tf;
    skcms_TransferFunction tf;

    // If this profile's gamut can be represented by a 3x3 transform to XYZD50,
    // skcms_Parse() sets toXYZD50 to that transform and has_toXYZD50 to true.
    bool                   has_toXYZD50;
    skcms_Matrix3x3        toXYZD50;

    // If the profile has a valid A2B0 tag, skcms_Parse() sets A2B to that data,
    // and has_A2B to true.
    bool                   has_A2B;
    skcms_A2B              A2B;
} skcms_ICCProfile;

// Parse an ICC profile and return true if possible, otherwise return false.
// The buffer is not copied, it must remain valid as long as the skcms_ICCProfile
// will be used.
bool skcms_Parse(const void*, size_t, skcms_ICCProfile*);

bool skcms_ApproximateTransferFunction(const skcms_ICCProfile*, skcms_TransferFunction*
                                                              , float* max_error);

typedef struct {
    uint32_t       signature;
    uint32_t       type;
    uint32_t       size;
    const uint8_t* buf;
} skcms_ICCTag;

void skcms_GetTagByIndex    (const skcms_ICCProfile*, uint32_t idx, skcms_ICCTag*);
bool skcms_GetTagBySignature(const skcms_ICCProfile*, uint32_t sig, skcms_ICCTag*);

typedef enum {
    skcms_PixelFormat_RGB_565,
    skcms_PixelFormat_BGR_565,

    skcms_PixelFormat_RGB_888,
    skcms_PixelFormat_BGR_888,
    skcms_PixelFormat_RGBA_8888,
    skcms_PixelFormat_BGRA_8888,

    skcms_PixelFormat_RGB_101010x,
    skcms_PixelFormat_BGR_101010x,
    skcms_PixelFormat_RGBA_1010102,
    skcms_PixelFormat_BGRA_1010102,

    skcms_PixelFormat_RGB_161616,     // Big-endian.  Pointers must be 16-bit aligned.
    skcms_PixelFormat_BGR_161616,
    skcms_PixelFormat_RGBA_16161616,
    skcms_PixelFormat_BGRA_16161616,

    skcms_PixelFormat_RGB_hhh,        // 1-5-10 half-precision float.
    skcms_PixelFormat_BGR_hhh,        // Pointers must be 16-bit aligned.
    skcms_PixelFormat_RGBA_hhhh,
    skcms_PixelFormat_BGRA_hhhh,

    skcms_PixelFormat_RGB_fff,        // 1-8-23 single-precision float (the normal kind).
    skcms_PixelFormat_BGR_fff,        // Pointers must be 32-bit aligned.
    skcms_PixelFormat_RGBA_ffff,
    skcms_PixelFormat_BGRA_ffff,
} skcms_PixelFormat;

// TODO: do we want/need to support anything other than unpremul input, unpremul output?

// Convert npixels pixels from src format and color profile to dst format and color profile
// and return true, otherwise return false.  It is safe to alias dst == src if dstFmt == srcFmt.
bool skcms_Transform(const void* src, skcms_PixelFormat srcFmt, const skcms_ICCProfile* srcProfile,
                           void* dst, skcms_PixelFormat dstFmt, const skcms_ICCProfile* dstProfile,
                     size_t npixels);

#ifdef __cplusplus
}
#endif
