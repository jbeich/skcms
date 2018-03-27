/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "LinearAlgebra.h"
#include "TransferFunction.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON)
    #include <arm_neon.h>
#elif defined(__SSE__)
    #include <immintrin.h>
#endif

#if defined(SKCMS_PORTABLE) || (!defined(__clang__) && !defined(__GNUC__))
    #define N 1
    typedef float    F  ;
    typedef int32_t  I32;
    typedef uint64_t U64;
    typedef uint32_t U32;
    typedef uint16_t U16;
    typedef uint8_t  U8 ;
    static const F F0 = 0,
                   F1 = 1;
#elif defined(__clang__) && defined(__AVX512F__)
    #define N 16
    typedef float    __attribute__((ext_vector_type(N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(N))) I32;
    typedef uint64_t __attribute__((ext_vector_type(N))) U64;
    typedef uint32_t __attribute__((ext_vector_type(N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(N))) U16;
    typedef uint8_t  __attribute__((ext_vector_type(N))) U8 ;
    static const F F0 = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};
#elif defined(__GNUC__) && defined(__AVX512F__)
    #define N 16
    typedef float    __attribute__((vector_size( 64))) F  ;
    typedef int32_t  __attribute__((vector_size( 64))) I32;
    typedef uint64_t __attribute__((vector_size(128))) U64;
    typedef uint32_t __attribute__((vector_size( 64))) U32;
    typedef uint16_t __attribute__((vector_size( 32))) U16;
    typedef uint8_t  __attribute__((vector_size( 16))) U8 ;
    static const F F0 = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};
