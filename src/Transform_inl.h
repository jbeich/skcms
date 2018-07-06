/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// Intentionally NO #pragma once... included multiple times.

// This file is included from skcms.c with some values and types pre-defined:
//    N:    depth of all vectors, 1,4,8, or 16
//
//    F:    a vector of N float
//    I32:  a vector of N int32_t
//    U64:  a vector of N uint64_t
//    U32:  a vector of N uint32_t
//    U16:  a vector of N uint16_t
//    U8:   a vector of N uint8_t
//
//    F0: a vector of N floats set to zero
//    F1: a vector of N floats set to one
//
//    NS(id): a macro that returns unique identifiers
//    ATTR:   an __attribute__ to apply to functions

#if defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
    // TODO(mtklein): this build supports FP16 compute
#endif

#if N == 4 && defined(__ARM_NEON)
    #define USING_NEON
    #if __ARM_FP & 2
        #define USING_NEON_F16C
    #endif
#elif N == 8 && defined(__AVX__)
    #if defined(__F16C__)
        #define USING_AVX_F16C
    #endif
#endif

// Swap high and low bytes of 16-bit lanes, converting between big-endian and little-endian.
#if defined(USING_NEON)
    SI ATTR U16 NS(swap_endian_16_)(U16 v) {
        return (U16)vrev16_u8((uint8x8_t) v);
    }
    #define swap_endian_16 NS(swap_endian_16_)
#endif

SI ATTR U8 NS(gather_8_)(const uint8_t* p, I32 ix) {
#if N == 1
    U8 v = p[ix];
#elif N == 4
    U8 v = { p[ix[0]], p[ix[1]], p[ix[2]], p[ix[3]] };
#elif N == 8
    U8 v = { p[ix[0]], p[ix[1]], p[ix[2]], p[ix[3]],
             p[ix[4]], p[ix[5]], p[ix[6]], p[ix[7]] };
#elif N == 16
    U8 v = { p[ix[ 0]], p[ix[ 1]], p[ix[ 2]], p[ix[ 3]],
             p[ix[ 4]], p[ix[ 5]], p[ix[ 6]], p[ix[ 7]],
             p[ix[ 8]], p[ix[ 9]], p[ix[10]], p[ix[11]],
             p[ix[12]], p[ix[13]], p[ix[14]], p[ix[15]] };
#endif
    return v;
}
#define gather_8 NS(gather_8_)

// Helper for gather_16(), loading the ix'th 16-bit value from p.
SI ATTR uint16_t NS(load_16_)(const uint8_t* p, int ix) {
    uint16_t v;
    small_memcpy(&v, p + 2*ix, 2);
    return v;
}
#define load_16 NS(load_16_)

SI ATTR U16 NS(gather_16_)(const uint8_t* p, I32 ix) {
#if N == 1
    U16 v = load_16(p,ix);
#elif N == 4
    U16 v = { load_16(p,ix[0]), load_16(p,ix[1]), load_16(p,ix[2]), load_16(p,ix[3]) };
#elif N == 8
    U16 v = { load_16(p,ix[0]), load_16(p,ix[1]), load_16(p,ix[2]), load_16(p,ix[3]),
              load_16(p,ix[4]), load_16(p,ix[5]), load_16(p,ix[6]), load_16(p,ix[7]) };
#elif N == 16
    U16 v = { load_16(p,ix[ 0]), load_16(p,ix[ 1]), load_16(p,ix[ 2]), load_16(p,ix[ 3]),
              load_16(p,ix[ 4]), load_16(p,ix[ 5]), load_16(p,ix[ 6]), load_16(p,ix[ 7]),
              load_16(p,ix[ 8]), load_16(p,ix[ 9]), load_16(p,ix[10]), load_16(p,ix[11]),
              load_16(p,ix[12]), load_16(p,ix[13]), load_16(p,ix[14]), load_16(p,ix[15]) };
#endif
    return v;
}
#define gather_16 NS(gather_16_)

#if !defined(__AVX2__)
    // Helpers for gather_24/48(), loading the ix'th 24/48-bit value from p, and 1/2 extra bytes.
    SI ATTR uint32_t NS(load_24_32_)(const uint8_t* p, int ix) {
        uint32_t v;
        small_memcpy(&v, p + 3*ix, 4);
        return v;
    }
    SI ATTR uint64_t NS(load_48_64_)(const uint8_t* p, int ix) {
        uint64_t v;
        small_memcpy(&v, p + 6*ix, 8);
        return v;
    }
    #define load_24_32 NS(load_24_32_)
    #define load_48_64 NS(load_48_64_)
#endif

