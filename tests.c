/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define expect(cond) \
    if (!(cond)) (fprintf(stderr, "expect(" #cond ") failed at %s:%d\n",__FILE__,__LINE__),exit(1))


static void test_ICCProfile() {
    // Nothing works yet.  :)
    skcms_ICCProfile profile;

    const uint8_t buf[] = { 0x42 };
    expect(!skcms_ICCProfile_parse(&profile, buf, sizeof(buf)));

    skcms_Matrix3x3 toXYZD50;
    expect(!skcms_ICCProfile_toXYZD50(&profile, &toXYZD50));

    skcms_TransferFunction transferFunction;
    expect(!skcms_ICCProfile_getTransferFunction(&profile, &transferFunction));
}

static void test_Transform() {
    // Nothing works yet.  :)
    skcms_ICCProfile src, dst;
    uint8_t buf[16];

    for (skcms_PixelFormat fmt  = skcms_PixelFormat_RGB_565;
                           fmt <= skcms_PixelFormat_BGRA_ffff; fmt++) {
        expect(!skcms_Transform(buf,fmt,&dst,
                                buf,fmt,&src, 1));
    }
}

static void test_FormatConversions() {
    // If we use a single skcms_ICCProfile, we should be able to use skcms_Transform()
    // to do skcms_PixelFormat conversions.
    skcms_ICCProfile profile;

    // We can interpret src as 85 RGB_888 pixels or 64 RGB_8888 pixels.
    uint8_t src[256],
            dst[85*4];
    for (int i = 0; i < 256; i++) {
        src[i] = (uint8_t)i;
    }

    // This should basically be a really complicated memcpy().
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888, &profile,
                           src, skcms_PixelFormat_RGBA_8888, &profile, 64));
    for (int i = 0; i < 256; i++) {
        expect(dst[i] == i);
    }

    // We can do RGBA -> BGRA swaps two ways:
    expect(skcms_Transform(dst, skcms_PixelFormat_BGRA_8888, &profile,
                           src, skcms_PixelFormat_RGBA_8888, &profile, 64));
    for (int i = 0; i < 64; i++) {
        expect(dst[4*i+0] == 4*i+2);
        expect(dst[4*i+1] == 4*i+1);
        expect(dst[4*i+2] == 4*i+0);
        expect(dst[4*i+3] == 4*i+3);
    }
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888, &profile,
                           src, skcms_PixelFormat_BGRA_8888, &profile, 64));
    for (int i = 0; i < 64; i++) {
        expect(dst[4*i+0] == 4*i+2);
        expect(dst[4*i+1] == 4*i+1);
        expect(dst[4*i+2] == 4*i+0);
        expect(dst[4*i+3] == 4*i+3);
    }

    // Let's convert RGB_888 to RGBA_8888...
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888, &profile,
                           src, skcms_PixelFormat_RGB_888  , &profile, 85));
    for (int i = 0; i < 85; i++) {
        expect(dst[4*i+0] == 3*i+0);
        expect(dst[4*i+1] == 3*i+1);
        expect(dst[4*i+2] == 3*i+2);
        expect(dst[4*i+3] ==   255);
    }
    // ... and now all the variants of R-B swaps.
    expect(skcms_Transform(dst, skcms_PixelFormat_BGRA_8888, &profile,
                           src, skcms_PixelFormat_BGR_888  , &profile, 85));
    for (int i = 0; i < 85; i++) {
        expect(dst[4*i+0] == 3*i+0);
        expect(dst[4*i+1] == 3*i+1);
        expect(dst[4*i+2] == 3*i+2);
        expect(dst[4*i+3] ==   255);
    }
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888, &profile,
                           src, skcms_PixelFormat_BGR_888  , &profile, 85));
    for (int i = 0; i < 85; i++) {
        expect(dst[4*i+0] == 3*i+2);
        expect(dst[4*i+1] == 3*i+1);
        expect(dst[4*i+2] == 3*i+0);
        expect(dst[4*i+3] ==   255);
    }
    expect(skcms_Transform(dst, skcms_PixelFormat_BGRA_8888, &profile,
                           src, skcms_PixelFormat_RGB_888  , &profile, 85));
    for (int i = 0; i < 85; i++) {
        expect(dst[4*i+0] == 3*i+2);
        expect(dst[4*i+1] == 3*i+1);
        expect(dst[4*i+2] == 3*i+0);
        expect(dst[4*i+3] ==   255);
    }

    // Let's test in-place transforms.
    // RGBA_8888 and RGB_888 aren't the same size, so we shouldn't allow this call.
    expect(!skcms_Transform(src, skcms_PixelFormat_RGBA_8888, &profile,
                            src, skcms_PixelFormat_RGB_888,   &profile, 85));

    // These two should work fine.
    expect(skcms_Transform(src, skcms_PixelFormat_RGBA_8888, &profile,
                           src, skcms_PixelFormat_BGRA_8888, &profile, 64));
    for (int i = 0; i < 64; i++) {
        expect(src[4*i+0] == 4*i+2);
        expect(src[4*i+1] == 4*i+1);
        expect(src[4*i+2] == 4*i+0);
        expect(src[4*i+3] == 4*i+3);
    }
    expect(skcms_Transform(src, skcms_PixelFormat_BGRA_8888, &profile,
                           src, skcms_PixelFormat_RGBA_8888, &profile, 64));
    for (int i = 0; i < 64; i++) {
        expect(src[4*i+0] == 4*i+0);
        expect(src[4*i+1] == 4*i+1);
        expect(src[4*i+2] == 4*i+2);
        expect(src[4*i+3] == 4*i+3);
    }
}

static void test_FormatConversions_565() {
    // If we use a single skcms_ICCProfile, we should be able to use skcms_Transform()
    // to do skcms_PixelFormat conversions.
    skcms_ICCProfile profile;

    // This should hit all the unique values of each lane of 565.
    uint16_t src[64];
    for (int i = 0; i < 64; i++) {
        src[i] = (uint16_t)( (i/2) <<  0 )
               | (uint16_t)( (i/1) <<  5 )
               | (uint16_t)( (i/2) << 11 );
    }
    expect(src[ 0] == 0x0000);
    expect(src[63] == 0xffff);

    uint32_t dst[64];
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888, &profile,
                           src, skcms_PixelFormat_RGB_565,   &profile, 64));
    // We'll just spot check these results a bit.
    for (int i = 0; i < 64; i++) {
        expect((dst[i] >> 24) == 255);  // All opaque.
    }
    expect(dst[ 0] == 0xff000000);  // 0 -> 0
    expect(dst[20] == 0xff525152);  // (10/31) ≈ (82/255) and (20/63) ≈ (81/255)
    expect(dst[62] == 0xfffffbff);  // (31/31) == (255/255) and (62/63) ≈ (251/255)
    expect(dst[63] == 0xffffffff);  // 1 -> 1
}

int main(void) {
    test_ICCProfile();
    test_Transform();
    test_FormatConversions();
    test_FormatConversions_565();
    return 0;
}
