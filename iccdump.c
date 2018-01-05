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
#include <string.h>

static void fatal(const char* msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}

const char* skcms_signature_as_string(uint32_t sig) {
    static char str[5];
    memcpy(str, &sig, 4);
    str[4] = 0;
    return str;
}
/*
bool skcms_ICCHeader_dump(skcms_ICCHeader* header) {
    printf("size: %u\n", header->size);
    printf("cmmType: %08X\n", header->cmmType);
    printf("version: %u (%u.%u.%u)\n", header->version, header->version >> 24,
        (header->version >> 20) & 0xf, (header->version >> 16) & 0xf);
    printf("class: %08X '%s'\n", header->profileClass, skcms_signature_as_string(header->profileClass));
    //    printf("data space: ")
    return false;
}
*/
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

#define DUMP_SIG_FIELD(field) do {                                            \
        uint32_t val = skcms_ICCProfile_get ## field(&profile);               \
        printf("%20s : 0x%08X : '%c%c%c%c'\n", #field, val,                   \
               val >> 24, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF); \
    } while(0)

#define DUMP_INT_FIELD(field) do {                                            \
        uint32_t val = skcms_ICCProfile_get ## field(&profile);               \
        printf("%20s : 0x%08X : %u\n", #field, val, val);                     \
    } while(0)

#define DUMP_VER_FIELD(field) do {                                            \
        uint32_t val = skcms_ICCProfile_get ## field(&profile);               \
        printf("%20s : 0x%08X : %u.%u.%u\n", #field, val,                     \
               val >> 24, (val >> 20) & 0xF, (val >> 16) & 0xF);              \
    } while(0)

    DUMP_INT_FIELD(Size);
    DUMP_SIG_FIELD(CMMType);
    DUMP_VER_FIELD(Version);
    DUMP_SIG_FIELD(ProfileClass);
    DUMP_SIG_FIELD(DataColorSpace);
    DUMP_SIG_FIELD(PCS);
    DUMP_SIG_FIELD(Platform);
    printf("%20s : 0x%08X\n", "Flags", skcms_ICCProfile_getFlags(&profile));
    DUMP_SIG_FIELD(DeviceManufacturer);
    DUMP_SIG_FIELD(DeviceModel);
    DUMP_INT_FIELD(RenderingIntent);
    DUMP_SIG_FIELD(Creator);

    return 0;
}
