/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "PortableMath.h"
#include <limits.h>
#include <string.h>

#if defined(__clang__) || defined(__GNUC__)
    #define small_memcpy __builtin_memcpy
#else
    #define small_memcpy memcpy
#endif

#if defined(__SSE__)
    #include <immintrin.h>
#endif

float log2f_(float x) {
    // The first approximation of log2(x) is its exponent 'e', minus 127.
    int32_t bits;
    small_memcpy(&bits, &x, sizeof(bits));

    float e = (float)bits * (1.0f / (1<<23));

    // If we use the mantissa too we can refine the error signficantly.
    int32_t m_bits = (bits & 0x007fffff) | 0x3f000000;
    float m;
    small_memcpy(&m, &m_bits, sizeof(m));

    return (e - 124.225514990f
              -   1.498030302f*m
              -   1.725879990f/(0.3520887068f + m));
}

float exp2f_(float x) {
    float fract = x - floorf_(x);

    float fbits = (1.0f * (1<<23)) * (x + 121.274057500f
                                        -   1.490129070f*fract
                                        +  27.728023300f/(4.84252568f - fract));
    if (fbits > INT_MAX) {
        return INFINITY_;
    } else if (fbits < INT_MIN) {
        return -INFINITY_;
    }
    int32_t bits = (int32_t)fbits;
    small_memcpy(&x, &bits, sizeof(x));
    return x;
}

float powf_(float x, float y) {
    // Handling all the integral powers first increases our precision a little. If y is very large,
    // this loop may never terminate, but for any reasonably large y, the approximation is fine.
    float r = 1.0f;
    while (y >= 1.0f && y < 32) {
        r *= x;
        y -= 1.0f;
    }

    return (x == 0) || (x == 1) ? x : r * exp2f_(log2f_(x) * y);
}

bool isfinitef_(float x) {
    uint32_t bits;
    small_memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7f800000) != 0x7f800000;
}

void sqrt_ftrt_(float x, float* s, float* f) {
    // Hard guarantee that 0->0 and 1->1, no matter the platform.
    if (x == 0.0f || x == 1.0f) {
        *s = x;
        *f = x;
        return;
    }

#if 0 && defined(__SSE__)
    __m128 rsqrt = _mm_rsqrt_ss(_mm_set_ss(x));
    *s = _mm_cvtss_f32(_mm_rcp_ss  (rsqrt));
    *f = _mm_cvtss_f32(_mm_rsqrt_ss(rsqrt));
#elif 0 && defined(__ARM_NEON)
    // TODO
#else
    const int kSteps = 4;
    // We'll use Newton's Method to minimize F(s) = s^2 - x.
    // s' = s - F(s)/F'(s) = s - (s^2 - x)/2s.
    *s = x;
    for (int i = 0; i < kSteps; i++) {
        *s -= (*s * *s - x) / (2 * *s);
    }
    // Now the same to minimize F(f) = f^2 - s.
    *f = *s;
    for (int i = 0; i < kSteps; i++) {
        *f -= (*f * *f - *s) / (2 * *f);
    }
#endif
}
