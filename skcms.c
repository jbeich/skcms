/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms.h"
#include "src/skcms_internal.h"

 // skcms.c is a unity build target for skcms, #including every other C source file.

#include "src/ICCProfile.c"
#include "src/TransferFunction.c"
#include "src/Transform.c"
