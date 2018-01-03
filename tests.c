/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms.h"
#include <assert.h>

static void test_ICCProfile() {
    // Nothing works yet.  :)
    skcms_ICCProfile profile;

    const char buf[] = { 0x42 };
    assert(!skcms_ICCProfile_parse(&profile, buf, sizeof(buf)));

    skcms_Matrix3x3 toXYZD50;
    assert(!skcms_ICCProfile_toXYZD50(&profile, &toXYZD50));

    skcms_TransferFunction transferFunction;
    assert(!skcms_ICCProfile_getTransferFunction(&profile, &transferFunction));
}

int main(void) {
    test_ICCProfile();
    return 0;
}
