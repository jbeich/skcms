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

static void dump_sig_field(const char* name, uint32_t val) {
    printf("%20s : 0x%08X : '%c%c%c%c'\n",
           name, val, val >> 24, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
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

    printf("%20s : 0x%08x : %u\n", "Size", profile.size, profile.size);
    dump_sig_field("CMM type", profile.cmm_type);
    printf("%20s : 0x%08X : %u.%u.%u\n", "Version", profile.version,
           profile.version >> 24, (profile.version >> 20) & 0xF, (profile.version >> 16) & 0xF);
    dump_sig_field("Profile class", profile.profile_class);
    dump_sig_field("Data color space", profile.data_color_space);
    dump_sig_field("PCS", profile.pcs);
    printf("%20s : %u-%02u-%02u %02u:%02u:%02u\n", "Creation date/time",
           profile.creation_date_time.year, profile.creation_date_time.month,
           profile.creation_date_time.day, profile.creation_date_time.hour,
           profile.creation_date_time.minute, profile.creation_date_time.second);
    dump_sig_field("Signature", profile.signature);
    dump_sig_field("Platform", profile.platform);
    printf("%20s : 0x%08X\n", "Flags", profile.flags);
    dump_sig_field("Device manufacturer", profile.device_manufacturer);
    dump_sig_field("Device model", profile.device_model);
    printf("%20s : 0x%08X\n", "Device attributes", (uint32_t)profile.device_attributes);
    printf("%20s : 0x%08X\n", "", (uint32_t)(profile.device_attributes >> 32));
    printf("%20s : 0x%08x : %u\n", "Rendering intent", profile.rendering_intent,
           profile.rendering_intent);
    printf("%20s : %f\n", "Illuminant X", profile.illuminant_X);
    printf("%20s : %f\n", "Illuminant Y", profile.illuminant_Y);
    printf("%20s : %f\n", "Illuminant Z", profile.illuminant_Z);
    dump_sig_field("Creator", profile.creator);

    return 0;
}
