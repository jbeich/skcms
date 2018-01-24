/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "skcms.h"
#include "src/skcms_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define expect(cond) \
    if (!(cond)) (fprintf(stderr, "expect(" #cond ") failed at %s:%d\n",__FILE__,__LINE__),exit(1))

// Compilers can be a little nervous about exact float equality comparisons.
#define expect_eq(a, b) expect((a) <= (b) && (b) <= (a))
#define expect_eq2(a, b, c) expect( ((a) <= (b) && (b) <= (a)) || ((a) <= (c) && (c) <= (a)) )

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

    uint32_t _8888[3] = { 0x03020100, 0x07060504, 0x0b0a0908 };
    uint8_t _888[9];
    expect(skcms_Transform(_888 , skcms_PixelFormat_RGB_888  , &profile,
                           _8888, skcms_PixelFormat_RGBA_8888, &profile, 3));
    expect(_888[0] == 0 && _888[1] == 1 && _888[2] ==  2);
    expect(_888[3] == 4 && _888[4] == 5 && _888[5] ==  6);
    expect(_888[6] == 8 && _888[7] == 9 && _888[8] == 10);
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
    expect(src[31] == 0x7bef);
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

    // Let's convert back the other way.
    uint16_t back[64];
    expect(skcms_Transform(back, skcms_PixelFormat_RGB_565  , &profile,
                            dst, skcms_PixelFormat_RGBA_8888, &profile, 64));
    for (int i = 0; i < 64; i++) {
        expect(src[i] == back[i]);
    }
}

static void test_FormatConversions_16161616() {
    skcms_ICCProfile profile;

    // We want to hit each 16-bit value, 4 per each of 16384 pixels.
    uint64_t* src = malloc(8 * 16384);
    for (int i = 0; i < 16384; i++) {
        src[i] = (uint64_t)(4*i + 0) <<  0
               | (uint64_t)(4*i + 1) << 16
               | (uint64_t)(4*i + 2) << 32
               | (uint64_t)(4*i + 3) << 48;
    }
    expect(src[    0] == 0x0003000200010000);
    expect(src[ 8127] == 0x7eff7efe7efd7efc);  // This should demonstrate interesting rounding.
    expect(src[16383] == 0xfffffffefffdfffc);

    uint32_t* dst = malloc(4 * 16384);
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888    , &profile,
                           src, skcms_PixelFormat_RGBA_16161616, &profile, 16384));

    // skcms_Transform() will treat src as holding big-endian 16-bit values,
    // so the low lanes are actually the most significant byte, and the high least.

    expect(dst[    0] == 0x03020100);
    expect(dst[ 8127] == 0xfefefdfc);  // 0x7eff rounds down to 0xfe, 0x7efe rounds up to 0xfe.
    expect(dst[16383] == 0xfffefdfc);

    // We've lost precision when transforming to 8-bit, so these won't quite round-trip.
    // Instead we should see the 8-bit dst value byte-doubled, as 65535/255 = 257 = 0x0101.
    uint64_t* back = malloc(8 * 16384);
    expect(skcms_Transform(back, skcms_PixelFormat_RGBA_16161616, &profile,
                           dst , skcms_PixelFormat_RGBA_8888    , &profile, 16384));
    for (int i = 0; i < 16384; i++) {
        expect( ((back[i] >>  0) & 0xffff) == ((dst[i] >>  0) & 0xff) * 0x0101);
        expect( ((back[i] >> 16) & 0xffff) == ((dst[i] >>  8) & 0xff) * 0x0101);
        expect( ((back[i] >> 32) & 0xffff) == ((dst[i] >> 16) & 0xff) * 0x0101);
        expect( ((back[i] >> 48) & 0xffff) == ((dst[i] >> 24) & 0xff) * 0x0101);
    }

    free(src);
    free(dst);
    free(back);
}

static void test_FormatConversions_161616() {
    skcms_ICCProfile profile;

    // We'll test the same cases as the _16161616() test, as if they were 4 RGB pixels.
    uint16_t src[] = { 0x0000, 0x0001, 0x0002,
                       0x0003, 0x7efc, 0x7efd,
                       0x7efe, 0x7eff, 0xfffc,
                       0xfffd, 0xfffe, 0xffff };
    uint32_t dst[4];
    expect(skcms_Transform(dst, skcms_PixelFormat_RGBA_8888 , &profile,
                           src, skcms_PixelFormat_RGB_161616, &profile, 4));

    expect(dst[0] == 0xff020100);
    expect(dst[1] == 0xfffdfc03);
    expect(dst[2] == 0xfffcfefe);
    expect(dst[3] == 0xfffffefd);

    // We've lost precision when transforming to 8-bit, so these won't quite round-trip.
    // Instead we should see the most signficant (low) byte doubled, as 65535/255 = 257 = 0x0101.
    uint16_t back[12];
    expect(skcms_Transform(back, skcms_PixelFormat_RGB_161616, &profile,
                           dst , skcms_PixelFormat_RGBA_8888 , &profile, 4));
    for (int i = 0; i < 12; i++) {
        expect(back[0] == (src[0] & 0xff) * 0x0101);
    }
}

