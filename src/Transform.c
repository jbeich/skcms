/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

typedef void (*Stage)(size_t i, void** ip, char* dst, const char* src, char* tmp,
                      F r, F g, F b, F a);

static void next_stage(size_t i, void** ip, char* dst, const char* src, char* tmp,
                      F r, F g, F b, F a) {
    Stage next;
#if defined(__x86_64__)
    __asm__("lodsq" : "=a"(next), "+S"(ip));
#else
    next = (Stage)*ip++;
#endif
    next(i,ip,dst,src,tmp, r,g,b,a);
}

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
static I32 to_fixed(F f) { return CAST(I32, f + 0.5f); }

static void load_565_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    U16 rgb;
    memcpy(&rgb, src + 2*i, 2*N);

    r = CAST(F, rgb & (31<< 0)) * (1.0f / (31<< 0));
    g = CAST(F, rgb & (63<< 5)) * (1.0f / (63<< 5));
    b = CAST(F, rgb & (31<<11)) * (1.0f / (31<<11));
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_565_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    memcpy(tmp, src + 2*i, 2);
    src = tmp - 2*i;
    load_565_N(i,ip,dst,src,tmp, r,g,b,a);
}

// Strided loads and stores of N values, starting from p.
#if N == 1
    #define LOAD_3(T, p) (T)(p)[0]
    #define LOAD_4(T, p) (T)(p)[0]
    #define STORE_3(p, v) (p)[0] = v
    #define STORE_4(p, v) (p)[0] = v
#elif N == 4
    #define LOAD_3(T, p) (T){(p)[0], (p)[3], (p)[6], (p)[ 9]}
    #define LOAD_4(T, p) (T){(p)[0], (p)[4], (p)[8], (p)[12]};
    #define STORE_3(p, v) for (int k = 0; k < N; k++) (p)[3*k] = v[k]
    #define STORE_4(p, v) for (int k = 0; k < N; k++) (p)[4*k] = v[k]
#elif N == 8
    #define LOAD_3(T, p) (T){(p)[0], (p)[3], (p)[6], (p)[ 9],  (p)[12], (p)[15], (p)[18], (p)[21]}
    #define LOAD_4(T, p) (T){(p)[0], (p)[4], (p)[8], (p)[12],  (p)[16], (p)[20], (p)[24], (p)[28]}
    #define STORE_3(p, v) for (int k = 0; k < N; k++) (p)[3*k] = v[k]
    #define STORE_4(p, v) for (int k = 0; k < N; k++) (p)[4*k] = v[k]
#endif

static void load_888_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    const uint8_t* rgb = (const uint8_t*)(src + 3*i);
    r = CAST(F, LOAD_3(U32, rgb+0) ) * (1/255.0f);
    g = CAST(F, LOAD_3(U32, rgb+1) ) * (1/255.0f);
    b = CAST(F, LOAD_3(U32, rgb+2) ) * (1/255.0f);
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_888_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    memcpy(tmp, src + 3*i, 3);
    src = tmp - 3*i;
    load_888_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_8888_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    U32 rgba;
    memcpy(&rgba, src + 4*i, 4*N);

    r = CAST(F, (rgba >>  0) & 0xff) * (1/255.0f);
    g = CAST(F, (rgba >>  8) & 0xff) * (1/255.0f);
    b = CAST(F, (rgba >> 16) & 0xff) * (1/255.0f);
    a = CAST(F, (rgba >> 24) & 0xff) * (1/255.0f);
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_8888_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    memcpy(tmp, src + 4*i, 4);
    src = tmp - 4*i;
    load_8888_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_1010102_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                           F r, F g, F b, F a) {
    U32 rgba;
    memcpy(&rgba, src + 4*i, 4*N);

    r = CAST(F, (rgba >>  0) & 0x3ff) * (1/1023.0f);
    g = CAST(F, (rgba >> 10) & 0x3ff) * (1/1023.0f);
    b = CAST(F, (rgba >> 20) & 0x3ff) * (1/1023.0f);
    a = CAST(F, (rgba >> 30) & 0x3  ) * (1/   3.0f);
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_1010102_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    memcpy(tmp, src + 4*i, 4);
    src = tmp - 4*i;
    load_1010102_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_161616_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                          F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(src + 6*i);
    assert( (ptr & 1) == 0 );                   // The src pointer must be 2-byte aligned
    const uint16_t* rgb = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.
    U32 R = LOAD_3(U32, rgb+0),
        G = LOAD_3(U32, rgb+1),
        B = LOAD_3(U32, rgb+2);
    // R,G,B are big-endian 16-bit, so byte swap them before converting to float.
    r = CAST(F, (R & 0x00ff)<<8 | (R & 0xff00)>>8) * (1/65535.0f);
    g = CAST(F, (G & 0x00ff)<<8 | (G & 0xff00)>>8) * (1/65535.0f);
    b = CAST(F, (B & 0x00ff)<<8 | (B & 0xff00)>>8) * (1/65535.0f);
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_161616_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                          F r, F g, F b, F a) {
    memcpy(tmp, src + 6*i, 6);
    src = tmp - 6*i;
    load_161616_N(i,ip,dst,src,tmp, r,g,b,a);
}