SI ATTR U32 NS(gather_24_)(const uint8_t* p, I32 ix) {
    // First, back up a byte.  Any place we're gathering from has a safe junk byte to read
    // in front of it, either a previous table value, or some tag metadata.
    p -= 1;

    // Now load multiples of 4 bytes (a junk byte, then r,g,b).
#if N == 1
    U32 v = load_24_32(p,ix);
#elif N == 4
    U32 v = { load_24_32(p,ix[0]), load_24_32(p,ix[1]), load_24_32(p,ix[2]), load_24_32(p,ix[3]) };
#elif N == 8 && !defined(__AVX2__)
    U32 v = { load_24_32(p,ix[0]), load_24_32(p,ix[1]), load_24_32(p,ix[2]), load_24_32(p,ix[3]),
              load_24_32(p,ix[4]), load_24_32(p,ix[5]), load_24_32(p,ix[6]), load_24_32(p,ix[7]) };
#elif N == 8
    // The gather instruction here doesn't need any particular alignment,
    // but the intrinsic takes a const int*.
    const int* p4;
    small_memcpy(&p4, &p, sizeof(p4));
    I32 zero = { 0, 0, 0, 0,  0, 0, 0, 0},
        mask = {-1,-1,-1,-1, -1,-1,-1,-1};
    #if defined(__clang__)
        U32 v = (U32)__builtin_ia32_gatherd_d256(zero, p4, 3*ix, mask, 1);
    #elif defined(__GNUC__)
        U32 v = (U32)__builtin_ia32_gathersiv8si(zero, p4, 3*ix, mask, 1);
    #endif
#elif N == 16
    // The intrinsic is supposed to take const void* now, but it takes const int*, just like AVX2.
    // And AVX-512 swapped the order of arguments.  :/
    const int* p4;
    small_memcpy(&p4, &p, sizeof(p4));
    U32 v = (U32)_mm512_i32gather_epi32((__m512i)(3*ix), p4, 1);
#endif

    // Shift off the junk byte, leaving r,g,b in low 24 bits (and zero in the top 8).
    return v >> 8;
}
#define gather_24 NS(gather_24_)

#if !defined(__arm__)
    SI ATTR void NS(gather_48_)(const uint8_t* p, I32 ix, U64* v) {
        // As in gather_24(), with everything doubled.
        p -= 2;

    #if N == 1
        *v = load_48_64(p,ix);
    #elif N == 4
        *v = U64{
            load_48_64(p,ix[0]), load_48_64(p,ix[1]), load_48_64(p,ix[2]), load_48_64(p,ix[3]),
        };
    #elif N == 8 && !defined(__AVX2__)
        *v = U64{
            load_48_64(p,ix[0]), load_48_64(p,ix[1]), load_48_64(p,ix[2]), load_48_64(p,ix[3]),
            load_48_64(p,ix[4]), load_48_64(p,ix[5]), load_48_64(p,ix[6]), load_48_64(p,ix[7]),
        };
    #elif N == 8
        typedef int32_t   __attribute__((vector_size(16))) Half_I32;
        typedef long long __attribute__((vector_size(32))) Half_I64;

        // The gather instruction here doesn't need any particular alignment,
        // but the intrinsic takes a const long long*.
        const long long int* p8;
        small_memcpy(&p8, &p, sizeof(p8));

        Half_I64 zero = { 0, 0, 0, 0},
                 mask = {-1,-1,-1,-1};

        ix *= 6;
        Half_I32 ix_lo = { ix[0], ix[1], ix[2], ix[3] },
                 ix_hi = { ix[4], ix[5], ix[6], ix[7] };

        #if defined(__clang__)
            Half_I64 lo = (Half_I64)__builtin_ia32_gatherd_q256(zero, p8, ix_lo, mask, 1),
                     hi = (Half_I64)__builtin_ia32_gatherd_q256(zero, p8, ix_hi, mask, 1);
        #elif defined(__GNUC__)
            Half_I64 lo = (Half_I64)__builtin_ia32_gathersiv4di(zero, p8, ix_lo, mask, 1),
                     hi = (Half_I64)__builtin_ia32_gathersiv4di(zero, p8, ix_hi, mask, 1);
        #endif
        small_memcpy((char*)v +  0, &lo, 32);
        small_memcpy((char*)v + 32, &hi, 32);
    #elif N == 16
        const long long int* p8;
        small_memcpy(&p8, &p, sizeof(p8));
        __m512i lo = _mm512_i32gather_epi64(_mm512_extracti32x8_epi32((__m512i)(6*ix), 0), p8, 1),
                hi = _mm512_i32gather_epi64(_mm512_extracti32x8_epi32((__m512i)(6*ix), 1), p8, 1);
        small_memcpy((char*)v +  0, &lo, 64);
        small_memcpy((char*)v + 64, &hi, 64);
    #endif

        *v >>= 16;
    }
    #define gather_48 NS(gather_48_)
#endif

