/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// This allows one defined fuzz target to work for both afl and libfuzzer
#ifdef IS_FUZZING_WITH_LIBFUZZER
    #define DEF_FUZZ_MAIN(data, size)                                  \
        int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
#else
    #include <stdio.h>
    #include <stdlib.h>

    #define DEF_FUZZ_MAIN(data, size)                                  \
        int main(int argc, char** argv) {                              \
            if (argc != 2) {                                           \
                printf("usage: %s <ICC filename>\n", argv[0]);         \
                return 1;                                              \
            }                                                          \
            FILE* fp = fopen(argv[1], "rb");                           \
            if (!fp) {                                                 \
                printf("Unable to open input file");                   \
                return 1;                                              \
            }                                                          \
            fseek(fp, 0L, SEEK_END);                                   \
            long slen = ftell(fp);                                     \
            if (slen <= 0) {                                           \
                printf("ftell failed");                                \
                return 1;                                              \
            }                                                          \
            size_t len = (size_t)slen;                                 \
            rewind(fp);                                                \
            void* data = malloc(len);                                  \
            size_t size = fread(data, 1, len, fp);                     \
            fclose(fp);                                                \
            if (size != len) {                                         \
                printf("Unable to read file");                         \
                return 1;                                              \
            }
#endif
