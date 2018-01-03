/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"

bool skcms_Transform(void* dst, skcms_PixelFormat dstFmt, const skcms_ICCProfile* dstProfile,
               const void* src, skcms_PixelFormat srcFmt, const skcms_ICCProfile* srcProfile,
                     int npixels) {
    (void)dst;
    (void)dstFmt;
    (void)dstProfile;
    (void)src;
    (void)srcFmt;
    (void)srcProfile;
    (void)npixels;
    return false;
}