#elif defined(__clang__) && defined(__AVX__)
    #define N 8
    typedef float    __attribute__((ext_vector_type(N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(N))) I32;
    typedef uint64_t __attribute__((ext_vector_type(N))) U64;
    typedef uint32_t __attribute__((ext_vector_type(N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(N))) U16;
    typedef uint8_t  __attribute__((ext_vector_type(N))) U8 ;
    static const F F0 = {0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1};
#elif defined(__GNUC__) && defined(__AVX__)
    #define N 8
    typedef float    __attribute__((vector_size(32))) F  ;
    typedef int32_t  __attribute__((vector_size(32))) I32;
    typedef uint64_t __attribute__((vector_size(64))) U64;
    typedef uint32_t __attribute__((vector_size(32))) U32;
    typedef uint16_t __attribute__((vector_size(16))) U16;
    typedef uint8_t  __attribute__((vector_size( 8))) U8 ;
    static const F F0 = {0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1};
#elif defined(__clang__)
    #define N 4
    typedef float    __attribute__((ext_vector_type(N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(N))) I32;
    typedef uint64_t __attribute__((ext_vector_type(N))) U64;
    typedef uint32_t __attribute__((ext_vector_type(N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(N))) U16;
    typedef uint8_t  __attribute__((ext_vector_type(N))) U8 ;
    static const F F0 = {0,0,0,0},
                   F1 = {1,1,1,1};
#elif defined(__GNUC__)
    #define N 4
    typedef float    __attribute__((vector_size(16))) F  ;
    typedef int32_t  __attribute__((vector_size(16))) I32;
    typedef uint64_t __attribute__((vector_size(32))) U64;
    typedef uint32_t __attribute__((vector_size(16))) U32;
    typedef uint16_t __attribute__((vector_size( 8))) U16;
    typedef uint8_t  __attribute__((vector_size( 4))) U8 ;
    static const F F0 = {0,0,0,0},
                   F1 = {1,1,1,1};
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

// It helps codegen to call __builtin_memcpy() when we know the byte count at compile time.
#if defined(__clang__) || defined(__GNUC__)
    #define small_memcpy __builtin_memcpy
#else
    #define small_memcpy memcpy
#endif

// We tag all non-stage helper functions as SI, to enforce good code generation
// but also work around what we think is a bug in GCC: when targeting 32-bit
// x86, GCC tends to pass U16 (4x uint16_t vector) function arguments in the
// MMX mm0 register, which seems to mess with unrelated code that later uses
// x87 FP instructions (MMX's mm0 is an alias for x87's st0 register).
//
// (Stage functions should be simply marked as static.)
#if defined(__clang__) || defined(__GNUC__)
    #define SI static inline __attribute__((always_inline))
#else
    #define SI static inline
#endif

// (T)v is a cast when N == 1 and a bit-pun when N>1, so we must use CAST(T, v) to actually cast.
#if N == 1
    #define CAST(T, v) (T)(v)
#elif defined(__clang__)
    #define CAST(T, v) __builtin_convertvector((v), T)
#elif N == 4
    #define CAST(T, v) (T){(v)[0],(v)[1],(v)[2],(v)[3]}
#elif N == 8
    #define CAST(T, v) (T){(v)[0],(v)[1],(v)[2],(v)[3], (v)[4],(v)[5],(v)[6],(v)[7]}
#elif N == 16
    #define CAST(T, v) (T){(v)[0],(v)[1],(v)[ 2],(v)[ 3], (v)[ 4],(v)[ 5],(v)[ 6],(v)[ 7], \
                           (v)[8],(v)[9],(v)[10],(v)[11], (v)[12],(v)[13],(v)[14],(v)[15]}
#endif

// When we convert from float to fixed point, it's very common to want to round,
// and for some reason compilers generate better code when converting to int32_t.
// To serve both those ends, we use this function to_fixed() instead of direct CASTs.
SI I32 to_fixed(F f) { return CAST(I32, f + 0.5f); }

// Comparisons result in bool when N == 1, in an I32 mask when N > 1.
// We've made this a macro so it can be type-generic...
// always (T) cast the result to the type you expect the result to be.
#if N == 1
    #define if_then_else(c,t,e) ( (c) ? (t) : (e) )
#else
    #define if_then_else(c,t,e) ( ((c) & (I32)(t)) | (~(c) & (I32)(e)) )
#endif

#if defined(USING_NEON_F16C)
    SI F F_from_Half(U16 half) { return vcvt_f32_f16(half); }
    SI U16 Half_from_F(F f)    { return vcvt_f16_f32(f   ); }
#elif defined(__AVX512F__)
    SI F F_from_Half(U16 half) { return (F)  _mm512_cvtph_ps((__m256i)half); }
    SI U16 Half_from_F(F f)    { return (U16)_mm512_cvtps_ph((__m512 )f,
                                                             _MM_FROUND_CUR_DIRECTION ); }
#elif defined(USING_AVX_F16C)
    SI F F_from_Half(U16 half) { return (F)  _mm256_cvtph_ps((__m128i)half); }
    SI U16 Half_from_F(F f)    { return (U16)_mm256_cvtps_ph((__m256 )f,
                                                             _MM_FROUND_CUR_DIRECTION ); }
#else
    SI F F_from_Half(U16 half) {
        U32 wide = CAST(U32, half);
        // A half is 1-5-10 sign-exponent-mantissa, with 15 exponent bias.
        U32 s  = wide & 0x8000,
            em = wide ^ s;

        // Constructing the float is easy if the half is not denormalized.
        U32 norm_bits = (s<<16) + (em<<13) + ((127-15)<<23);
        F norm;
        small_memcpy(&norm, &norm_bits, sizeof(norm));

        // Simply flush all denorm half floats to zero.
        return (F)if_then_else(em < 0x0400, F0, norm);
    }

    SI U16 Half_from_F(F f) {
        // A float is 1-8-23 sign-exponent-mantissa, with 127 exponent bias.
        U32 sem;
        small_memcpy(&sem, &f, sizeof(sem));

        U32 s  = sem & 0x80000000,
            em = sem ^ s;

        // For simplicity we flush denorm half floats (including all denorm floats) to zero.
        return CAST(U16, (U32)if_then_else(em < 0x38800000, (U32)F0
                                                          , (s>>16) + (em>>13) - ((127-15)<<10)));
    }
#endif

// Swap high and low bytes of 16-bit lanes, converting between big-endian and little-endian.
#if defined(USING_NEON)
    SI U16 swap_endian_16(U16 v) {
        return (U16)vrev16_u8((uint8x8_t) v);
    }
#else
    // Passing by U64* instead of U64 avoids ABI warnings.  It's all moot when inlined.
    SI void swap_endian_16x4(U64* rgba) {
        *rgba = (*rgba & 0x00ff00ff00ff00ff) << 8
              | (*rgba & 0xff00ff00ff00ff00) >> 8;
    }
#endif

#if defined(USING_NEON)
    SI F min_(F x, F y) { return vminq_f32(x,y); }
    SI F max_(F x, F y) { return vmaxq_f32(x,y); }
#else
    SI F min_(F x, F y) { return (F)if_then_else(x > y, y, x); }
    SI F max_(F x, F y) { return (F)if_then_else(x < y, y, x); }
#endif

SI F floor_(F x) {
#if N == 1
    return floorf(x);
#elif defined(__aarch64__)
    return vrndmq_f32(x);
#elif defined(__AVX512F__)
    return _mm512_floor_ps(x);
#elif defined(__AVX__)
    return _mm256_floor_ps(x);
#elif defined(__SSE4_1__)
    return _mm_floor_ps(x);
#else
    // Round trip through integers with a truncating cast.
    F roundtrip = CAST(F, CAST(I32, x));
    // If x is negative, truncating gives the ceiling instead of the floor.
    return roundtrip - (F)if_then_else(roundtrip > x, F1, F0);

    // This implementation fails for values of x that are outside
    // the range an integer can represent.  We expect most x to be small.
#endif
}

SI F approx_log2(F x) {
    // The first approximation of log2(x) is its exponent 'e', minus 127.
    I32 bits;
    small_memcpy(&bits, &x, sizeof(bits));

    F e = CAST(F, bits) * (1.0f / (1<<23));

    // If we use the mantissa too we can refine the error signficantly.
    I32 m_bits = (bits & 0x007fffff) | 0x3f000000;
    F m;
    small_memcpy(&m, &m_bits, sizeof(m));

    return e - 124.225514990f
             -   1.498030302f*m
             -   1.725879990f/(0.3520887068f + m);
}

SI F approx_pow2(F x) {
    F fract = x - floor_(x);

    I32 bits = CAST(I32, (1.0f * (1<<23)) * (x + 121.274057500f
                                               -   1.490129070f*fract
                                               +  27.728023300f/(4.84252568f - fract)));
    small_memcpy(&x, &bits, sizeof(x));
    return x;
}

SI F approx_powf(F x, float y) {
    // Handling all the integral powers first increases our precision a little.
    F r = F1;
    while (y >= 1.0f) {
        r *= x;
        y -= 1.0f;
    }

    // TODO: The rest of this could perhaps be specialized further knowing 0 <= y < 1.
    assert (0 <= y && y < 1);
    return (F)if_then_else((x == F0) | (x == F1), x, r * approx_pow2(approx_log2(x) * y));
}

// Return tf(x).
SI F apply_transfer_function(const skcms_TransferFunction* tf, F x) {
    F sign = (F)if_then_else(x < 0, -F1, F1);
    x *= sign;

    F linear    =             tf->c*x + tf->f;
    F nonlinear = approx_powf(tf->a*x + tf->b, tf->g) + tf->e;

    return sign * (F)if_then_else(x < tf->d, linear, nonlinear);
}

// Strided loads and stores of N values, starting from p.
#if N == 1
    #define LOAD_3(T, p) (T)(p)[0]
    #define LOAD_4(T, p) (T)(p)[0]
    #define STORE_3(p, v) (p)[0] = v
    #define STORE_4(p, v) (p)[0] = v
#elif N == 4 && !defined(USING_NEON)
    #define LOAD_3(T, p) (T){(p)[0], (p)[3], (p)[6], (p)[ 9]}
    #define LOAD_4(T, p) (T){(p)[0], (p)[4], (p)[8], (p)[12]};
    #define STORE_3(p, v) (p)[0] = (v)[0]; (p)[3] = (v)[1]; (p)[6] = (v)[2]; (p)[ 9] = (v)[3]
    #define STORE_4(p, v) (p)[0] = (v)[0]; (p)[4] = (v)[1]; (p)[8] = (v)[2]; (p)[12] = (v)[3]
#elif N == 8
    #define LOAD_3(T, p) (T){(p)[0], (p)[3], (p)[6], (p)[ 9],  (p)[12], (p)[15], (p)[18], (p)[21]}
    #define LOAD_4(T, p) (T){(p)[0], (p)[4], (p)[8], (p)[12],  (p)[16], (p)[20], (p)[24], (p)[28]}
    #define STORE_3(p, v) (p)[ 0] = (v)[0]; (p)[ 3] = (v)[1]; (p)[ 6] = (v)[2]; (p)[ 9] = (v)[3]; \
                          (p)[12] = (v)[4]; (p)[15] = (v)[5]; (p)[18] = (v)[6]; (p)[21] = (v)[7]
    #define STORE_4(p, v) (p)[ 0] = (v)[0]; (p)[ 4] = (v)[1]; (p)[ 8] = (v)[2]; (p)[12] = (v)[3]; \
                          (p)[16] = (v)[4]; (p)[20] = (v)[5]; (p)[24] = (v)[6]; (p)[28] = (v)[7]
#elif N == 16
    // TODO: revisit with AVX-512 gathers and scatters?
    #define LOAD_3(T, p) (T){(p)[ 0], (p)[ 3], (p)[ 6], (p)[ 9], \
                             (p)[12], (p)[15], (p)[18], (p)[21], \
                             (p)[24], (p)[27], (p)[30], (p)[33], \
                             (p)[36], (p)[39], (p)[42], (p)[45]}

    #define LOAD_4(T, p) (T){(p)[ 0], (p)[ 4], (p)[ 8], (p)[12], \
                             (p)[16], (p)[20], (p)[24], (p)[28], \
                             (p)[32], (p)[36], (p)[40], (p)[44], \
                             (p)[48], (p)[52], (p)[56], (p)[60]}

    #define STORE_3(p, v) \
        (p)[ 0] = (v)[ 0]; (p)[ 3] = (v)[ 1]; (p)[ 6] = (v)[ 2]; (p)[ 9] = (v)[ 3]; \
        (p)[12] = (v)[ 4]; (p)[15] = (v)[ 5]; (p)[18] = (v)[ 6]; (p)[21] = (v)[ 7]; \
        (p)[24] = (v)[ 8]; (p)[27] = (v)[ 9]; (p)[30] = (v)[10]; (p)[33] = (v)[11]; \
        (p)[36] = (v)[12]; (p)[39] = (v)[13]; (p)[42] = (v)[14]; (p)[45] = (v)[15]

    #define STORE_4(p, v) \
        (p)[ 0] = (v)[ 0]; (p)[ 4] = (v)[ 1]; (p)[ 8] = (v)[ 2]; (p)[12] = (v)[ 3]; \
        (p)[16] = (v)[ 4]; (p)[20] = (v)[ 5]; (p)[24] = (v)[ 6]; (p)[28] = (v)[ 7]; \
        (p)[32] = (v)[ 8]; (p)[36] = (v)[ 9]; (p)[40] = (v)[10]; (p)[44] = (v)[11]; \
        (p)[48] = (v)[12]; (p)[52] = (v)[13]; (p)[56] = (v)[14]; (p)[60] = (v)[15]
#endif

typedef struct {
    char*        dst;  // Buffer we're writing to in store_xxx.
    const char*  src;  // Buffer we're reading from in load_xxx.
    const void** args; // Pointers to arguments for other stages, in order.
} Context;

typedef void (*Stage)(int i, void** ip, Context* ctx, F r, F g, F b, F a);

SI void next_stage(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    Stage next;
#if defined(__x86_64__)
    __asm__("lodsq" : "=a"(next), "+S"(ip));
#else
    next = (Stage)*ip++;
#endif
    next(i,ip,ctx, r,g,b,a);
}

static void load_565(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    U16 rgb;
    small_memcpy(&rgb, ctx->src + 2*i, 2*N);

    r = CAST(F, rgb & (31<< 0)) * (1.0f / (31<< 0));
    g = CAST(F, rgb & (63<< 5)) * (1.0f / (63<< 5));
    b = CAST(F, rgb & (31<<11)) * (1.0f / (31<<11));
    a = F1;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_888(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const uint8_t* rgb = (const uint8_t*)(ctx->src + 3*i);
#if defined(USING_NEON)
    // There's no uint8x4x3_t or vld3 load for it, so we'll load each rgb pixel one at at time.
    // Since we're doing that, we might as well load them into 16-bit lanes.
    // (We'd even load into 32-bit lanes, but that's not possible on ARMv7.)
    uint8x8x3_t v = {{ vdup_n_u8(0), vdup_n_u8(0), vdup_n_u8(0) }};
    v = vld3_lane_u8(rgb+0, v, 0);
    v = vld3_lane_u8(rgb+3, v, 2);
    v = vld3_lane_u8(rgb+6, v, 4);
    v = vld3_lane_u8(rgb+9, v, 6);

    // Now if we squint, those 3 uint8x8_t we constructed are really U16s, easy to convert to F.
    // (Again, U32 would be even better here if drop ARMv7 or split ARMv7 and ARMv8 impls.)
    r = CAST(F, (U16)v.val[0]) * (1/255.0f);
    g = CAST(F, (U16)v.val[1]) * (1/255.0f);
    b = CAST(F, (U16)v.val[2]) * (1/255.0f);
#else
    r = CAST(F, LOAD_3(U32, rgb+0) ) * (1/255.0f);
    g = CAST(F, LOAD_3(U32, rgb+1) ) * (1/255.0f);
    b = CAST(F, LOAD_3(U32, rgb+2) ) * (1/255.0f);
#endif
    a = F1;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_8888(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    U32 rgba;
    small_memcpy(&rgba, ctx->src + 4*i, 4*N);

    r = CAST(F, (rgba >>  0) & 0xff) * (1/255.0f);
    g = CAST(F, (rgba >>  8) & 0xff) * (1/255.0f);
    b = CAST(F, (rgba >> 16) & 0xff) * (1/255.0f);
    a = CAST(F, (rgba >> 24) & 0xff) * (1/255.0f);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_1010102(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    U32 rgba;
    small_memcpy(&rgba, ctx->src + 4*i, 4*N);

    r = CAST(F, (rgba >>  0) & 0x3ff) * (1/1023.0f);
    g = CAST(F, (rgba >> 10) & 0x3ff) * (1/1023.0f);
    b = CAST(F, (rgba >> 20) & 0x3ff) * (1/1023.0f);
    a = CAST(F, (rgba >> 30) & 0x3  ) * (1/   3.0f);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_161616(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->src + 6*i);
    assert( (ptr & 1) == 0 );                   // The src pointer must be 2-byte aligned
    const uint16_t* rgb = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.
#if defined(USING_NEON)
    uint16x4x3_t v = vld3_u16(rgb);
    r = CAST(F, swap_endian_16(v.val[0])) * (1/65535.0f);
    g = CAST(F, swap_endian_16(v.val[1])) * (1/65535.0f);
    b = CAST(F, swap_endian_16(v.val[2])) * (1/65535.0f);
#else
    U32 R = LOAD_3(U32, rgb+0),
        G = LOAD_3(U32, rgb+1),
        B = LOAD_3(U32, rgb+2);
    // R,G,B are big-endian 16-bit, so byte swap them before converting to float.
    r = CAST(F, (R & 0x00ff)<<8 | (R & 0xff00)>>8) * (1/65535.0f);
    g = CAST(F, (G & 0x00ff)<<8 | (G & 0xff00)>>8) * (1/65535.0f);
    b = CAST(F, (B & 0x00ff)<<8 | (B & 0xff00)>>8) * (1/65535.0f);
#endif
    a = F1;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_16161616(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->src + 8*i);
    assert( (ptr & 1) == 0 );                    // The src pointer must be 2-byte aligned
    const uint16_t* rgba = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.
#if defined(USING_NEON)
    uint16x4x4_t v = vld4_u16(rgba);
    r = CAST(F, swap_endian_16(v.val[0])) * (1/65535.0f);
    g = CAST(F, swap_endian_16(v.val[1])) * (1/65535.0f);
    b = CAST(F, swap_endian_16(v.val[2])) * (1/65535.0f);
    a = CAST(F, swap_endian_16(v.val[3])) * (1/65535.0f);
#else
    U64 px;
    small_memcpy(&px, rgba, 8*N);

    swap_endian_16x4(&px);
    r = CAST(F, (px >>  0) & 0xffff) * (1/65535.0f);
    g = CAST(F, (px >> 16) & 0xffff) * (1/65535.0f);
    b = CAST(F, (px >> 32) & 0xffff) * (1/65535.0f);
    a = CAST(F, (px >> 48) & 0xffff) * (1/65535.0f);
#endif
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_hhh(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->src + 6*i);
    assert( (ptr & 1) == 0 );                   // The src pointer must be 2-byte aligned
    const uint16_t* rgb = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.
#if defined(USING_NEON)
    uint16x4x3_t v = vld3_u16(rgb);
    U16 R = v.val[0],
        G = v.val[1],
        B = v.val[2];
#else
    U16 R = LOAD_3(U16, rgb+0),
        G = LOAD_3(U16, rgb+1),
        B = LOAD_3(U16, rgb+2);
#endif
    r = F_from_Half(R);
    g = F_from_Half(G);
    b = F_from_Half(B);
    a = F1;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_hhhh(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->src + 8*i);
    assert( (ptr & 1) == 0 );                    // The src pointer must be 2-byte aligned
    const uint16_t* rgba = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.
#if defined(USING_NEON)
    uint16x4x4_t v = vld4_u16(rgba);
    U16 R = v.val[0],
        G = v.val[1],
        B = v.val[2],
        A = v.val[3];
#else
    U64 px;
    small_memcpy(&px, rgba, 8*N);
    U16 R = CAST(U16, (px >>  0) & 0xffff),
        G = CAST(U16, (px >> 16) & 0xffff),
        B = CAST(U16, (px >> 32) & 0xffff),
        A = CAST(U16, (px >> 48) & 0xffff);
#endif
    r = F_from_Half(R);
    g = F_from_Half(G);
    b = F_from_Half(B);
    a = F_from_Half(A);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_fff(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->src + 12*i);
    assert( (ptr & 3) == 0 );                   // The src pointer must be 4-byte aligned
    const float* rgb = (const float*)ptr;       // for this cast to const float* to be safe.
#if defined(USING_NEON)
    float32x4x3_t v = vld3q_f32(rgb);
    r = v.val[0];
    g = v.val[1];
    b = v.val[2];
#else
    r = LOAD_3(F, rgb+0);
    g = LOAD_3(F, rgb+1);
    b = LOAD_3(F, rgb+2);
#endif
    a = F1;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void load_ffff(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->src + 16*i);
    assert( (ptr & 3) == 0 );                   // The src pointer must be 4-byte aligned
    const float* rgba = (const float*)ptr;      // for this cast to const float* to be safe.
#if defined(USING_NEON)
    float32x4x4_t v = vld4q_f32(rgba);
    r = v.val[0];
    g = v.val[1];
    b = v.val[2];
    a = v.val[3];
#else
    r = LOAD_4(F, rgba+0);
    g = LOAD_4(F, rgba+1);
    b = LOAD_4(F, rgba+2);
    a = LOAD_4(F, rgba+3);
#endif
    next_stage(i,ip,ctx, r,g,b,a);
}

static void store_565(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    U16 rgb = CAST(U16, to_fixed(r * 31) <<  0 )
            | CAST(U16, to_fixed(g * 63) <<  5 )
            | CAST(U16, to_fixed(b * 31) << 11 );
    small_memcpy(ctx->dst + 2*i, &rgb, 2*N);
    (void)a;
    (void)ip;
}

static void store_888(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uint8_t* rgb = (uint8_t*)ctx->dst + 3*i;
#if defined(USING_NEON)
    // Same deal as load_888 but in reverse... we'll store using uint8x8x3_t, but
    // get there via U16 to save some instructions converting to float.  And just
    // like load_888, we'd prefer to go via U32 but for ARMv7 support.
    U16 R = CAST(U16, to_fixed(r * 255)),
        G = CAST(U16, to_fixed(g * 255)),
        B = CAST(U16, to_fixed(b * 255));

    uint8x8x3_t v = {{ (uint8x8_t)R, (uint8x8_t)G, (uint8x8_t)B }};
    vst3_lane_u8(rgb+0, v, 0);
    vst3_lane_u8(rgb+3, v, 2);
    vst3_lane_u8(rgb+6, v, 4);
    vst3_lane_u8(rgb+9, v, 6);
#else
    STORE_3(rgb+0, CAST(U8, to_fixed(r * 255)) );
    STORE_3(rgb+1, CAST(U8, to_fixed(g * 255)) );
    STORE_3(rgb+2, CAST(U8, to_fixed(b * 255)) );
#endif
    (void)a;
    (void)ip;
}

static void store_8888(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    U32 rgba = CAST(U32, to_fixed(r * 255) <<  0)
             | CAST(U32, to_fixed(g * 255) <<  8)
             | CAST(U32, to_fixed(b * 255) << 16)
             | CAST(U32, to_fixed(a * 255) << 24);
    small_memcpy(ctx->dst + 4*i, &rgba, 4*N);
    (void)ip;
}

static void store_1010102(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    U32 rgba = CAST(U32, to_fixed(r * 1023) <<  0)
             | CAST(U32, to_fixed(g * 1023) << 10)
             | CAST(U32, to_fixed(b * 1023) << 20)
             | CAST(U32, to_fixed(a *    3) << 30);
    small_memcpy(ctx->dst + 4*i, &rgba, 4*N);
    (void)ip;
}

static void store_161616(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->dst + 6*i);
    assert( (ptr & 1) == 0 );                   // The dst pointer must be 2-byte aligned
    uint16_t* rgb = (uint16_t*)ptr;             // for this cast to uint16_t* to be safe.
#if defined(USING_NEON)
    uint16x4x3_t v = {{
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(r * 65535))),
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(g * 65535))),
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(b * 65535))),
    }};
    vst3_u16(rgb, v);
