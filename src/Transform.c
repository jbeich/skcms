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
#include <stdint.h>
#include <string.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
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
    #include <arm_neon.h>
    #define USING_NEON
    #if __ARM_FP & 2
        #define USING_NEON_F16C
    #endif
#elif N == 8 && defined(__AVX__)
    #include <immintrin.h>
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
#elif !defined(USING_NEON_F16C)
    #define if_then_else(c,t,e) ( ((c) & (I32)(t)) | (~(c) & (I32)(e)) )
#endif

#if defined(USING_NEON_F16C)
    SI F F_from_Half(U16 half) { return vcvt_f32_f16(half); }
    SI U16 Half_from_F(F f)    { return vcvt_f16_f32(f   ); }
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
    SI F min(F x, F y) { return vminq_f32(x,y); }
    SI F max(F x, F y) { return vmaxq_f32(x,y); }
#else
    SI F min(F x, F y) { return (F)if_then_else(x > y, y, x); }
    SI F max(F x, F y) { return (F)if_then_else(x < y, y, x); }
#endif

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
#endif

typedef struct {
    skcms_TransferFunction src_tf;
    skcms_TransferFunction dst_tf;
    skcms_Matrix3x3 to_xyz;
    skcms_Matrix3x3 from_xyz;
} Context;

typedef void (*Stage)(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx);

