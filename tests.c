/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms.h"
#include <stdio.h>
#include <stdlib.h>

#define expect(cond) \
    if (!cond) (fprintf(stderr, "expect(" #cond ") failed at %s:%d\n", __FILE__, __LINE__), exit(1))


static void test_ICCProfile() {
    // Nothing works yet.  :)
    skcms_ICCProfile profile;

    const char buf[] = { 0x42 };
    expect(!skcms_ICCProfile_parse(&profile, buf, sizeof(buf)));

    skcms_Matrix3x3 toXYZD50;
    expect(!skcms_ICCProfile_toXYZD50(&profile, &toXYZD50));

    skcms_TransferFunction transferFunction;
    expect(!skcms_ICCProfile_getTransferFunction(&profile, &transferFunction));
}

static void test_Transform() {
    // Nothing works yet.  :)
    skcms_ICCProfile src, dst;
    char buf[16];

    for (skcms_PixelFormat fmt  = skcms_PixelFormat_RGB_565;
                           fmt <= skcms_PixelFormat_BGRA_ffff; fmt++) {
        expect(!skcms_Transform(buf,fmt,&dst,
                                buf,fmt,&src, 1));
    }
}

int main(void) {
    test_ICCProfile();
    test_Transform();
    return 0;
}