static void test_FormatConversions_101010() {
    skcms_ICCProfile profile;

    uint32_t src = (uint32_t)1023 <<  0    // 1.0.
                 | (uint32_t) 511 << 10    // About 1/2.
                 | (uint32_t)   4 << 20    // Smallest 10-bit channel that's non-zero in 8-bit.
                 | (uint32_t)   1 << 30;   // 1/3, smallest non-zero alpha.
    uint32_t dst;
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_8888   , &profile,
                           &src, skcms_PixelFormat_RGBA_1010102, &profile, 1));
    expect(dst == 0x55017fff);

    // Same as above, but we'll ignore the 1/3 alpha and fill in 1.0.
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_8888  , &profile,
                           &src, skcms_PixelFormat_RGB_101010x, &profile, 1));
    expect(dst == 0xff017fff);

    // Converting 101010x <-> 1010102 will force opaque in either direction.
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGB_101010x , &profile,
                           &src, skcms_PixelFormat_RGBA_1010102, &profile, 1));
    expect(dst == ( (uint32_t)1023 <<  0
                  | (uint32_t) 511 << 10
                  | (uint32_t)   4 << 20
                  | (uint32_t)   3 << 30));
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_1010102, &profile,
                           &src, skcms_PixelFormat_RGB_101010x , &profile, 1));
    expect(dst == ( (uint32_t)1023 <<  0
                  | (uint32_t) 511 << 10
                  | (uint32_t)   4 << 20
                  | (uint32_t)   3 << 30));
}

static void test_FormatConversions_half() {
    skcms_ICCProfile profile;

    uint16_t src[] = {
        0x3c00,  // 1.0
        0x3800,  // 0.5
        0x1805,  // Should round up to 0x01
        0x1804,  // Should round down to 0x00
        0x4000,  // 2.0
        0x03ff,  // A denorm, may be flushed to zero.
        0x83ff,  // A negative denorm, may be flushed to zero.
        0xbc00,  // -1.0
    };

    uint32_t dst[2];
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_8888, &profile,
                           &src, skcms_PixelFormat_RGBA_hhhh, &profile, 2));
    expect(dst[0] == 0x000180ff);
    expect(dst[1] == 0x000000ff);  // Notice we've clamped 2.0 to 0xff and -1.0 to 0x00.

    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_8888, &profile,
                           &src, skcms_PixelFormat_RGB_hhh,   &profile, 2));
    expect(dst[0] == 0xff0180ff);
    expect(dst[1] == 0xff00ff00);  // Remember, this corresponds to src[3-5].

    float fdst[8];
    expect(skcms_Transform(&fdst, skcms_PixelFormat_RGBA_ffff, &profile,
                            &src, skcms_PixelFormat_RGBA_hhhh, &profile, 2));
    expect_eq(fdst[0],  1.0f);
    expect_eq(fdst[1],  0.5f);
    expect(fdst[2] > 1/510.0f);
    expect(fdst[3] < 1/510.0f);
    expect_eq(fdst[4],  2.0f);
    expect_eq2(fdst[5], +0.00006097555f, 0.0f);  // may have been flushed to zero
    expect_eq2(fdst[6], -0.00006097555f, 0.0f);
    expect_eq(fdst[7], -1.0f);

    // Now convert back, first to RGBA halfs, then RGB halfs.
    uint16_t back[8];
    expect(skcms_Transform(&back, skcms_PixelFormat_RGBA_hhhh, &profile,
                           &fdst, skcms_PixelFormat_RGBA_ffff, &profile, 2));
    expect_eq (back[0], src[0]);
    expect_eq (back[1], src[1]);
    expect_eq (back[2], src[2]);
    expect_eq (back[3], src[3]);
    expect_eq (back[4], src[4]);
    expect_eq2(back[5], src[5], 0x0000);
    expect_eq2(back[6], src[6], 0x0000);
    expect_eq (back[7], src[7]);

    expect(skcms_Transform(&back, skcms_PixelFormat_RGB_hhh  , &profile,
                           &fdst, skcms_PixelFormat_RGBA_ffff, &profile, 2));
    expect_eq (back[0], src[0]);
    expect_eq (back[1], src[1]);
    expect_eq (back[2], src[2]);
    expect_eq (back[3], src[4]);
    expect_eq2(back[4], src[5], 0x0000);
    expect_eq2(back[5], src[6], 0x0000);
}