// Swap high and low bytes of 16-bit lanes, converting between big-endian and little-endian.
static U64 swap_endian_16bit(U64 rgba) {
    return (rgba & 0x00ff00ff00ff00ff) << 8
         | (rgba & 0xff00ff00ff00ff00) >> 8;
}

static void load_16161616_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    U64 rgba;
    memcpy(&rgba, src + 8*i, 8*N);

    rgba = swap_endian_16bit(rgba);
    r = CAST(F, (rgba >>  0) & 0xffff) * (1/65535.0f);
    g = CAST(F, (rgba >> 16) & 0xffff) * (1/65535.0f);
    b = CAST(F, (rgba >> 32) & 0xffff) * (1/65535.0f);
    a = CAST(F, (rgba >> 48) & 0xffff) * (1/65535.0f);
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_16161616_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    memcpy(tmp, src + 8*i, 8);
    src = tmp - 8*i;
    load_16161616_N(i,ip,dst,src,tmp, r,g,b,a);
}

static F if_then_else(I32 cond, F t, F e) {
#if N == 1
    return cond ? t : e;
#else
    return (F)( (cond & (I32)t) | (~cond & (I32)e) );
#endif
}

static F F_from_Half(U32 half) {
    // A half is 1-5-10 sign-exponent-mantissa, with 15 exponent bias.
    U32 s  = half & 0x8000,
        em = half ^ s;

    // Constructing the float is easy if the half is not denormalized.
    U32 norm_bits = (s<<16) + (em<<13) + ((127-15)<<23);
    F norm;
    memcpy(&norm, &norm_bits, sizeof(norm));

    // Simply flush all denorm half floats to zero.
    return if_then_else(em < 0x0400, F0, norm);
}

