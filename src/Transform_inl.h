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

SI ATTR F NS(F_from_U8_)(U8 v) {
    return CAST(F, v) * (1/255.0f);
}
#define F_from_U8 NS(F_from_U8_)

SI ATTR F NS(F_from_U16_BE_)(U16 v) {
    // All 16-bit ICC values are big-endian, so we byte swap before converting to float.
    // MSVC catches the "loss" of data here in the portable path, so we also make sure to mask.
    v = (U16)( ((v<<8)|(v>>8)) & 0xffff );
    return CAST(F, v) * (1/65535.0f);
}
#define F_from_U16_BE NS(F_from_U16_BE_)

SI ATTR F NS(minus_1_ulp_)(F v) {
    I32 bits;
    small_memcpy(&bits, &v, sizeof(bits));
    bits = bits - 1;
    small_memcpy(&v, &bits, sizeof(bits));
    return v;
}
#define minus_1_ulp NS(minus_1_ulp_)

SI ATTR F NS(table_8_)(const skcms_Curve* curve, F v) {
    // Clamp the input to [0,1], then scale to a table index.
    F ix = max_(F0, min_(v, F1)) * (float)(curve->table_entries - 1);

    // We'll look up (equal or adjacent) entries at lo and hi, then lerp by t between the two.
    I32 lo = CAST(I32,             ix      ),
        hi = CAST(I32, minus_1_ulp(ix+1.0f));
    F t = ix - CAST(F, lo);  // i.e. the fractional part of ix.

    // TODO: can we load l and h simultaneously?  Each entry in 'h' is either
    // the same as in 'l' or adjacent.  We have a rough idea that's it'd always be safe
    // to read adjacent entries and perhaps underflow the table by a byte or two
    // (it'd be junk, but always safe to read).  Not sure how to lerp yet.
    F l = F_from_U8(gather_8(curve->table_8, lo)),
      h = F_from_U8(gather_8(curve->table_8, hi));
    return l + (h-l)*t;
}

SI ATTR F NS(table_16_)(const skcms_Curve* curve, F v) {
    // All just as in table_8() until the gathers.
    F ix = max_(F0, min_(v, F1)) * (float)(curve->table_entries - 1);

    I32 lo = CAST(I32,             ix      ),
        hi = CAST(I32, minus_1_ulp(ix+1.0f));
    F t = ix - CAST(F, lo);

    // TODO: as above, load l and h simultaneously?
    // Here we could even use AVX2-style 32-bit gathers.
    F l = F_from_U16_BE(gather_16(curve->table_16, lo)),
      h = F_from_U16_BE(gather_16(curve->table_16, hi));
    return l + (h-l)*t;
}

// Color lookup tables, by input dimension and bit depth.
SI ATTR void NS(clut_0_8_)(const skcms_A2B* a2b, I32 ix, I32 stride, F* r, F* g, F* b, F a) {
    U32 rgb = gather_24(a2b->grid_8, ix);

    *r = CAST(F, (rgb >>  0) & 0xff) * (1/255.0f);
    *g = CAST(F, (rgb >>  8) & 0xff) * (1/255.0f);
    *b = CAST(F, (rgb >> 16) & 0xff) * (1/255.0f);

    (void)a;
    (void)stride;
}
SI ATTR void NS(clut_0_16_)(const skcms_A2B* a2b, I32 ix, I32 stride, F* r, F* g, F* b, F a) {
    #if defined(__arm__)
        // This is up to 2x faster on 32-bit ARM than the #else-case fast path.
        *r = F_from_U16_BE(gather_16(a2b->grid_16, 3*ix+0));
        *g = F_from_U16_BE(gather_16(a2b->grid_16, 3*ix+1));
        *b = F_from_U16_BE(gather_16(a2b->grid_16, 3*ix+2));
    #else
        // This strategy is much faster for 64-bit builds, and fine for 32-bit x86 too.
        U64 rgb;
        gather_48(a2b->grid_16, ix, &rgb);
        swap_endian_16x4(&rgb);

        *r = CAST(F, (rgb >>  0) & 0xffff) * (1/65535.0f);
        *g = CAST(F, (rgb >> 16) & 0xffff) * (1/65535.0f);
        *b = CAST(F, (rgb >> 32) & 0xffff) * (1/65535.0f);
    #endif
    (void)a;
    (void)stride;
}

// __attribute__((always_inline)) hits some pathological case in GCC that makes
// compilation way too slow for my patience.
#if defined(__clang__)
    #define MAYBE_SI SI
#else
    #define MAYBE_SI static inline
#endif

// These are all the same basic approach: handle one dimension, then the rest recursively.
// We let "I" be the current dimension, and "J" the previous dimension, I-1.  "B" is the bit depth.
#define DEF_CLUT(I,J,B)                                                                           \
    MAYBE_SI ATTR                                                                                 \
    void NS(clut_##I##_##B##_)(const skcms_A2B* a2b, I32 ix, I32 stride, F* r, F* g, F* b, F a) { \
        I32 limit = CAST(I32, F0);                                                                \
        limit += a2b->grid_points[I-1];                                                           \
                                                                                                  \
        const F* srcs[] = { r,g,b,&a };                                                           \
        F src = *srcs[I-1];                                                                       \
                                                                                                  \
        F x = max_(F0, min_(src, F1)) * CAST(F, limit - 1);                                       \
                                                                                                  \
        I32 lo = CAST(I32,             x      ),                                                  \
            hi = CAST(I32, minus_1_ulp(x+1.0f));                                                  \
        F lr = *r, lg = *g, lb = *b,                                                              \
          hr = *r, hg = *g, hb = *b;                                                              \
        NS(clut_##J##_##B##_)(a2b, stride*lo + ix, stride*limit, &lr,&lg,&lb,a);                  \
        NS(clut_##J##_##B##_)(a2b, stride*hi + ix, stride*limit, &hr,&hg,&hb,a);                  \
                                                                                                  \
        F t = x - CAST(F, lo);                                                                    \
        *r = lr + (hr-lr)*t;                                                                      \
        *g = lg + (hg-lg)*t;                                                                      \
        *b = lb + (hb-lb)*t;                                                                      \
    }

DEF_CLUT(1,0,8)
DEF_CLUT(2,1,8)
DEF_CLUT(3,2,8)
DEF_CLUT(4,3,8)

DEF_CLUT(1,0,16)
DEF_CLUT(2,1,16)
DEF_CLUT(3,2,16)
DEF_CLUT(4,3,16)
