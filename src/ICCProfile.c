/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <math.h>
#include <string.h>

static uint32_t skcms_make_signature(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((a << 24) | (b << 16) | (c << 8) | d);
}

static uint16_t read_big_u16(const uint8_t* ptr) {
    return ptr[0] << 8 | ptr[1];
}

static uint32_t read_big_u32(const uint8_t* ptr) {
    return ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
}

static int32_t read_big_i32(const uint8_t* ptr) {
    return (int32_t)read_big_u32(ptr);
}

static uint64_t read_big_u64(const uint8_t* ptr) {
    uint64_t hi = read_big_u32(ptr);
    uint64_t lo = read_big_u32(ptr + 4);
    return hi << 32 | lo;
}

static float read_big_fixed(const uint8_t* ptr) {
    return read_big_i32(ptr) * (1.0f / 65536.0f);
}

static void read_big_date_time(const uint8_t* ptr, skcms_ICCDateTime* date_time) {
    date_time->year   = read_big_u16(ptr + 0);
    date_time->month  = read_big_u16(ptr + 2);
    date_time->day    = read_big_u16(ptr + 4);
    date_time->hour   = read_big_u16(ptr + 6);
    date_time->minute = read_big_u16(ptr + 8);
    date_time->second = read_big_u16(ptr + 10);
}

static bool equal_within_tolerance(float tol, float a, float b) {
    return fabsf(a - b) < tol;
}

// Maps to an in-memory profile so that fields line up to the locations specified
// in ICC.1:2010, section 7.2
typedef struct {
    uint8_t size                [ 4];
    uint8_t cmm_type            [ 4];
    uint8_t version             [ 4];
    uint8_t profile_class       [ 4];
    uint8_t data_color_space    [ 4];
    uint8_t pcs                 [ 4];
    uint8_t creation_date_time  [12];
    uint8_t signature           [ 4];
    uint8_t platform            [ 4];
    uint8_t flags               [ 4];
    uint8_t device_manufacturer [ 4];
    uint8_t device_model        [ 4];
    uint8_t device_attributes   [ 8];
    uint8_t rendering_intent    [ 4];
    uint8_t illuminant_X        [ 4];
    uint8_t illuminant_Y        [ 4];
    uint8_t illuminant_Z        [ 4];
    uint8_t creator             [ 4];
    uint8_t profile_id          [16];
    uint8_t reserved            [28];
    uint8_t tag_count           [ 4]; // Technically not part of header, but required
} skcms_ICCHeader;

bool skcms_ICCProfile_parse(skcms_ICCProfile* profile,
                            const void* buf,
                            size_t len) {
    assert(sizeof(skcms_ICCHeader) == 132);

    if (!profile) {
        return false;
    }
    memset(profile, 0, sizeof(skcms_ICCProfile));

    if (len < sizeof(skcms_ICCHeader)) {
        return false;
    }

    // Byte-swap all header fields
    const skcms_ICCHeader* header = buf;
    profile->buffer              = buf;
    profile->size                = read_big_u32(header->size);
    profile->cmm_type            = read_big_u32(header->cmm_type);
    profile->version             = read_big_u32(header->version);
    profile->profile_class       = read_big_u32(header->profile_class);
    profile->data_color_space    = read_big_u32(header->data_color_space);
    profile->pcs                 = read_big_u32(header->pcs);
    read_big_date_time(header->creation_date_time, &profile->creation_date_time);
    profile->signature           = read_big_u32(header->signature);
    profile->platform            = read_big_u32(header->platform);
    profile->flags               = read_big_u32(header->flags);
    profile->device_manufacturer = read_big_u32(header->device_manufacturer);
    profile->device_model        = read_big_u32(header->device_model);
    profile->device_attributes   = read_big_u64(header->device_attributes);
    profile->rendering_intent    = read_big_u32(header->rendering_intent);
    profile->illuminant_X        = read_big_fixed(header->illuminant_X);
    profile->illuminant_Y        = read_big_fixed(header->illuminant_Y);
    profile->illuminant_Z        = read_big_fixed(header->illuminant_Z);
    profile->creator             = read_big_u32(header->creator);
    profile->profile_id          = header->profile_id;
    profile->tag_count           = read_big_u32(header->tag_count);

    // Validate signature, size, and major version
    if (profile->signature != skcms_make_signature('a', 'c', 's', 'p') ||
        profile->size > len ||
        profile->size < sizeof(skcms_ICCHeader) + profile->tag_count * 12 ||
        (read_big_u32(header->version) >> 24) > 4) {
        return false;
    }

    // Validate that illuminant is D50 white
    if (!equal_within_tolerance(0.0100f, 0.9642f, profile->illuminant_X) ||
        !equal_within_tolerance(0.0100f, 1.0000f, profile->illuminant_Y) ||
        !equal_within_tolerance(0.0100f, 0.8249f, profile->illuminant_Z)) {
        return false;
    }

    // TODO: Validate tag count and table make sense (including last offset + size
    // being smaller than header->size).

    return true;
}

bool skcms_ICCProfile_toXYZD50(const skcms_ICCProfile* profile,
                               skcms_Matrix3x3* toXYZD50) {
    (void)profile;
    (void)toXYZD50;
    return false;
}

bool skcms_ICCProfile_getTransferFunction(const skcms_ICCProfile* profile,
                                          skcms_TransferFunction* transferFunction) {
    (void)profile;
    (void)transferFunction;
    return false;
}