static void load_hhh_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(src + 6*i);
    assert( (ptr & 1) == 0 );                   // The src pointer must be 2-byte aligned
    const uint16_t* rgb = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.

    r = F_from_Half( LOAD_3(U32, rgb+0) );
    g = F_from_Half( LOAD_3(U32, rgb+1) );
    b = F_from_Half( LOAD_3(U32, rgb+2) );
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_hhh_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    memcpy(tmp, src + 6*i, 6);
    src = tmp - 6*i;
    load_hhh_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_hhhh_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    U64 rgba;
    memcpy(&rgba, src + 8*i, 8*N);

    r = F_from_Half( CAST(U32, (rgba >>  0) & 0xffff) );
    g = F_from_Half( CAST(U32, (rgba >> 16) & 0xffff) );
    b = F_from_Half( CAST(U32, (rgba >> 32) & 0xffff) );
    a = F_from_Half( CAST(U32, (rgba >> 48) & 0xffff) );
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_hhhh_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    memcpy(tmp, src + 8*i, 8);
    src = tmp - 8*i;
    load_hhhh_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_fff_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(src + 12*i);
    assert( (ptr & 3) == 0 );                   // The src pointer must be 4-byte aligned
    const float* rgb = (const float*)ptr;       // for this cast to const float* to be safe.
    r = LOAD_3(F, rgb+0);
    g = LOAD_3(F, rgb+1);
    b = LOAD_3(F, rgb+2);
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_fff_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    memcpy(tmp, src + 12*i, 12);
    src = tmp - 12*i;
    load_fff_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_ffff_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(src + 16*i);
    assert( (ptr & 3) == 0 );                   // The src pointer must be 4-byte aligned
    const float* rgba = (const float*)ptr;      // for this cast to const float* to be safe.
    r = LOAD_4(F, rgba+0);
    g = LOAD_4(F, rgba+1);
    b = LOAD_4(F, rgba+2);
    a = LOAD_4(F, rgba+3);
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_ffff_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    memcpy(tmp, src + 16*i, 16);
    src = tmp - 16*i;
    load_ffff_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void store_565_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    U16 rgb = CAST(U16, to_fixed(r * 31) <<  0 )
            | CAST(U16, to_fixed(g * 63) <<  5 )
            | CAST(U16, to_fixed(b * 31) << 11 );
    memcpy(dst + 2*i, &rgb, 2*N);
    (void)a;
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_565_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    store_565_N(i,ip,tmp - 2*i,src,tmp, r,g,b,a);
    memcpy(dst + 2*i, tmp, 2);
}

static void store_888_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    uint8_t* rgb = (uint8_t*)dst + 3*i;
    STORE_3(rgb+0, CAST(U8, to_fixed(r * 255)) );
    STORE_3(rgb+1, CAST(U8, to_fixed(g * 255)) );
    STORE_3(rgb+2, CAST(U8, to_fixed(b * 255)) );
    (void)a;
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_888_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    store_888_N(i,ip,tmp - 3*i,src,tmp, r,g,b,a);
    memcpy(dst + 3*i, tmp, 3);
}

static void store_8888_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                         F r, F g, F b, F a) {
    U32 rgba = CAST(U32, to_fixed(r * 255) <<  0)
             | CAST(U32, to_fixed(g * 255) <<  8)
             | CAST(U32, to_fixed(b * 255) << 16)
             | CAST(U32, to_fixed(a * 255) << 24);
    memcpy(dst + 4*i, &rgba, 4*N);
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_8888_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                         F r, F g, F b, F a) {
    store_8888_N(i,ip,tmp - 4*i,src,tmp, r,g,b,a);
    memcpy(dst + 4*i, tmp, 4);
}

static void store_1010102_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    U32 rgba = CAST(U32, to_fixed(r * 1023) <<  0)
             | CAST(U32, to_fixed(g * 1023) << 10)
             | CAST(U32, to_fixed(b * 1023) << 20)
             | CAST(U32, to_fixed(a *    3) << 30);
    memcpy(dst + 4*i, &rgba, 4*N);
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_1010102_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    store_1010102_N(i,ip,tmp - 4*i,src,tmp, r,g,b,a);
    memcpy(dst + 4*i, tmp, 4);
}

static void store_161616_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                           F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(dst + 6*i);
    assert( (ptr & 1) == 0 );                   // The dst pointer must be 2-byte aligned
    uint16_t* rgb = (uint16_t*)ptr;             // for this cast to uint16_t* to be safe.

    I32 R = to_fixed(r * 65535),
        G = to_fixed(g * 65535),
        B = to_fixed(b * 65535);
    STORE_3(rgb+0, CAST(U16, (R & 0x00ff) << 8 | (R & 0xff00) >> 8) );
    STORE_3(rgb+1, CAST(U16, (G & 0x00ff) << 8 | (G & 0xff00) >> 8) );
    STORE_3(rgb+2, CAST(U16, (B & 0x00ff) << 8 | (B & 0xff00) >> 8) );
    (void)a;
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_161616_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                           F r, F g, F b, F a) {
    store_161616_N(i,ip,tmp - 6*i,src,tmp, r,g,b,a);
    memcpy(dst + 6*i, tmp, 6);
}