SI void next_stage(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                   const Context* ctx) {
    Stage next;
#if defined(__x86_64__)
    __asm__("lodsq" : "=a"(next), "+S"(ip));
#else
    next = (Stage)*ip++;
#endif
    next(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_565(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                     const Context* ctx) {
    U16 rgb;
    small_memcpy(&rgb, src + 2*i, 2*N);

    r = CAST(F, rgb & (31<< 0)) * (1.0f / (31<< 0));
    g = CAST(F, rgb & (63<< 5)) * (1.0f / (63<< 5));
    b = CAST(F, rgb & (31<<11)) * (1.0f / (31<<11));
    a = F1;
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                     const Context* ctx) {
    const uint8_t* rgb = (const uint8_t*)(src + 3*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_8888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    U32 rgba;
    small_memcpy(&rgba, src + 4*i, 4*N);

    r = CAST(F, (rgba >>  0) & 0xff) * (1/255.0f);
    g = CAST(F, (rgba >>  8) & 0xff) * (1/255.0f);
    b = CAST(F, (rgba >> 16) & 0xff) * (1/255.0f);
    a = CAST(F, (rgba >> 24) & 0xff) * (1/255.0f);
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_1010102(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                         const Context* ctx) {
    U32 rgba;
    small_memcpy(&rgba, src + 4*i, 4*N);

    r = CAST(F, (rgba >>  0) & 0x3ff) * (1/1023.0f);
    g = CAST(F, (rgba >> 10) & 0x3ff) * (1/1023.0f);
    b = CAST(F, (rgba >> 20) & 0x3ff) * (1/1023.0f);
    a = CAST(F, (rgba >> 30) & 0x3  ) * (1/   3.0f);
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_161616(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                        const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(src + 6*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_16161616(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                          const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(src + 8*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_hhh(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                     const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(src + 6*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_hhhh(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(src + 8*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_fff(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                     const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(src + 12*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void load_ffff(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(src + 16*i);
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
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void store_565(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    U16 rgb = CAST(U16, to_fixed(r * 31) <<  0 )
            | CAST(U16, to_fixed(g * 63) <<  5 )
            | CAST(U16, to_fixed(b * 31) << 11 );
    small_memcpy(dst + 2*i, &rgb, 2*N);
    (void)a;
    (void)ip;
    (void)src;
    (void)ctx;
}

static void store_888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    uint8_t* rgb = (uint8_t*)dst + 3*i;
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
    (void)src;
    (void)ctx;
}

static void store_8888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                       const Context* ctx) {
    U32 rgba = CAST(U32, to_fixed(r * 255) <<  0)
             | CAST(U32, to_fixed(g * 255) <<  8)
             | CAST(U32, to_fixed(b * 255) << 16)
             | CAST(U32, to_fixed(a * 255) << 24);
    small_memcpy(dst + 4*i, &rgba, 4*N);
    (void)ip;
    (void)src;
    (void)ctx;
}

static void store_1010102(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                          const Context* ctx) {
    U32 rgba = CAST(U32, to_fixed(r * 1023) <<  0)
             | CAST(U32, to_fixed(g * 1023) << 10)
             | CAST(U32, to_fixed(b * 1023) << 20)
             | CAST(U32, to_fixed(a *    3) << 30);
    small_memcpy(dst + 4*i, &rgba, 4*N);
    (void)ip;
    (void)src;
    (void)ctx;
}

static void store_161616(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                         const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(dst + 6*i);
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
    (void)src;
    (void)ctx;
}

static void store_16161616(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                           const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(dst + 8*i);
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
    (void)src;
    (void)ctx;
}

static void store_hhh(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(dst + 6*i);
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
    (void)src;
    (void)ctx;
}

static void store_hhhh(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                       const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(dst + 8*i);
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
    (void)src;
    (void)ctx;
}

static void store_fff(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                      const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(dst + 12*i);
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
    (void)src;
    (void)ctx;
}

static void store_ffff(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                       const Context* ctx) {
    uintptr_t ptr = (uintptr_t)(dst + 16*i);
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
    (void)src;
    (void)ctx;
}

static void swap_rb(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                    const Context* ctx) {
    next_stage(i,ip,dst,src, b,g,r,a, ctx);
}

static void clamp(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                  const Context* ctx) {
    r = max(F0, min(r, F1));
    g = max(F0, min(g, F1));
    b = max(F0, min(b, F1));
    a = max(F0, min(a, F1));
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void force_opaque(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                         const Context* ctx) {
    a = F1;
    next_stage(i,ip,dst,src, r,g,b,a, ctx);
}

static void src_tf(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                   const Context* ctx) {
    // TODO: Math
    next_stage(i, ip, dst, src, r, g, b, a, ctx);
}

static void dst_tf(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                   const Context* ctx) {
    // TODO: Math
    next_stage(i, ip, dst, src, r, g, b, a, ctx);
}

static void to_xyz(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                   const Context* ctx) {
    // TODO: Math
    next_stage(i, ip, dst, src, r, g, b, a, ctx);
}

static void from_xyz(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a,
                     const Context* ctx) {
    // TODO: Math
    next_stage(i, ip, dst, src, r, g, b, a, ctx);
}

SI size_t bytes_per_pixel(skcms_PixelFormat fmt) {
    switch (fmt >> 1) {   // ignore rgb/bgr
        case skcms_PixelFormat_RGB_565       >> 1: return  2;
        case skcms_PixelFormat_RGB_888       >> 1: return  3;
        case skcms_PixelFormat_RGBA_8888     >> 1: return  4;
        case skcms_PixelFormat_RGB_101010x   >> 1: return  4;
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

bool skcms_Transform(void* dst, skcms_PixelFormat dstFmt, const skcms_ICCProfile* dstProfile,
               const void* src, skcms_PixelFormat srcFmt, const skcms_ICCProfile* srcProfile,
                     size_t n) {
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

    void* program[32];
    void** ip = program;
    Context ctx;

    switch (srcFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGB_565       >> 1: *ip++ = (void*)load_565;      break;
        case skcms_PixelFormat_RGB_888       >> 1: *ip++ = (void*)load_888;      break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ip++ = (void*)load_8888;     break;
        case skcms_PixelFormat_RGB_101010x   >> 1: *ip++ = (void*)load_1010102;
                                                   *ip++ = (void*)force_opaque;  break;
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

    if (dstProfile != srcProfile) {
        // TODO: A2B, Lab -> XYZ, tables, etc...

        // 1) Src RGB to XYZ
        if (skcms_ICCProfile_getTransferFunction(srcProfile, &ctx.src_tf) &&
            skcms_ICCProfile_toXYZD50(srcProfile, &ctx.to_xyz)) {
            *ip++ = (void*)src_tf;
            *ip++ = (void*)to_xyz;
        } else {
            return false;
        }

        // 2) Lab <-> XYZ (if PCS is different)

        // 3) Dst XYZ to RGB
        if (skcms_ICCProfile_getTransferFunction(dstProfile, &ctx.dst_tf) &&
            skcms_ICCProfile_toXYZD50(dstProfile, &ctx.from_xyz) &&
            skcms_TransferFunction_invert(&ctx.dst_tf, &ctx.dst_tf) &&
            skcms_Matrix3x3_invert(&ctx.from_xyz, &ctx.from_xyz)) {
            *ip++ = (void*)from_xyz;
            *ip++ = (void*)dst_tf;
        } else {
            return false;
        }
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
        case skcms_PixelFormat_RGB_101010x   >> 1: *ip++ = (void*)force_opaque;
                                                   *ip++ = (void*)store_1010102;  break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ip++ = (void*)store_1010102;  break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ip++ = (void*)store_161616;   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ip++ = (void*)store_16161616; break;
        case skcms_PixelFormat_RGB_hhh       >> 1: *ip++ = (void*)store_hhh;      break;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: *ip++ = (void*)store_hhhh;     break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ip++ = (void*)store_fff;      break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ip++ = (void*)store_ffff;     break;
    }

    size_t i = 0;
    while (n >= N) {
        Stage start = (Stage)program[0];
        start(i,program+1,dst,src, F0,F0,F0,F0, &ctx);
        i += N;
        n -= N;
    }
    if (n > 0) {
        // Pad out src and dst so our stage functions can pretend they're working on N pixels.
        // Big enough to hold any of our skcms_PixelFormats, the largest being 4x 4-byte float.
        char tmp_src[4*4*N] = {0},
             tmp_dst[4*4*N] = {0};

        size_t src_bpp = bytes_per_pixel(srcFmt);
        memcpy(tmp_src, (const char*)src + i*src_bpp, n*src_bpp);

        Stage start = (Stage)program[0];
        start(0,program+1,tmp_dst,tmp_src, F0,F0,F0,F0, &ctx);

        size_t dst_bpp = bytes_per_pixel(dstFmt);
        memcpy((char*)dst + i*dst_bpp, tmp_dst, n*dst_bpp);
    }
    return true;
}
