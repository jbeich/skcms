/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "skcms.h"
#include <stdio.h>
#include <stdlib.h>

static void fatal(const char* msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: %s <ICC filename>\n", argv[0]);
        return 1;
    }

    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        fatal("Unable to open input file");
    }

    fseek(fp, 0L, SEEK_END);
    size_t len = ftell(fp);
    rewind(fp);

    void* buf = malloc(len);
    size_t bytesRead = fread(buf, 1, len, fp);
    fclose(fp);
    if (bytesRead != len) {
        fatal("Unable to read file");
    }

    skcms_ICCProfile profile;
    if (!skcms_ICCProfile_parse(&profile, buf, bytesRead)) {
        fatal("Unable to parse ICC profile");
    }

    return 0;
}