static void store_16161616_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                             F r, F g, F b, F a) {
    U64 rgba = CAST(U64, to_fixed(r * 65535)) <<  0
             | CAST(U64, to_fixed(g * 65535)) << 16
             | CAST(U64, to_fixed(b * 65535)) << 32
             | CAST(U64, to_fixed(a * 65535)) << 48;
    rgba = swap_endian_16bit(rgba);
    memcpy(dst + 8*i, &rgba, 8*N);
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_16161616_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                             F r, F g, F b, F a) {
    store_16161616_N(i,ip,tmp - 8*i,src,tmp, r,g,b,a);
    memcpy(dst + 8*i, tmp, 8);
}

// TODO: store_hhh_N,1
// TODO: store_hhhh_N,1

static void store_fff_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(dst + 12*i);
    assert( (ptr & 3) == 0 );                   // The dst pointer must be 4-byte aligned
    float* rgb = (float*)ptr;                   // for this cast to float* to be safe.
    STORE_4(rgb+0, r);
    STORE_4(rgb+1, g);
    STORE_4(rgb+2, b);
    (void)a;
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_fff_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    store_fff_N(i,ip,tmp - 12*i,src,tmp, r,g,b,a);
    memcpy(dst + 12*i, tmp, 12);
}

static void store_ffff_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                         F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(dst + 16*i);
    assert( (ptr & 3) == 0 );                   // The dst pointer must be 4-byte aligned
    float* rgba = (float*)ptr;                  // for this cast to float* to be safe.
    STORE_4(rgba+0, r);
    STORE_4(rgba+1, g);
    STORE_4(rgba+2, b);
    STORE_4(rgba+3, a);
    (void)ip;
    (void)src;
    (void)tmp;
}
static void store_ffff_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                         F r, F g, F b, F a) {
    store_ffff_N(i,ip,tmp - 16*i,src,tmp, r,g,b,a);
    memcpy(dst + 16*i, tmp, 16);
}

static void swap_rb(size_t i, void** ip, char* dst, const char* src, char* tmp,
                    F r, F g, F b, F a) {
    next_stage(i,ip,dst,src,tmp, b,g,r,a);
}

static F min(F x, F y) { return if_then_else(x > y, y, x); }
static F max(F x, F y) { return if_then_else(x < y, y, x); }

static void clamp(size_t i, void** ip, char* dst, const char* src, char* tmp,
                  F r, F g, F b, F a) {
    r = max(F0, min(r, F1));
    g = max(F0, min(g, F1));
    b = max(F0, min(b, F1));
    a = max(F0, min(a, F1));
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}

static void force_opaque(size_t i, void** ip, char* dst, const char* src, char* tmp,
                         F r, F g, F b, F a) {
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}