#else
    I32 R = to_fixed(r * 65535),
        G = to_fixed(g * 65535),
        B = to_fixed(b * 65535);
    STORE_3(rgb+0, CAST(U16, (R & 0x00ff) << 8 | (R & 0xff00) >> 8) );
    STORE_3(rgb+1, CAST(U16, (G & 0x00ff) << 8 | (G & 0xff00) >> 8) );
    STORE_3(rgb+2, CAST(U16, (B & 0x00ff) << 8 | (B & 0xff00) >> 8) );
#endif
    (void)a;
    (void)ip;
}

static void store_16161616(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->dst + 8*i);
    assert( (ptr & 1) == 0 );                   // The dst pointer must be 2-byte aligned
    uint16_t* rgba = (uint16_t*)ptr;            // for this cast to uint16_t* to be safe.
#if defined(USING_NEON)
    uint16x4x4_t v = {{
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(r * 65535))),
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(g * 65535))),
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(b * 65535))),
        (uint16x4_t)swap_endian_16(CAST(U16, to_fixed(a * 65535))),
    }};
    vst4_u16(rgba, v);
#else
    U64 px = CAST(U64, to_fixed(r * 65535)) <<  0
           | CAST(U64, to_fixed(g * 65535)) << 16
           | CAST(U64, to_fixed(b * 65535)) << 32
           | CAST(U64, to_fixed(a * 65535)) << 48;
    swap_endian_16x4(&px);
    small_memcpy(rgba, &px, 8*N);
