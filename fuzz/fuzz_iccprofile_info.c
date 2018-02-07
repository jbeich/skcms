/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// This fuzz target parses an ICCProfile and then queries several pieces
// of info from it.

#include "../skcms.h"
#include "fuzz.h"

DEF_FUZZ_MAIN(data, size)
    skcms_ICCProfile p;
    if (!skcms_ICCProfile_parse(&p, data, size)) {
        return 0;
    }
    skcms_Matrix3x3 m;
    skcms_ICCProfile_toXYZD50(&p, &m);
    skcms_TransferFunction tf;
    skcms_ICCProfile_getTransferFunction(&p, &tf);

    if (p.tag_count > 0) {
        skcms_ICCTag tag;
        skcms_ICCProfile_getTagByIndex(&p, 0, &tag);
        skcms_ICCProfile_getTagByIndex(&p, p.tag_count - 1, &tag);
    }
    return 0;
}
