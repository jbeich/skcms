/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#if 0 || (!defined(__clang__) && !defined(__GNUC__))
    #define N 1
    #include <string.h>
    typedef float    F  ;
    typedef int32_t  I32;
    typedef uint32_t U32;
    typedef uint16_t U16;
    static const F F0 = 0,
                   F1 = 1;
#elif defined(__clang__) && defined(__AVX__)
    #define N 8
    #define memcpy __builtin_memcpy
    #define shuffle(v, ...) __builtin_shufflevector(v,v, __VA_ARGS__)
    typedef uint8_t  __attribute__((ext_vector_type(4*N))) RawBytes;
    typedef float    __attribute__((ext_vector_type(  N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(  N))) I32;
    typedef uint32_t __attribute__((ext_vector_type(  N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(  N))) U16;
    static const F F0 = {0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1};
#elif defined(__GNUC__) && defined(__AVX__)
    #define N 8
    #define memcpy __builtin_memcpy
    #define shuffle(v, ...) __builtin_shuffle(v, (RawBytes){__VA_ARGS__})
    typedef uint8_t  __attribute__((vector_size(32))) RawBytes;
    typedef float    __attribute__((vector_size(32))) F  ;
    typedef int32_t  __attribute__((vector_size(32))) I32;
    typedef uint32_t __attribute__((vector_size(32))) U32;
    typedef uint16_t __attribute__((vector_size(16))) U16;
    static const F F0 = {0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1};
#elif defined(__clang__)
    #define N 4
    #define memcpy __builtin_memcpy
    #define shuffle(v, ...) __builtin_shufflevector(v,v, __VA_ARGS__)
    typedef uint8_t  __attribute__((ext_vector_type(4*N))) RawBytes;
    typedef float    __attribute__((ext_vector_type(  N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(  N))) I32;
    typedef uint32_t __attribute__((ext_vector_type(  N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(  N))) U16;
    static const F F0 = {0,0,0,0},
                   F1 = {1,1,1,1};
#elif defined(__GNUC__)
    #define N 4
    #define memcpy __builtin_memcpy
    #define shuffle(v, ...) __builtin_shuffle(v, (RawBytes){__VA_ARGS__})
    typedef uint8_t  __attribute__((vector_size(16))) RawBytes;
    typedef float    __attribute__((vector_size(16))) F  ;
    typedef int32_t  __attribute__((vector_size(16))) I32;
    typedef uint32_t __attribute__((vector_size(16))) U32;
    typedef uint16_t __attribute__((vector_size(8)))  U16;
    static const F F0 = {0,0,0,0},
                   F1 = {1,1,1,1};
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

#if N == 1
    static inline F F_from_U32(U32 u)     { return (F)u; }
    static inline U32 U32_from_F(F f)     { return (U32)f; }
    static inline U32 U32_from_U16(U16 h) { return (U16)h; }
#elif N == 4
    static inline F F_from_U32(U32 u) {
        I32 i = (I32)u;
        F f = {(float)i[0], (float)i[1], (float)i[2], (float)i[3]};
        return f;
    }
    static inline U32 U32_from_F(F f) {
        I32 i = {(int)f[0], (int)f[1], (int)f[2], (int)f[3]};
        return (U32)i;
    }
    static inline U32 U32_from_U16(U16 h) {
        U32 u = {h[0],h[1],h[2],h[3]};
        return u;
    }
#elif N == 8
    static inline F F_from_U32(U32 u) {
        I32 i = (I32)u;
        F f = {(float)i[0], (float)i[1], (float)i[2], (float)i[3],
               (float)i[4], (float)i[5], (float)i[6], (float)i[7]};
        return f;
    }
    static inline U32 U32_from_F(F f) {
        I32 i = {(int)f[0], (int)f[1], (int)f[2], (int)f[3],
                 (int)f[4], (int)f[5], (int)f[6], (int)f[7]};
        return (U32)i;
    }
    static inline U32 U32_from_U16(U16 h) {
        U32 u = {h[0],h[1],h[2],h[3], h[4],h[5],h[6],h[7]};
        return u;
    }
#endif

static void load_2(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = F0;
    memcpy(&r, src + 2*i, 2);
    next_stage(i,ip,dst,src, r,g,b,a);
}
static void load_2N(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = F0;
    memcpy(&r, src + 2*i, 2*N);
    next_stage(i,ip,dst,src, r,g,b,a);
}

static void load_3(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = F0;
    memcpy(&r, src + 3*i, 3);
    next_stage(i,ip,dst,src, r,g,b,a);
}
static void load_3N(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = F0;
    memcpy(&r, src + 3*i, 3*N);
    next_stage(i,ip,dst,src, r,g,b,a);
}

static void load_4(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = F0;
    memcpy(&r, src + 4*i, 4);
    next_stage(i,ip,dst,src, r,g,b,a);
}
static void load_4N(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    r = F0;
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

static void from_565(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
    U16 rgb;
    memcpy(&rgb, &r, sizeof(rgb));

    U32 wide = U32_from_U16(rgb);
    r = F_from_U32(wide & (31<< 0)) * (1.0f / (31<< 0));
    g = F_from_U32(wide & (63<< 5)) * (1.0f / (63<< 5));
    b = F_from_U32(wide & (31<<11)) * (1.0f / (31<<11));
    a = F1;
    next_stage(i,ip,dst,src, r,g,b,a);
}

static void from_888(size_t i, void** ip, char* dst, const char* src, F r, F g, F b, F a) {
#if N == 1
    U32 rgb;
    memcpy(&rgb, &r, sizeof(rgb));
    r = F_from_U32((rgb >>  0) & 0xff) * (1/255.0f);
    g = F_from_U32((rgb >>  8) & 0xff) * (1/255.0f);
    b = F_from_U32((rgb >> 16) & 0xff) * (1/255.0f);
#elif N == 4
    RawBytes rgb;
    memcpy(&rgb, &r, sizeof(rgb));
    #define _ 15,15,15  // Lanes 24-31 are zero bytes.  Any will do.
    r = F_from_U32( (U32)shuffle(rgb, 0,_, 3,_, 6,_,  9,_) ) * (1/255.0f);
    g = F_from_U32( (U32)shuffle(rgb, 1,_, 4,_, 7,_, 10,_) ) * (1/255.0f);
    b = F_from_U32( (U32)shuffle(rgb, 2,_, 5,_, 8,_, 11,_) ) * (1/255.0f);
#elif N == 8
    RawBytes rgb;
    memcpy(&rgb, &r, sizeof(rgb));
    #define _ 31,31,31  // Lanes 24-31 are zero bytes.  Any will do.
    r = F_from_U32( (U32)shuffle(rgb, 0,_, 3,_, 6,_,  9,_, 12,_, 15,_, 18,_, 21,_) ) * (1/255.0f);
    g = F_from_U32( (U32)shuffle(rgb, 1,_, 4,_, 7,_, 10,_, 13,_, 16,_, 19,_, 22,_) ) * (1/255.0f);
    b = F_from_U32( (U32)shuffle(rgb, 2,_, 5,_, 8,_, 11,_, 14,_, 17,_, 20,_, 23,_) ) * (1/255.0f);
#endif

    a = F1;
    next_stage(i,ip,dst,src, r,g,b,a);
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
        case skcms_PixelFormat_RGB_565   >> 1: *ip_N++ = (void*)load_2N; *ip_N++ = (void*)from_565;
                                               *ip_1++ = (void*)load_2 ; *ip_1++ = (void*)from_565;
                                               break;
        case skcms_PixelFormat_RGB_888   >> 1: *ip_N++ = (void*)load_3N; *ip_N++ = (void*)from_888;
                                               *ip_1++ = (void*)load_3 ; *ip_1++ = (void*)from_888;
                                               break;
        case skcms_PixelFormat_RGBA_8888 >> 1: *ip_N++ = (void*)load_4N; *ip_N++ = (void*)from_8888;
                                               *ip_1++ = (void*)load_4 ; *ip_1++ = (void*)from_8888;
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
    switch (dstFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_RGBA_8888 >> 1: *ip_N++ = (void*)to_8888; *ip_N++ = (void*)store_4N;
                                               *ip_1++ = (void*)to_8888; *ip_1++ = (void*)store_4 ;
                                               break;
    }

    size_t i = 0;
    while (n >= N) {
        Stage start = (Stage)program_N[0];
        start(i,program_N+1,dst,src, F0,F0,F0,F0);
        i += N;
        n -= N;
    }
    while (n > 0) {
        Stage start = (Stage)program_1[0];
        start(i,program_1+1,dst,src, F0,F0,F0,F0);
        i += 1;
        n -= 1;
    }
    return true;
}
