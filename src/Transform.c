/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#if defined(SKCMS_PORTABLE) || (!defined(__clang__) && !defined(__GNUC__))
    #define N 1
    typedef float    F  ;
    typedef int32_t  I32;
    typedef uint64_t U64;
    typedef uint32_t U32;
    typedef uint16_t U16;
    static const F F0 = 0,
                   F1 = 1;
#elif defined(__clang__) && defined(__AVX__)
    #define N 8
    typedef float    __attribute__((ext_vector_type(  N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(  N))) I32;
    typedef uint64_t __attribute__((ext_vector_type(  N))) U64;
    typedef uint32_t __attribute__((ext_vector_type(  N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(  N))) U16;
    static const F F0 = {0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1};
#elif defined(__GNUC__) && defined(__AVX__)
    #define N 8
    typedef float    __attribute__((vector_size(32))) F  ;
    typedef int32_t  __attribute__((vector_size(32))) I32;
    typedef uint64_t __attribute__((vector_size(64))) U64;
    typedef uint32_t __attribute__((vector_size(32))) U32;
    typedef uint16_t __attribute__((vector_size(16))) U16;
    static const F F0 = {0,0,0,0, 0,0,0,0},
                   F1 = {1,1,1,1, 1,1,1,1};
#elif defined(__clang__)
    #define N 4
    typedef float    __attribute__((ext_vector_type(  N))) F  ;
    typedef int32_t  __attribute__((ext_vector_type(  N))) I32;
    typedef uint64_t __attribute__((ext_vector_type(  N))) U64;
    typedef uint32_t __attribute__((ext_vector_type(  N))) U32;
    typedef uint16_t __attribute__((ext_vector_type(  N))) U16;
    static const F F0 = {0,0,0,0},
                   F1 = {1,1,1,1};
#elif defined(__GNUC__)
    #define N 4
    typedef float    __attribute__((vector_size(16))) F  ;
    typedef int32_t  __attribute__((vector_size(16))) I32;
    typedef uint64_t __attribute__((vector_size(32))) U64;
    typedef uint32_t __attribute__((vector_size(16))) U32;
    typedef uint16_t __attribute__((vector_size( 8))) U16;
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

#if N == 1
    static inline F F_from_U32(U32 u)     { return (F)u; }
    static inline U32 U32_from_F(F f)     { return (U32)f; }
    static inline U32 U32_from_U16(U16 h) { return (U16)h; }
    static inline U32 U32_from_U64(U64 w) { return (U32)w; }
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
    static inline U32 U32_from_U64(U64 w) {
        U32 u = {(uint32_t)w[0],(uint32_t)w[1],(uint32_t)w[2],(uint32_t)w[3]};
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
    static inline U32 U32_from_U64(U64 w) {
        U32 u = {(uint32_t)w[0],(uint32_t)w[1],(uint32_t)w[2],(uint32_t)w[3],
                 (uint32_t)w[4],(uint32_t)w[5],(uint32_t)w[6],(uint32_t)w[7]};
        return u;
    }
#endif

static void load_565_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    U16 rgb;
    memcpy(&rgb, src + 2*i, 2*N);

    U32 wide = U32_from_U16(rgb);
    r = F_from_U32(wide & (31<< 0)) * (1.0f / (31<< 0));
    g = F_from_U32(wide & (63<< 5)) * (1.0f / (63<< 5));
    b = F_from_U32(wide & (31<<11)) * (1.0f / (31<<11));
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_565_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    memcpy(tmp, src + 2*i, 2);
    src = tmp - 2*i;
    load_565_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_888_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    const uint8_t* rgb = (const uint8_t*)(src + 3*i);
#if N == 1
    U32 R = rgb[0],
        G = rgb[1],
        B = rgb[2];
#elif N == 4
    U32 R = { rgb[0], rgb[3], rgb[6], rgb[ 9] },
        G = { rgb[1], rgb[4], rgb[7], rgb[10] },
        B = { rgb[2], rgb[5], rgb[8], rgb[11] };
#elif N == 8
    U32 R = { rgb[0], rgb[3], rgb[6], rgb[ 9],  rgb[12], rgb[15], rgb[18], rgb[21] },
        G = { rgb[1], rgb[4], rgb[7], rgb[10],  rgb[13], rgb[16], rgb[19], rgb[22] },
        B = { rgb[2], rgb[5], rgb[8], rgb[11],  rgb[14], rgb[17], rgb[20], rgb[23] };
#endif
    r = F_from_U32(R) * (1/255.0f);
    g = F_from_U32(G) * (1/255.0f);
    b = F_from_U32(B) * (1/255.0f);
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

    r = F_from_U32((rgba >>  0) & 0xff) * (1/255.0f);
    g = F_from_U32((rgba >>  8) & 0xff) * (1/255.0f);
    b = F_from_U32((rgba >> 16) & 0xff) * (1/255.0f);
    a = F_from_U32((rgba >> 24) & 0xff) * (1/255.0f);
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_8888_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                        F r, F g, F b, F a) {
    memcpy(tmp, src + 4*i, 4);
    src = tmp - 4*i;
    load_8888_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_161616_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                          F r, F g, F b, F a) {
    uintptr_t ptr = (uintptr_t)(src + 6*i);
    assert( (ptr & 1) == 0 );                   // The src pointer must be 2-byte aligned
    const uint16_t* rgb = (const uint16_t*)ptr; // for this cast to const uint16_t* to be safe.
#if N == 1
    U32 R = rgb[0],
        G = rgb[1],
        B = rgb[2];
#elif N == 4
    U32 R = { rgb[0], rgb[3], rgb[6], rgb[ 9] },
        G = { rgb[1], rgb[4], rgb[7], rgb[10] },
        B = { rgb[2], rgb[5], rgb[8], rgb[11] };
#elif N == 8
    U32 R = { rgb[0], rgb[3], rgb[6], rgb[ 9],  rgb[12], rgb[15], rgb[18], rgb[21] },
        G = { rgb[1], rgb[4], rgb[7], rgb[10],  rgb[13], rgb[16], rgb[19], rgb[22] },
        B = { rgb[2], rgb[5], rgb[8], rgb[11],  rgb[14], rgb[17], rgb[20], rgb[23] };
#endif
    // R,G,B are big-endian 16-bit, so byte swap them before converting to float.
    r = F_from_U32( (R & 0x00ff)<<8 | (R & 0xff00)>>8 ) * (1/65535.0f);
    g = F_from_U32( (G & 0x00ff)<<8 | (G & 0xff00)>>8 ) * (1/65535.0f);
    b = F_from_U32( (B & 0x00ff)<<8 | (B & 0xff00)>>8 ) * (1/65535.0f);
    a = F1;
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_161616_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                       F r, F g, F b, F a) {
    memcpy(tmp, src + 6*i, 6);
    src = tmp - 6*i;
    load_161616_N(i,ip,dst,src,tmp, r,g,b,a);
}

static void load_16161616_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    U64 rgba;
    memcpy(&rgba, src + 8*i, 8*N);

    // Swap high and low bytes of 16-bit lanes, converting big-endian to little-endian.
    rgba = (rgba & 0x00ff00ff00ff00ff) << 8
         | (rgba & 0xff00ff00ff00ff00) >> 8;

    r = F_from_U32( U32_from_U64((rgba >>  0) & 0xffff) ) * (1/65535.0f);
    g = F_from_U32( U32_from_U64((rgba >> 16) & 0xffff) ) * (1/65535.0f);
    b = F_from_U32( U32_from_U64((rgba >> 32) & 0xffff) ) * (1/65535.0f);
    a = F_from_U32( U32_from_U64((rgba >> 48) & 0xffff) ) * (1/65535.0f);
    next_stage(i,ip,dst,src,tmp, r,g,b,a);
}
static void load_16161616_1(size_t i, void** ip, char* dst, const char* src, char* tmp,
                            F r, F g, F b, F a) {
    memcpy(tmp, src + 8*i, 8);
    src = tmp - 8*i;
    load_16161616_N(i,ip,dst,src,tmp, r,g,b,a);
}

//TODO: load_hhhh_N
//TODO: load_hhhh_1
//
//TODO: load_ffff_N
//TODO: load_ffff_1

static void store_8888_N(size_t i, void** ip, char* dst, const char* src, char* tmp,
                         F r, F g, F b, F a) {
    U32 rgba = U32_from_F(r * 255 + 0.5f) <<  0
             | U32_from_F(g * 255 + 0.5f) <<  8
             | U32_from_F(b * 255 + 0.5f) << 16
             | U32_from_F(a * 255 + 0.5f) << 24;
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

static void swap_rb(size_t i, void** ip, char* dst, const char* src, char* tmp,
                    F r, F g, F b, F a) {
    next_stage(i,ip,dst,src,tmp, b,g,r,a);
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
        case skcms_PixelFormat_RGB_161616    >> 1: *ip_N++ = (void*)load_161616_N;
                                                   *ip_1++ = (void*)load_161616_1;
                                                   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ip_N++ = (void*)load_16161616_N;
                                                   *ip_1++ = (void*)load_16161616_1;
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
        case skcms_PixelFormat_RGBA_8888 >> 1: *ip_N++ = (void*)store_8888_N;
                                               *ip_1++ = (void*)store_8888_1;
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