#endif
    (void)ip;
}

static void store_hhh(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->dst + 6*i);
    assert( (ptr & 1) == 0 );                   // The dst pointer must be 2-byte aligned
    uint16_t* rgb = (uint16_t*)ptr;             // for this cast to uint16_t* to be safe.

    U16 R = Half_from_F(r),
        G = Half_from_F(g),
        B = Half_from_F(b);
#if defined(USING_NEON)
    uint16x4x3_t v = {{
        (uint16x4_t)R,
        (uint16x4_t)G,
        (uint16x4_t)B,
    }};
    vst3_u16(rgb, v);
#else
    STORE_3(rgb+0, R);
    STORE_3(rgb+1, G);
    STORE_3(rgb+2, B);
#endif
    (void)a;
    (void)ip;
}

static void store_hhhh(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->dst + 8*i);
    assert( (ptr & 1) == 0 );                   // The dst pointer must be 2-byte aligned
    uint16_t* rgba = (uint16_t*)ptr;            // for this cast to uint16_t* to be safe.

    U16 R = Half_from_F(r),
        G = Half_from_F(g),
        B = Half_from_F(b),
        A = Half_from_F(a);
#if defined(USING_NEON)
    uint16x4x4_t v = {{
        (uint16x4_t)R,
        (uint16x4_t)G,
        (uint16x4_t)B,
        (uint16x4_t)A,
    }};
    vst4_u16(rgba, v);
