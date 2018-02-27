/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <stdio.h>

void parse_and_dump_profile(const void* buf, size_t size, FILE* fp, bool limit_approx_precision);
