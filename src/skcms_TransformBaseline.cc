/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms_public.h"     // NO_G3_REWRITE
#include "skcms_internals.h"  // NO_G3_REWRITE
#include "skcms_Transform.h"  // NO_G3_REWRITE
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ARM_NEON)
    #include <arm_neon.h>
#elif defined(__SSE__)
    #include <immintrin.h>

    #if defined(__clang__)
        // That #include <immintrin.h> is usually enough, but Clang's headers
        // avoid #including the whole kitchen sink when _MSC_VER is defined,
        // because lots of programs on Windows would include that and it'd be
        // a lot slower. But we want all those headers included, so we can use
        // their features (after making runtime checks).
        #include <smmintrin.h>
        #include <avxintrin.h>
        #include <avx2intrin.h>
        #include <avx512fintrin.h>
        #include <avx512dqintrin.h>
    #endif
#endif

using namespace skcms_private;

namespace skcms_private {
namespace baseline {

#if defined(SKCMS_PORTABLE) || !(defined(__clang__) || \
                                 defined(__GNUC__)) || \
                                 (defined(__EMSCRIPTEN_major__) && !defined(__wasm_simd128__))
    // Build skcms in a portable scalar configuration.
    #define N 1
    template <typename T> using V = T;
    using Color = float;
#elif defined(__AVX512F__) && defined(__AVX512DQ__)
    // Build skcms with AVX512 (Skylake) support.
    #define N 16
    template <typename T> using V = skcms_private::Vec<N,T>;
    using Color = float;
#elif defined(__AVX__)
    // Build skcms with AVX (Haswell) support.
    #define N 8
    template <typename T> using V = skcms_private::Vec<N,T>;
    using Color = float;
#else
    // Build skcms with basic SSE support.
    #define N 4
    template <typename T> using V = skcms_private::Vec<N,T>;
    using Color = float;
#endif

#include "Transform_inl.h"

}  // namespace baseline
}  // namespace skcms_private