#else
    U64 px = CAST(U64, R) <<  0
           | CAST(U64, G) << 16
           | CAST(U64, B) << 32
           | CAST(U64, A) << 48;
    small_memcpy(rgba, &px, 8*N);
#endif
    (void)ip;
}

static void store_fff(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->dst + 12*i);
    assert( (ptr & 3) == 0 );                   // The dst pointer must be 4-byte aligned
    float* rgb = (float*)ptr;                   // for this cast to float* to be safe.
#if defined(USING_NEON)
    float32x4x3_t v = {{
        (float32x4_t)r,
        (float32x4_t)g,
        (float32x4_t)b,
    }};
    vst3q_f32(rgb, v);
#else
    STORE_3(rgb+0, r);
    STORE_3(rgb+1, g);
    STORE_3(rgb+2, b);
#endif
    (void)a;
    (void)ip;
}

static void store_ffff(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(ctx->dst + 16*i);
    assert( (ptr & 3) == 0 );                   // The dst pointer must be 4-byte aligned
    float* rgba = (float*)ptr;                  // for this cast to float* to be safe.
#if defined(USING_NEON)
    float32x4x4_t v = {{
        (float32x4_t)r,
        (float32x4_t)g,
        (float32x4_t)b,
        (float32x4_t)a,
    }};
    vst4q_f32(rgba, v);
#else
    STORE_4(rgba+0, r);
    STORE_4(rgba+1, g);
    STORE_4(rgba+2, b);
    STORE_4(rgba+3, a);
#endif
    (void)ip;
}

static void swap_rb(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    next_stage(i,ip,ctx, b,g,r,a);
}

