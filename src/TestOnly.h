/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "../skcms.h"
#include <stdio.h>

void dump_profile(const skcms_ICCProfile* profile, FILE* fp, bool for_unit_test);
