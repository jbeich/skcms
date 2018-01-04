/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

bool skcms_Transform(void* dst, skcms_PixelFormat dstFmt, const skcms_ICCProfile* dstProfile,
               const void* src, skcms_PixelFormat srcFmt, const skcms_ICCProfile* srcProfile,
                     int npixels) {
    (void)dst;
    (void)dstFmt;
    (void)dstProfile;
    (void)src;
    (void)srcFmt;
    (void)srcProfile;
    (void)npixels;
    return false;
}

#if 0 || !defined(__clang__)
    #define N 1
    #include <string.h>
    typedef float    F  ;
    typedef int32_t  I32;
    typedef uint32_t U32;
#elif defined(__AVX__)
    #define N 8
    #define memcpy __builtin_memcpy
    typedef float    __attribute__((ext_vector_type(N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(N))) I32;
    typedef uint32_t __attribute__((ext_vector_type(N))) U32;
#else
    #define N 4
    #define memcpy __builtin_memcpy
    typedef float    __attribute__((ext_vector_type(N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(N))) I32;
    typedef uint32_t __attribute__((ext_vector_type(N))) U32;
#endif

typedef void (*Stage)(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a);

static void next_stage(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    Stage next;
#if defined(__x86_64__)
    __asm__("lodsq" : "=a"(next), "+S"(ip));
#else
    next = (Stage)*ip++;
#endif
    next(i,ip,dst,src, r,g,b,a);
}

#if N > 1
    static inline F F_from_U32(U32 v) { return __builtin_convertvector((I32)v, F); }
    static inline U32 U32_from_F(F v) { return (U32)__builtin_convertvector(v, I32); }
#else
    static inline F F_from_U32(U32 v) { return (F)v; }
    static inline U32 U32_from_F(F v) { return (U32)v; }
#endif

static void load_4(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = 0;
    memcpy(&r, src + 4*i, 4);
    next_stage(i,ip,dst,src, r,g,b,a);
}
static void load_4N(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = 0;
    memcpy(&r, src + 4*i, 4*N);
    next_stage(i,ip,dst,src, r,g,b,a);
}

static void store_4(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    memcpy(dst + 4*i, &r, 4);
    (void)ip; (void)src; (void)g; (void)b; (void)a;
}
static void store_4N(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    memcpy(dst + 4*i, &r, 4*N);
    (void)ip; (void)src; (void)g; (void)b; (void)a;
}

static void from_8888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    U32 rgba;
    memcpy(&rgba, &r, sizeof(rgba));

    r = F_from_U32((rgba >>  0) & 0xff) * (1/255.0f);
    g = F_from_U32((rgba >>  8) & 0xff) * (1/255.0f);
    b = F_from_U32((rgba >> 16) & 0xff) * (1/255.0f);
    a = F_from_U32((rgba >> 24) & 0xff) * (1/255.0f);
    next_stage(i,ip,dst,src, r,g,b,a);
}
static void to_8888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    U32 rgba = U32_from_F(r * 255 + 0.5f) <<  0
             | U32_from_F(g * 255 + 0.5f) <<  8
             | U32_from_F(b * 255 + 0.5f) << 16
             | U32_from_F(a * 255 + 0.5f) << 24;
    memcpy(&r, &rgba, sizeof(rgba));
    next_stage(i,ip,dst,src, r,g,b,a);
}

static void swap_rb(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    next_stage(i,ip,dst,src, b,g,r,a);
}


bool run_pipeline(void* dst, const void* src, size_t n,
                  skcms_PixelFormat srcFmt, skcms_PixelFormat dstFmt);
bool run_pipeline(void* dst, const void* src, size_t n,
                  skcms_PixelFormat srcFmt, skcms_PixelFormat dstFmt) {
    void* program_N[8];
    void* program_1[8];

    void** ip_N = program_N;
    void** ip_1 = program_1;
    switch (srcFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGBA_8888 >> 1: *ip_N++ = (void*)load_4N; *ip_N++ = (void*)from_8888;
                                               *ip_1++ = (void*)load_4 ; *ip_N++ = (void*)from_8888;
                                               break;
    }
    if (srcFmt & 1) {
        *ip_N++ = (void*)swap_rb;
        *ip_1++ = (void*)swap_rb;
    }

    //  ...  //

    if (dstFmt & 1) {
        *ip_N++ = (void*)swap_rb;
        *ip_1++ = (void*)swap_rb;
    }
    switch (dstFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGBA_8888 >> 1: *ip_N++ = (void*)to_8888; *ip_N++ = (void*)store_4N;
                                               *ip_1++ = (void*)to_8888; *ip_1++ = (void*)store_4 ;
                                               break;
    }

    size_t i = 0;
    while (n >= N) {
        Stage start = (Stage)program_N[0];
        start(i,program_N+1,dst,src, 0,0,0,0);
        i += N;
        n -= N;
    }
    while (n > 0) {
        Stage start = (Stage)program_1[0];
        start(i,program_1+1,dst,src, 0,0,0,0);
        i += 1;
        n -= 1;
    }
    return true;
}