static void invert(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    r = F1 - r;
    g = F1 - g;
    b = F1 - b;
    a = F1 - a;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void unpremul(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    F scale = (F)if_then_else(F1 / a < INFINITY, F1 / a, F0);
    r *= scale;
    g *= scale;
    b *= scale;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void premul(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    r *= a;
    g *= a;
    b *= a;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void clamp(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    r = max_(F0, min_(r, F1));
    g = max_(F0, min_(g, F1));
    b = max_(F0, min_(b, F1));
    a = max_(F0, min_(a, F1));
    next_stage(i,ip,ctx, r,g,b,a);
}

static void force_opaque(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    a = F1;
    next_stage(i,ip,ctx, r,g,b,a);
}

static void tf_r(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_TransferFunction* tf = *ctx->args++;
    r = apply_transfer_function(tf, r);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void tf_g(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_TransferFunction* tf = *ctx->args++;
    g = apply_transfer_function(tf, g);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void tf_b(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_TransferFunction* tf = *ctx->args++;
    b = apply_transfer_function(tf, b);
    next_stage(i,ip,ctx, r,g,b,a);
}

// Applied to the 'a' channel when using 4-channel input, i.e. CMYK.  So think 'tf_K'.
static void tf_a(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_TransferFunction* tf = *ctx->args++;
    a = apply_transfer_function(tf, a);
    next_stage(i,ip,ctx, r,g,b,a);
}

SI U8 gather_8(const uint8_t* p, I32 ix) {
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

// Helper for gather_16(), loading the ix'th 16-bit value from p.
SI uint16_t load_16(const uint8_t* p, int ix) {
    uint16_t v;
    small_memcpy(&v, p + 2*ix, 2);
    return v;
}

SI U16 gather_16(const uint8_t* p, I32 ix) {
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

SI F F_from_U8(U8 v) {
    return CAST(F, v) * (1/255.0f);
}

SI F F_from_U16_BE(U16 v) {
    // All 16-bit ICC values are big-endian, so we byte swap before converting to float.
    // MSVC catches the "loss" of data here in the portable path, so we also make sure to mask.
    v = (U16)( ((v<<8)|(v>>8)) & 0xffff );
    return CAST(F, v) * (1/65535.0f);
}

SI F minus_1_ulp(F v) {
    I32 bits;
    small_memcpy(&bits, &v, sizeof(bits));
    bits = bits - 1;
    small_memcpy(&v, &bits, sizeof(bits));
    return v;
}

SI F table_8(const skcms_Curve* curve, F v) {
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

SI F table_16(const skcms_Curve* curve, F v) {
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

static void table_8_r(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    r = table_8(*ctx->args++, r);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void table_16_r(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    r = table_16(*ctx->args++, r);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void table_8_g(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    g = table_8(*ctx->args++, g);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void table_16_g(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    g = table_16(*ctx->args++, g);
    next_stage(i,ip,ctx, r,g,b,a);
}

static void table_8_b(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    b = table_8(*ctx->args++, b);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void table_16_b(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    b = table_16(*ctx->args++, b);
    next_stage(i,ip,ctx, r,g,b,a);
}

// Applied to the 'a' channel when using 4-channel input, i.e. CMYK.  So think 'table_{8,16}_K'.
static void table_8_a(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    a = table_8(*ctx->args++, a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void table_16_a(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    a = table_16(*ctx->args++, a);
    next_stage(i,ip,ctx, r,g,b,a);
}

typedef struct {
    Stage       stage;
    const void* arg;
} StageAndArg;

SI StageAndArg select_curve_stage(const skcms_Curve* curve, int channel) {
    static const struct { Stage parametric, table_8, table_16; } stages[] = {
        { tf_r, table_8_r, table_16_r },
        { tf_g, table_8_g, table_16_g },
        { tf_b, table_8_b, table_16_b },
        { tf_a, table_8_a, table_16_a },
    };

    if (curve->table_entries == 0) {
        return (StageAndArg){ stages[channel].parametric, &curve->parametric };
    } else if (curve->table_8) {
        return (StageAndArg){ stages[channel].table_8,  curve };
    } else if (curve->table_16) {
        return (StageAndArg){ stages[channel].table_16, curve };
    }

    assert(false);
    return (StageAndArg){NULL,NULL};
}

static void matrix_3x3(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_Matrix3x3* matrix = *ctx->args++;
    const float* m = &matrix->vals[0][0];

    F R = m[0]*r + m[1]*g + m[2]*b,
      G = m[3]*r + m[4]*g + m[5]*b,
      B = m[6]*r + m[7]*g + m[8]*b;

    next_stage(i,ip,ctx, R,G,B,a);
}

static void matrix_3x4(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_Matrix3x4* matrix = *ctx->args++;
    const float* m = &matrix->vals[0][0];

    F R = m[0]*r + m[1]*g + m[ 2]*b + m[ 3],
      G = m[4]*r + m[5]*g + m[ 6]*b + m[ 7],
      B = m[8]*r + m[9]*g + m[10]*b + m[11];

    next_stage(i,ip,ctx, R,G,B,a);
}

// Color lookup tables, by input dimension and bit depth.
SI void clut_0_8(const skcms_A2B* a2b, I32 ix, I32 stride, F* r, F* g, F* b, F a) {
    // TODO: gather_24()?
    *r = F_from_U8(gather_8(a2b->grid_8, 3*ix+0));
    *g = F_from_U8(gather_8(a2b->grid_8, 3*ix+1));
    *b = F_from_U8(gather_8(a2b->grid_8, 3*ix+2));
    (void)a;
    (void)stride;
}
SI void clut_0_16(const skcms_A2B* a2b, I32 ix, I32 stride, F* r, F* g, F* b, F a) {
    // TODO: gather_48()?
    *r = F_from_U16_BE(gather_16(a2b->grid_16, 3*ix+0));
    *g = F_from_U16_BE(gather_16(a2b->grid_16, 3*ix+1));
    *b = F_from_U16_BE(gather_16(a2b->grid_16, 3*ix+2));
    (void)a;
    (void)stride;
}

// These are all the same basic approach: handle one dimension, then the rest recursively.
// We let "I" be the current dimension, and "J" the previous dimension, I-1.  "B" is the bit depth.
// We use static inline here: __attribute__((always_inline)) hits some pathological
// case in GCC that makes compilation way too slow for my patience.
#define DEF_CLUT(I,J,B)                                                                       \
    static inline                                                                             \
    void clut_##I##_##B(const skcms_A2B* a2b, I32 ix, I32 stride, F* r, F* g, F* b, F a) {    \
        I32 limit = CAST(I32, F0);                                                            \
        limit += a2b->grid_points[I-1];                                                       \
                                                                                              \
        const F* srcs[] = { r,g,b,&a };                                                       \
        F src = *srcs[I-1];                                                                   \
                                                                                              \
        F x = max_(F0, min_(src, F1)) * CAST(F, limit - 1);                                   \
                                                                                              \
        I32 lo = CAST(I32,             x      ),                                              \
            hi = CAST(I32, minus_1_ulp(x+1.0f));                                              \
        F lr = *r, lg = *g, lb = *b,                                                          \
          hr = *r, hg = *g, hb = *b;                                                          \
        clut_##J##_##B(a2b, stride*lo + ix, stride*limit, &lr,&lg,&lb,a);                     \
        clut_##J##_##B(a2b, stride*hi + ix, stride*limit, &hr,&hg,&hb,a);                     \
                                                                                              \
        F t = x - CAST(F, lo);                                                                \
        *r = lr + (hr-lr)*t;                                                                  \
        *g = lg + (hg-lg)*t;                                                                  \
        *b = lb + (hb-lb)*t;                                                                  \
    }

DEF_CLUT(1,0,8)
DEF_CLUT(2,1,8)
DEF_CLUT(3,2,8)
DEF_CLUT(4,3,8)

DEF_CLUT(1,0,16)
DEF_CLUT(2,1,16)
DEF_CLUT(3,2,16)
DEF_CLUT(4,3,16)

// Now the stages that just call into the various implementations above.
static void clut_1D_8(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_1_8(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void clut_2D_8(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_2_8(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void clut_3D_8(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_3_8(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void clut_4D_8(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_4_8(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    // 'a' was really a CMYK K, so our output is actually opaque.
    next_stage(i,ip,ctx, r,g,b,F1);
}

static void clut_1D_16(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_1_16(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void clut_2D_16(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_2_16(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void clut_3D_16(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_3_16(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    next_stage(i,ip,ctx, r,g,b,a);
}
static void clut_4D_16(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    const skcms_A2B* a2b = *ctx->args++;
    clut_4_16(a2b, CAST(I32,F0),CAST(I32,F1), &r,&g,&b,a);
    // 'a' was really a CMYK K, so our output is actually opaque.
    next_stage(i,ip,ctx, r,g,b,F1);
}

static void lab_to_xyz(int i, void** ip, Context* ctx, F r, F g, F b, F a) {
    // The L*a*b values are in r,g,b, but have been normalized to [0,1].  Reconstruct them:
    F L = r * 100.0f,
      A = g * 255.0f - 128.0f,
      B = b * 255.0f - 128.0f;

    // Convert to CIE XYZ.
    F Y = (L + 16.0f) * (1/116.0f),
      X = Y + A*(1/500.0f),
      Z = Y - B*(1/200.0f);

    X = (F)if_then_else(X*X*X > 0.008856f, X*X*X, (X - (16/116.0f)) * (1/7.787f));
    Y = (F)if_then_else(Y*Y*Y > 0.008856f, Y*Y*Y, (Y - (16/116.0f)) * (1/7.787f));
    Z = (F)if_then_else(Z*Z*Z > 0.008856f, Z*Z*Z, (Z - (16/116.0f)) * (1/7.787f));

    // Adjust to XYZD50 illuminant, and stuff back into r,g,b for the next stage.
    r = X * 0.9642f;
    g = Y          ;
    b = Z * 0.8249f;

    next_stage(i,ip,ctx, r,g,b,a);
}

SI size_t bytes_per_pixel(skcms_PixelFormat fmt) {
    switch (fmt >> 1) {   // ignore rgb/bgr
        case skcms_PixelFormat_RGB_565       >> 1: return  2;
        case skcms_PixelFormat_RGB_888       >> 1: return  3;
        case skcms_PixelFormat_RGBA_8888     >> 1: return  4;
        case skcms_PixelFormat_RGBA_1010102  >> 1: return  4;
        case skcms_PixelFormat_RGB_161616    >> 1: return  6;
        case skcms_PixelFormat_RGBA_16161616 >> 1: return  8;
        case skcms_PixelFormat_RGB_hhh       >> 1: return  6;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: return  8;
        case skcms_PixelFormat_RGB_fff       >> 1: return 12;
        case skcms_PixelFormat_RGBA_ffff     >> 1: return 16;
    }
    assert(false);
    return 0;
}

bool skcms_Transform(const void*             src,
                     skcms_PixelFormat       srcFmt,
                     skcms_AlphaFormat       srcAlpha,
                     const skcms_ICCProfile* srcProfile,
                     void*                   dst,
                     skcms_PixelFormat       dstFmt,
                     skcms_AlphaFormat       dstAlpha,
                     const skcms_ICCProfile* dstProfile,
                     size_t                  nz) {
    const size_t dst_bpp = bytes_per_pixel(dstFmt),
                 src_bpp = bytes_per_pixel(srcFmt);
    // Let's just refuse if the request is absurdly big.
    if (nz * dst_bpp > INT_MAX || nz * src_bpp > INT_MAX) {
        return false;
    }
    int n = (int)nz;

    // Both profiles can be null if we're just doing format conversion, otherwise both are needed
    if (!dstProfile != !srcProfile) {
        return false;
    }

    // We can't transform in place unless the PixelFormats are the same size.
    if (dst == src && (dstFmt >> 1) != (srcFmt >> 1)) {
        return false;
    }
    // TODO: this check lazilly disallows U16 <-> F16, but that would actually be fine.
    // TODO: more careful alias rejection (like, dst == src + 1)?

    void*       program  [32];   // Pointers to stages.
    const void* arguments[32];   // Arguments for non-load-store stages.

    void**       ip   = program;
    const void** args = arguments;

    skcms_TransferFunction inv_dst_tf_r, inv_dst_tf_g, inv_dst_tf_b;
    skcms_Matrix3x3        from_xyz;

    switch (srcFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGB_565       >> 1: *ip++ = (void*)load_565;      break;
        case skcms_PixelFormat_RGB_888       >> 1: *ip++ = (void*)load_888;      break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ip++ = (void*)load_8888;     break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ip++ = (void*)load_1010102;  break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ip++ = (void*)load_161616;   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ip++ = (void*)load_16161616; break;
        case skcms_PixelFormat_RGB_hhh       >> 1: *ip++ = (void*)load_hhh;      break;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: *ip++ = (void*)load_hhhh;     break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ip++ = (void*)load_fff;      break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ip++ = (void*)load_ffff;     break;
    }
    if (srcFmt & 1) {
        *ip++ = (void*)swap_rb;
    }

    if (srcProfile->data_color_space == 0x434D594B /*'CMYK*/) {
        // Photoshop creates CMYK images as inverse CMYK.
        // These happen to be the only ones we've _ever_ seen.
        *ip++ = (void*)invert;
    }

    if (srcAlpha == skcms_AlphaFormat_Opaque) {
        *ip++ = (void*)force_opaque;
    } else if (srcAlpha == skcms_AlphaFormat_PremulAsEncoded) {
        *ip++ = (void*)unpremul;
    }

    // TODO: We can skip this work if both srcAlpha and dstAlpha are PremulLinear, and the profiles
    // are the same. Also, if dstAlpha is PremulLinear, and SrcAlpha is Opaque.
    if (dstProfile != srcProfile ||
        srcAlpha == skcms_AlphaFormat_PremulLinear ||
        dstAlpha == skcms_AlphaFormat_PremulLinear) {

        if (srcProfile->has_A2B) {
            if (srcProfile->A2B.input_channels) {
                for (int i = 0; i < (int)srcProfile->A2B.input_channels; i++) {
                    StageAndArg sa = select_curve_stage(&srcProfile->A2B.input_curves[i], i);
                    *ip++   = (void*)sa.stage;
                    *args++ =        sa.arg;
                }
                switch (srcProfile->A2B.input_channels) {
                    case 1: *ip++ = (void*)(srcProfile->A2B.grid_8 ? clut_1D_8 : clut_1D_16); break;
                    case 2: *ip++ = (void*)(srcProfile->A2B.grid_8 ? clut_2D_8 : clut_2D_16); break;
                    case 3: *ip++ = (void*)(srcProfile->A2B.grid_8 ? clut_3D_8 : clut_3D_16); break;
                    case 4: *ip++ = (void*)(srcProfile->A2B.grid_8 ? clut_4D_8 : clut_4D_16); break;
                    default: return false;
                }
                *args++ = &srcProfile->A2B;
            }

            if (srcProfile->A2B.matrix_channels == 3) {
                for (int i = 0; i < 3; i++) {
                    StageAndArg sa = select_curve_stage(&srcProfile->A2B.matrix_curves[i], i);
                    *ip++   = (void*)sa.stage;
                    *args++ =        sa.arg;
                }
                *ip++   = (void*)matrix_3x4;
                *args++ = &srcProfile->A2B.matrix;
            }

            if (srcProfile->A2B.output_channels == 3) {
                for (int i = 0; i < 3; i++) {
                    StageAndArg sa = select_curve_stage(&srcProfile->A2B.output_curves[i], i);
                    *ip++   = (void*)sa.stage;
                    *args++ =        sa.arg;
                }
            }

            if (srcProfile->pcs == 0x4C616220 /* 'Lab ' */) {
                *ip++ = (void*)lab_to_xyz;
            }

        } else if (srcProfile->has_trc && srcProfile->has_toXYZD50) {
            for (int i = 0; i < 3; i++) {
                StageAndArg sa = select_curve_stage(&srcProfile->trc[i], i);
                *ip++   = (void*)sa.stage;
                *args++ =        sa.arg;
            }
        } else {
            return false;
        }

        // At this point our source colors are linear, either RGB (XYZ-type profiles)
        // or XYZ (A2B-type profiles). Unpremul is a linear operation (multiply by a
        // constant 1/a), so either way we can do it now if needed.
        if (srcAlpha == skcms_AlphaFormat_PremulLinear) {
            *ip++ = (void*)unpremul;
        }

        // We only support destination gamuts that can be transformed from XYZD50.
        if (!dstProfile->has_toXYZD50) {
            return false;
        }

        // A2B sources should already be in XYZD50 at this point.
        // Others still need to be transformed using their toXYZD50 matrix.
        // N.B. There are profiles that contain both A2B tags and toXYZD50 matrices.
        // If we use the A2B tags, we need to ignore the XYZD50 matrix entirely.
        assert (srcProfile->has_A2B || srcProfile->has_toXYZD50);
        static const skcms_Matrix3x3 I = {{
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f },
        }};
        const skcms_Matrix3x3* to_xyz = srcProfile->has_A2B ? &I : &srcProfile->toXYZD50;

        // There's a chance the source and destination gamuts are identical,
        // in which case we can skip the gamut transform.
        if (0 != memcmp(&dstProfile->toXYZD50, to_xyz, sizeof(skcms_Matrix3x3))) {
            if (!skcms_Matrix3x3_invert(&dstProfile->toXYZD50, &from_xyz)) {
                return false;
            }
            // TODO: concat these here and only append one matrix_3x3 stage.
            *ip++ = (void*)matrix_3x3; *args++ =    to_xyz;
            *ip++ = (void*)matrix_3x3; *args++ = &from_xyz;
        }

        // Encode back to dst RGB using its parametric transfer functions.
        if (dstProfile->has_trc &&
            dstProfile->trc[0].table_entries == 0 &&
            dstProfile->trc[1].table_entries == 0 &&
            dstProfile->trc[2].table_entries == 0 &&
            skcms_TransferFunction_invert(&dstProfile->trc[0].parametric, &inv_dst_tf_r) &&
            skcms_TransferFunction_invert(&dstProfile->trc[1].parametric, &inv_dst_tf_g) &&
            skcms_TransferFunction_invert(&dstProfile->trc[2].parametric, &inv_dst_tf_b)) {

            if (dstAlpha == skcms_AlphaFormat_PremulLinear) {
                *ip++ = (void*)premul;
            }

            *ip++ = (void*)tf_r;       *args++ = &inv_dst_tf_r;
            *ip++ = (void*)tf_g;       *args++ = &inv_dst_tf_g;
            *ip++ = (void*)tf_b;       *args++ = &inv_dst_tf_b;
        } else {
            return false;
        }
    }

    if (dstAlpha == skcms_AlphaFormat_Opaque) {
        *ip++ = (void*)force_opaque;
    } else if (dstAlpha == skcms_AlphaFormat_PremulAsEncoded) {
        *ip++ = (void*)premul;
    }
    if (dstFmt & 1) {
        *ip++ = (void*)swap_rb;
    }
    if (dstFmt < skcms_PixelFormat_RGB_hhh) {
        *ip++ = (void*)clamp;
    }
    switch (dstFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGB_565       >> 1: *ip++ = (void*)store_565;      break;
        case skcms_PixelFormat_RGB_888       >> 1: *ip++ = (void*)store_888;      break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ip++ = (void*)store_8888;     break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ip++ = (void*)store_1010102;  break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ip++ = (void*)store_161616;   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ip++ = (void*)store_16161616; break;
        case skcms_PixelFormat_RGB_hhh       >> 1: *ip++ = (void*)store_hhh;      break;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: *ip++ = (void*)store_hhhh;     break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ip++ = (void*)store_fff;      break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ip++ = (void*)store_ffff;     break;
    }

    int i = 0;
    while (n >= N) {
        Context ctx = { dst, src, arguments };
        Stage start = (Stage)program[0];
        start(i,program+1,&ctx, F0,F0,F0,F0);
        i += N;
        n -= N;
    }
    if (n > 0) {
        // Pad out src and dst so our stage functions can pretend they're working on N pixels.
        // Big enough to hold any of our skcms_PixelFormats, the largest being 4x 4-byte float.
        char tmp_src[4*4*N] = {0},
             tmp_dst[4*4*N] = {0};

        memcpy(tmp_src, (const char*)src + (size_t)i*src_bpp, (size_t)n*src_bpp);

        Context ctx = { tmp_dst, tmp_src, arguments };
        Stage start = (Stage)program[0];
        start(0,program+1,&ctx, F0,F0,F0,F0);

        memcpy((char*)dst + (size_t)i*dst_bpp, tmp_dst, (size_t)n*dst_bpp);
    }
    return true;
}
