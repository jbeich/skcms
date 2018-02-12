/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// A simple bench harness for skcms_Transform(), mostly to run in a profiler.

#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

#include "skcms.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define expect(cond) if (!(cond)) exit(1)

static void load_file(const char* filename, void** buf, size_t* len) {
    FILE* fp = fopen(filename, "rb");
    expect(fp);

    expect(fseek(fp, 0L, SEEK_END) == 0);
    long size = ftell(fp);
    expect(size > 0);
    *len = (size_t)size;
    rewind(fp);

    *buf = malloc(*len);
    expect(*buf);

    size_t bytes_read = fread(*buf, 1, *len, fp);
    expect(bytes_read == *len);
}

// Just to keep us on our toes, we transform a non-power-of-two number of pixels.
#define NPIXELS 255

static uint8_t src_pixels[NPIXELS * 4],
               dst_pixels[NPIXELS * 4];

int main(int argc, char** argv) {
    bool running_under_profiler = false;

    const char* XPC_SERVICE_NAME = getenv("XPC_SERVICE_NAME");
    if (XPC_SERVICE_NAME && NULL != strstr(XPC_SERVICE_NAME, "Instruments")) {
        running_under_profiler = true;
    }

    int loops = argc > 1 ? atoi(argv[1]) : 1e5;

    void  *src_buf, *dst_buf;
    size_t src_len,  dst_len;
    load_file(argc > 2 ? argv[2] : "profiles/mobile/sRGB_parametric.icc",       &src_buf, &src_len);
    load_file(argc > 3 ? argv[3] : "profiles/mobile/Display_P3_parametric.icc", &dst_buf, &dst_len);

    skcms_ICCProfile src_profile, dst_profile;
    if (!skcms_ICCProfile_parse(&src_profile, src_buf, src_len) ||
        !skcms_ICCProfile_parse(&dst_profile, dst_buf, dst_len)) {
        return 1;
    }

    clock_t start = clock();
    for (int i = 0; running_under_profiler || i < loops; i++) {
        (void)skcms_Transform(dst_pixels, skcms_PixelFormat_RGBA_8888, &dst_profile,
                              src_pixels, skcms_PixelFormat_RGBA_8888, &src_profile, NPIXELS);
    }

    clock_t ticks = clock() - start;
    printf("%d loops in %g ticks, %.3g ns / pixel\n",
            loops, (double)ticks, (double)ticks / (CLOCKS_PER_SEC * 1e-9) / (loops * NPIXELS));

    free(src_buf);
    free(dst_buf);

    return 0;
}
