/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

bool skcms_ICCProfile_parse(skcms_ICCProfile* profile,
                            const void* buf,
                            size_t len) {
    (void)profile;
    (void)buf;
    (void)len;
    return false;
}

bool skcms_ICCProfile_toXYZD50(const skcms_ICCProfile* profile,
                               skcms_Matrix3x3* toXYZD50) {
    (void)profile;
    (void)toXYZD50;
    return false;
}

bool skcms_ICCProfile_getTransferFunction(const skcms_ICCProfile* profile,
                                          skcms_TransferFunction* transferFunction) {
    (void)profile;
    (void)transferFunction;
    return false;
}