bool skcms_Transform(void* dst, skcms_PixelFormat dstFmt, const skcms_ICCProfile* dstProfile,
               const void* src, skcms_PixelFormat srcFmt, const skcms_ICCProfile* srcProfile,
                     size_t n) {
    // We can't transform in place unless the PixelFormats are the same size.
    if (dst == src && (dstFmt >> 1) != (srcFmt >> 1)) {
        return false;
    }
    // TODO: this check lazilly disallows U16 <-> F16, but that would actually be fine.
    // TODO: more careful alias rejection (like, dst == src + 1)?

    void* program_N[32];
    void* program_1[32];

    void** ip_N = program_N;
    void** ip_1 = program_1;

    switch (srcFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGB_565       >> 1: *ip_N++ = (void*)load_565_N;
                                                   *ip_1++ = (void*)load_565_1;
                                                   break;
        case skcms_PixelFormat_RGB_888       >> 1: *ip_N++ = (void*)load_888_N;
                                                   *ip_1++ = (void*)load_888_1;
                                                   break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ip_N++ = (void*)load_8888_N;
                                                   *ip_1++ = (void*)load_8888_1;
                                                   break;
        case skcms_PixelFormat_RGB_101010x   >> 1: *ip_N++ = (void*)load_1010102_N;
                                                   *ip_N++ = (void*)force_opaque;
                                                   *ip_1++ = (void*)load_1010102_1;
                                                   *ip_1++ = (void*)force_opaque;
                                                   break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ip_N++ = (void*)load_1010102_N;
                                                   *ip_1++ = (void*)load_1010102_1;
                                                   break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ip_N++ = (void*)load_161616_N;
                                                   *ip_1++ = (void*)load_161616_1;
                                                   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ip_N++ = (void*)load_16161616_N;
                                                   *ip_1++ = (void*)load_16161616_1;
                                                   break;
        case skcms_PixelFormat_RGB_hhh       >> 1: *ip_N++ = (void*)load_hhh_N;
                                                   *ip_1++ = (void*)load_hhh_1;
                                                   break;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: *ip_N++ = (void*)load_hhhh_N;
                                                   *ip_1++ = (void*)load_hhhh_1;
                                                   break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ip_N++ = (void*)load_fff_N;
                                                   *ip_1++ = (void*)load_fff_1;
                                                   break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ip_N++ = (void*)load_ffff_N;
                                                   *ip_1++ = (void*)load_ffff_1;
                                                   break;
    }
    if (srcFmt & 1) {
        *ip_N++ = (void*)swap_rb;
        *ip_1++ = (void*)swap_rb;
    }

    if (dstProfile != srcProfile) {
        // TODO: color space conversions, of course.
        return false;
    }

    if (dstFmt & 1) {
        *ip_N++ = (void*)swap_rb;
        *ip_1++ = (void*)swap_rb;
    }
    if (dstFmt < skcms_PixelFormat_RGB_hhh) {
        *ip_N++ = (void*)clamp;
        *ip_1++ = (void*)clamp;
    }
    switch (dstFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGB_565       >> 1: *ip_N++ = (void*)store_565_N;
                                                   *ip_1++ = (void*)store_565_1;
                                                   break;
        case skcms_PixelFormat_RGB_888       >> 1: *ip_N++ = (void*)store_888_N;
                                                   *ip_1++ = (void*)store_888_1;
                                                   break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ip_N++ = (void*)store_8888_N;
                                                   *ip_1++ = (void*)store_8888_1;
                                                   break;
        case skcms_PixelFormat_RGB_101010x   >> 1: *ip_N++ = (void*)force_opaque;
                                                   *ip_N++ = (void*)store_1010102_N;
                                                   *ip_1++ = (void*)force_opaque;
                                                   *ip_1++ = (void*)store_1010102_1;
                                                   break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ip_N++ = (void*)store_1010102_N;
                                                   *ip_1++ = (void*)store_1010102_1;
                                                   break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ip_N++ = (void*)store_161616_N;
                                                   *ip_1++ = (void*)store_161616_1;
                                                   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ip_N++ = (void*)store_16161616_N;
                                                   *ip_1++ = (void*)store_16161616_1;
                                                   break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ip_N++ = (void*)store_fff_N;
                                                   *ip_1++ = (void*)store_fff_1;
                                                   break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ip_N++ = (void*)store_ffff_N;
                                                   *ip_1++ = (void*)store_ffff_1;
                                                   break;
    }

    // Big enough to hold any of our skcms_PixelFormats (the largest is 4x 4-byte float.)
    char tmp[16*N] = {0};

    size_t i = 0;
    while (n >= N) {
        Stage start = (Stage)program_N[0];
        start(i,program_N+1,dst,src,tmp, F0,F0,F0,F0);
        i += N;
        n -= N;
    }
    while (n > 0) {
        Stage start = (Stage)program_1[0];
        start(i,program_1+1,dst,src,tmp, F0,F0,F0,F0);
        i += 1;
        n -= 1;
    }
    return true;
}
