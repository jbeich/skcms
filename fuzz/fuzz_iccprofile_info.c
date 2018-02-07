/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// This fuzz target parses an ICCProfile and then queries several pieces
// of info from it.

#include "../skcms.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    skcms_ICCProfile p;
    if (!skcms_ICCProfile_parse(&p, data, size)) {
        return 0;
    }

    // These should always be safe to call if ICCProfile_parse() succeeds.

    skcms_Matrix3x3 m;
    (void)skcms_ICCProfile_toXYZD50(&p, &m);

    skcms_TransferFunction tf;
    (void)skcms_ICCProfile_getTransferFunction(&p, &tf);

    // Instead of testing all tags, just test that we can read the first and last.
    // Anything in the middle should work fine if those two do.
    if (p.tag_count > 0) {
        skcms_ICCTag tag;
        skcms_ICCProfile_getTagByIndex(&p,               0, &tag);
        skcms_ICCProfile_getTagByIndex(&p, p.tag_count - 1, &tag);
    }
    return 0;
}
