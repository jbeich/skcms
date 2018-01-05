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
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
} skcms_ICCDateTime;

typedef struct {
    const uint8_t* buf;
    size_t         len;  // Length of profile, may be smaller than len passed to parse
} skcms_ICCProfile;

// Parse an ICC profile and return true if possible, otherwise return false.
// The buffer is not copied, it must remain valid as long as the skcms_ICCProfile
// will be used.
bool skcms_ICCProfile_parse(skcms_ICCProfile*, const void*, size_t);

// If this profile's gamut can be represented by a 3x3 transform to XYZD50,
// set it (if non-NULL) and return true, otherwise return false.
bool skcms_ICCProfile_toXYZD50(const skcms_ICCProfile*, skcms_Matrix3x3*);

// If the transfer functions for all color channels in this profile are
// identical and can be represented by a single skcms_TransferFunction, set it
// (if non-NULL) and return true, otherwise return false.
bool skcms_ICCProfile_getTransferFunction(const skcms_ICCProfile*, skcms_TransferFunction*);

// Accessors for simple (non-prescribed) ICCProfile properties from the header
uint32_t skcms_ICCProfile_getSize(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getCMMType(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getVersion(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getProfileClass(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getDataColorSpace(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getPCS(const skcms_ICCProfile*);
void skcms_ICCProfile_getCreationDateTime(const skcms_ICCProfile*, skcms_ICCDateTime*);
uint32_t skcms_ICCProfile_getPlatform(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getFlags(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getDeviceManufacturer(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getDeviceModel(const skcms_ICCProfile*);
uint64_t skcms_ICCProfile_getDeviceAttributes(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getRenderingIntent(const skcms_ICCProfile*);
uint32_t skcms_ICCProfile_getCreator(const skcms_ICCProfile*);

// TODO: read table-based transfer functions

typedef enum {
    skcms_PixelFormat_RGB_565,
    skcms_PixelFormat_BGR_565,

    skcms_PixelFormat_RGB_888,
    skcms_PixelFormat_BGR_888,

    skcms_PixelFormat_RGBA_8888,
    skcms_PixelFormat_BGRA_8888,

    skcms_PixelFormat_RGB_161616,     // Big-endian.
    skcms_PixelFormat_BGR_161616,

    skcms_PixelFormat_RGBA_16161616,  // Big-endian.
    skcms_PixelFormat_BGRA_16161616,

    skcms_PixelFormat_RGBA_hhhh,      // 1-5-10 half-precision float.
    skcms_PixelFormat_BGRA_hhhh,

    skcms_PixelFormat_RGBA_ffff,      // 1-8-23 single-precision float (the normal kind).
    skcms_PixelFormat_BGRA_ffff,
} skcms_PixelFormat;

// TODO: do we want/need to support anything other than unpremul input, unpremul output?

// Convert npixels pixels from src format and color profile to dst format and color profile
// and return true, otherwise return false.  It is safe to alias dst == src if dstFmt == srcFmt.
bool skcms_Transform(void* dst, skcms_PixelFormat dstFmt, const skcms_ICCProfile* dstProfile,
               const void* src, skcms_PixelFormat srcFmt, const skcms_ICCProfile* srcProfile,
                     size_t npixels);

#ifdef __cplusplus
}
#endif