static void test_FormatConversions_float() {
    skcms_ICCProfile profile;

    float src[] = { 1.0f, 0.5f, 1/255.0f, 1/512.0f };

    uint32_t dst;
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_8888, &profile,
                           &src, skcms_PixelFormat_RGBA_ffff, &profile, 1));
    expect(dst == 0x000180ff);

    // Same as above, but we'll ignore the 1/512 alpha and fill in 1.0.
    expect(skcms_Transform(&dst, skcms_PixelFormat_RGBA_8888, &profile,
                           &src, skcms_PixelFormat_RGB_fff,   &profile, 1));
    expect(dst == 0xff0180ff);

    // Let's make sure each byte converts to the float we expect.
    uint32_t bytes[64];
    float   fdst[4*64];
    for (int i = 0; i < 64; i++) {
        bytes[i] = 0x03020100 + 0x04040404 * (uint32_t)i;
    }
    expect(skcms_Transform(&fdst, skcms_PixelFormat_RGBA_ffff, &profile,
                          &bytes, skcms_PixelFormat_RGBA_8888, &profile, 64));
    for (int i = 0; i < 256; i++) {
        expect_eq(fdst[i], i*(1/255.0f));
    }

    float ffff[16] = { 0,1,2,3, 4,5,6,7, 8,9,10,11, 12,13,14,15 };
    float  fff[12] = { 0,0,0, 0,0,0, 0,0,0, 0,0,0};
    expect(skcms_Transform(fff , skcms_PixelFormat_RGB_fff  , &profile,
                           ffff, skcms_PixelFormat_RGBA_ffff, &profile, 1));
    expect_eq(fff[0],  0); expect_eq(fff[ 1],  1); expect_eq(fff[ 2],  2);

    expect(skcms_Transform(fff , skcms_PixelFormat_RGB_fff  , &profile,
                           ffff, skcms_PixelFormat_RGBA_ffff, &profile, 4));
    expect_eq(fff[0],  0); expect_eq(fff[ 1],  1); expect_eq(fff[ 2],  2);
    expect_eq(fff[3],  4); expect_eq(fff[ 4],  5); expect_eq(fff[ 5],  6);
    expect_eq(fff[6],  8); expect_eq(fff[ 7],  9); expect_eq(fff[ 8], 10);
    expect_eq(fff[9], 12); expect_eq(fff[10], 13); expect_eq(fff[11], 14);
}

static const skcms_TransferFunction srgb_transfer_fn =
    { 2.4f, 1 / 1.055f, 0.055f / 1.055f, 1 / 12.92f, 0.04045f, 0.0f, 0.0f };
