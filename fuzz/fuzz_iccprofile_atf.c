/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// This fuzz target parses an ICCProfile and then computes the
// approximateTransferFunction.  This is separate from fuzz_iccprofile_info
// because it is a much more time-consuming function call.

#include "../skcms.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    skcms_ICCProfile p;
    if (!skcms_ICCProfile_parse(&p, data, size)) {
        return 0;
    }

    skcms_TransferFunction tf;
    float max_error;
    (void)skcms_ICCProfile_approximateTransferFunction(&p, &tf, &max_error);
    (void)max_error;

    return 0;
}
