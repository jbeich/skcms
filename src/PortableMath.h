/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#define skcms_INFINITY (3E38f * 2)

static inline float floorf_(float x) {
    float roundtrip = (float)((int)x);
    return roundtrip > x ? roundtrip - 1 : roundtrip;
}

static inline float fmaxf_(float x, float y) { return x > y ? x : y; }
static inline double fmaxd_(double x, double y) { return x > y ? x : y; }

static inline float fminf_(float x, float y) { return x < y ? x : y; }
static inline double fmind_(double x, double y) { return x < y ? x : y; }

static inline float fabsf_(float x) { return x < 0 ? -x : x; }
static inline double fabsd_(double x) { return x < 0 ? -x : x; }

float log2f_(float);
static inline double log2d_(double x) { return (double)log2f_((float)x); }

float exp2f_(float);
static inline double exp2d_(double x) { return (double)exp2f_((float)x); }

float powf_(float, float);
static inline double powd_(double x, double y) { return (double)powf_((float)x, (float)y); }

bool isfinitef_(float);
bool isfinited_(double);

float nextafterf_(float x);