static const skcms_TransferFunction gamma_2_2_transfer_fn =
    { 2.2f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

static const skcms_Matrix3x3 srgb_to_xyz  = { { { 0.4360657f, 0.3851471f, 0.1430664f },
                                                { 0.2224884f, 0.7168732f, 0.0606079f },
                                                { 0.0139160f, 0.0970764f, 0.7140961f } } };

static const skcms_Matrix3x3 sgbr_to_xyz  = { { { 0.3851471f, 0.1430664f, 0.4360657f },
                                                { 0.7168732f, 0.0606079f, 0.2224884f },
                                                { 0.0970764f, 0.7140961f, 0.0139160f } } };

static const skcms_Matrix3x3 adobe_to_xyz = { { { 0.6097412f, 0.2052765f, 0.1491852f },
                                                { 0.3111115f, 0.6256714f, 0.0632172f },
                                                { 0.0194702f, 0.0608673f, 0.7445679f } } };

static const skcms_Matrix3x3 p3_to_xyz    = { { { 0.5151215f, 0.2919769f, 0.1571045f },
                                                { 0.2411957f, 0.6922455f, 0.0665741f },
                                                { -0.0010376f, 0.0418854f, 0.7840729f } } };

typedef enum {
    kInvalid,           // Too new (iccMAX) or otherwise not parsable
    kA2B,               // No TRC or XYZ, just A2B/B2A tags
    kLUT_XYZ,           // LUT based TRC, XYZ tags
    kParametric_XYZ,    // Parametric TRC curves, and XYZ tags
} ProfileType;

typedef struct {
    const char*                   filename;
    ProfileType                   type;
    const skcms_TransferFunction* expect_tf;
    const skcms_Matrix3x3*        expect_xyz;
} ProfileTestCase;

static const ProfileTestCase profile_test_cases[] = {
    // iccMAX profiles that we can't parse at all
    { "profiles/color.org/sRGB_D65_colorimetric.icc",  kInvalid, NULL, NULL },
    { "profiles/color.org/sRGB_D65_MAT.icc",           kInvalid, NULL, NULL },
    { "profiles/color.org/sRGB_ISO22028.icc",          kInvalid, NULL, NULL },

    // V4 profiles that only include A2B/B2A tags (no TRC or XYZ)
    { "profiles/color.org/sRGB_ICC_v4_Appearance.icc", kA2B, NULL, NULL },
    { "profiles/color.org/sRGB_v4_ICC_preference.icc", kA2B, NULL, NULL },
    { "profiles/color.org/Upper_Left.icc",             kA2B, NULL, NULL },
    { "profiles/color.org/Upper_Right.icc",            kA2B, NULL, NULL },

    // V4 profiles with parametric TRC curves and XYZ
    { "profiles/mobile/Display_P3_parametric.icc",     kParametric_XYZ, &srgb_transfer_fn, &p3_to_xyz },
    { "profiles/mobile/sRGB_parametric.icc",           kParametric_XYZ, &srgb_transfer_fn, &srgb_to_xyz },
    { "profiles/mobile/iPhone7p.icc",                  kParametric_XYZ, &srgb_transfer_fn, &p3_to_xyz },

    // V4 profiles with LUT TRC curves and XYZ
    { "profiles/mobile/Display_P3_LUT.icc",            kLUT_XYZ, &srgb_transfer_fn, &p3_to_xyz },
    { "profiles/mobile/sRGB_LUT.icc",                  kLUT_XYZ, &srgb_transfer_fn, &srgb_to_xyz },

    // V2 profiles with gamma TRC and XYZ
    { "profiles/color.org/Lower_Left.icc",             kParametric_XYZ, &gamma_2_2_transfer_fn, &sgbr_to_xyz },
    { "profiles/color.org/Lower_Right.icc",            kParametric_XYZ, &gamma_2_2_transfer_fn, &adobe_to_xyz },

    // V2 profiles with LUT TRC and XYZ
    { "profiles/color.org/sRGB2014.icc",               kLUT_XYZ, &srgb_transfer_fn, &srgb_to_xyz },
    { "profiles/sRGB_Facebook.icc",                    kLUT_XYZ, &srgb_transfer_fn, &srgb_to_xyz },
};

static void load_file(const char* filename, void** buf, size_t* len) {
    FILE* fp = fopen(filename, "rb");
    expect(fp);

    expect(fseek(fp, 0L, SEEK_END) == 0);
    long size = ftell(fp);
    expect(size > 0);
    *len = (size_t)size;
    rewind(fp);

    *buf = malloc(*len);
    expect(*buf);

    size_t bytes_read = fread(*buf, 1, *len, fp);
    expect(bytes_read == *len);
}

static void check_transfer_function(skcms_TransferFunction fn_a,
                                    skcms_TransferFunction fn_b,
                                    float tol) {
    expect(fabsf(fn_a.g - fn_b.g) < tol);
    expect(fabsf(fn_a.a - fn_b.a) < tol);
    expect(fabsf(fn_a.b - fn_b.b) < tol);
    expect(fabsf(fn_a.c - fn_b.c) < tol);
    expect(fabsf(fn_a.d - fn_b.d) < tol);
    expect(fabsf(fn_a.e - fn_b.e) < tol);
    expect(fabsf(fn_a.f - fn_b.f) < tol);
}

static void check_roundtrip_transfer_functions(const skcms_TransferFunction* fwd,
                                               const skcms_TransferFunction* rev,
                                               float tol) {
    for (int i = 0; i < 256; ++i) {
        float t = i / 255.0f;
        float x = skcms_TransferFunction_eval(rev, skcms_TransferFunction_eval(fwd, t));
        expect((int)(x * 255.0f + 0.5f) == i);
        expect(fabsf(x - t) < tol);
    }
}

static void invert_tf(const skcms_TransferFunction* src, skcms_TransferFunction* dst) {
    skcms_TransferFunction fn_inv = { 0, 0, 0, 0, 0, 0, 0 };
    if (src->a > 0 && src->g > 0) {
        double a_to_the_g = pow((double)src->a, (double)src->g);
        fn_inv.a = 1.f / (float)a_to_the_g;
        fn_inv.b = -src->e / (float)a_to_the_g;
        fn_inv.g = 1.f / src->g;
    }
    fn_inv.d = src->c * src->d + src->f;
    fn_inv.e = -src->b / src->a;
    if (fabsf(src->c) > 0) {
        fn_inv.c = 1.f / src->c;
        fn_inv.f = -src->f / src->c;
    }
    *dst = fn_inv;
}

static void test_ICCProfile_parse() {
    const int test_cases_count = sizeof(profile_test_cases) / sizeof(profile_test_cases[0]);
    for (int i = 0; i < test_cases_count; ++i) {
        const ProfileTestCase* test = profile_test_cases + i;

        // Make sure the test parameters are internally consistent
        switch (test->type) {
            case kInvalid:
            case kA2B:
                expect(!test->expect_tf && !test->expect_xyz);
                break;
            case kLUT_XYZ:
            case kParametric_XYZ:
                expect(test->expect_tf && test->expect_xyz);
                break;
        }

        void* buf = NULL;
        size_t len = 0;
        load_file(test->filename, &buf, &len);
        skcms_ICCProfile profile;
        bool result = skcms_ICCProfile_parse(&profile, buf, len);
        expect(result == (test->type != kInvalid));

        if (!result) {
            free(buf);
            continue;
        }

        skcms_TransferFunction exact_tf;
        bool exact_tf_result = skcms_ICCProfile_getTransferFunction(&profile, &exact_tf);
        expect(exact_tf_result == (test->type == kParametric_XYZ));

        skcms_TransferFunction approx_tf;
        float max_error;
        bool approx_tf_result = skcms_ICCProfile_approximateTransferFunction(&profile, &approx_tf,
                                                                             &max_error);
        expect(approx_tf_result == (test->type == kLUT_XYZ));

        if (exact_tf_result) {
            // V2 'curv' gamma values are 8.8 fixed point, so the maximum error is the value we
            // use here: 0.5 / 256 (~= 0.002)
            // V4 'para' curves are 1.15.16 fixed point, and should be precise to 5 digits, but
            // vendors sometimes round strangely when writing values. Regardless, all of our test
            // profiles are within 0.001, except for the odd version of sRGB used in the iPhone
            // profile. It has a D value of .039 (2556 / 64k) rather than .04045 (2651 / 64k).
            check_transfer_function(*test->expect_tf, exact_tf, 0.5f / 256.0f);
        }

        if (approx_tf_result) {
            // Our approximate curves can vary pretty significantly from the reference curves,
            // so this needs to be fairly tolerant. The more important thing is how the overall
            // curve behaves - which is tested with the byte round-tripping below.
            check_transfer_function(*test->expect_tf, approx_tf, 0.01f);

            // For this check, run every byte value through the forward version of one TF, and
            // the inverse of the other, and make sure it round-trips (using both combinations).
            skcms_TransferFunction approx_inverse_tf;
            skcms_TransferFunction expect_inverse_tf;
            invert_tf(&approx_tf, &approx_inverse_tf);
            invert_tf(test->expect_tf, &expect_inverse_tf);

            // This function verifies that all bytes round-trip perfectly, but also takes a
            // tolerance to further limit the error after round-trip. We can currently get this
            // within ~30% of a byte (0.0011).
            check_roundtrip_transfer_functions(&approx_tf, &expect_inverse_tf, 0.02f);
            check_roundtrip_transfer_functions(test->expect_tf, &approx_inverse_tf, 0.02f);
        }

        skcms_Matrix3x3 toXYZ;
        bool xyz_result = skcms_ICCProfile_toXYZD50(&profile, &toXYZ);
        expect(xyz_result == !!test->expect_xyz);

        if (xyz_result) {
            // XYZ values are 1.15.16 fixed point, but the precise values used by vendors vary
            // quite a bit, especially depending on their implementation of D50 adaptation.
            // This is still a pretty tight tolerance, and all of our test profiles pass.
            const float kXYZ_Tol = 0.0002f;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    expect(fabsf(toXYZ.vals[r][c] - test->expect_xyz->vals[r][c]) < kXYZ_Tol);
                }
            }
        }

        free(buf);
    }
}

int main(void) {
    test_ICCProfile();
    test_Transform();
    test_FormatConversions();
    test_FormatConversions_565();
    test_FormatConversions_16161616();
    test_FormatConversions_161616();
    test_FormatConversions_101010();
    test_FormatConversions_half();
    test_FormatConversions_float();
    test_ICCProfile_parse();
    return 0;
}
