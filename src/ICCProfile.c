/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <math.h>

#define skcms_make_tag(a, b, c, d)  (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

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
    return read_big_i32(ptr) / 65536.0f;
}

static bool almost_equal(float a, float b) {
    return fabsf(a - b) < 0.01f;
}

typedef struct {
    uint8_t Size[4];
    uint8_t CMMType[4];
    uint8_t Version[4];
    uint8_t ProfileClass[4];
    uint8_t DataColorSpace[4];
    uint8_t PCS[4];
    uint8_t CreationDateTime[12];
    uint8_t Signature[4];
    uint8_t Platform[4];
    uint8_t Flags[4];
    uint8_t DeviceManufacturer[4];
    uint8_t DeviceModel[4];
    uint8_t DeviceAttributes[8];
    uint8_t RenderingIntent[4];
    uint8_t IlluminantX[4];
    uint8_t IlluminantY[4];
    uint8_t IlluminantZ[4];
    uint8_t Creator[4];
    uint8_t ProfileId[16];
    uint8_t Reserved[28];

    // Tag count is not technically part of the header, but is required for a valid profile
    uint8_t tagCount[4];
} skcms_ICCHeader;

bool skcms_ICCProfile_parse(skcms_ICCProfile* profile,
                            const void* buf,
                            size_t len) {
    assert(sizeof(skcms_ICCHeader) == 132);

    if (!profile) {
        return false;
    }
    profile->buf = 0;
    profile->len = 0;

    if (len < sizeof(skcms_ICCHeader)) {
        return false;
    }

    const skcms_ICCHeader* header = buf;
    // Validate signature, size, and major version
    if (read_big_u32(header->Signature) != skcms_make_tag('a', 'c', 's', 'p') ||
        read_big_u32(header->Size) < len ||
        (read_big_u32(header->Version) >> 24) > 4) {
        return false;
    }

    if (!almost_equal(read_big_fixed(header->IlluminantX), 0.9642f) ||
        !almost_equal(read_big_fixed(header->IlluminantY), 1.0f) ||
        !almost_equal(read_big_fixed(header->IlluminantZ), 0.8249f)) {
        return false;
    }

    // TODO: Validate tag count and table make sense (including last offset + size
    // being smaller than header->size).

    profile->buf = buf;
    profile->len = read_big_u32(header->Size);
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

#define MAKE_HEADER_GETTER_U32(field) \
    uint32_t skcms_ICCProfile_get ## field (const skcms_ICCProfile* profile) { \
        if (!profile || !profile->buf || !profile->len) { return 0; }         \
        return read_big_u32(((const skcms_ICCHeader*)profile->buf)-> field);  \
    }

MAKE_HEADER_GETTER_U32(Size)
MAKE_HEADER_GETTER_U32(CMMType)
MAKE_HEADER_GETTER_U32(Version)
MAKE_HEADER_GETTER_U32(ProfileClass)
MAKE_HEADER_GETTER_U32(DataColorSpace)
MAKE_HEADER_GETTER_U32(PCS)
MAKE_HEADER_GETTER_U32(Platform)
MAKE_HEADER_GETTER_U32(Flags)
MAKE_HEADER_GETTER_U32(DeviceManufacturer)
MAKE_HEADER_GETTER_U32(DeviceModel)
MAKE_HEADER_GETTER_U32(RenderingIntent)
MAKE_HEADER_GETTER_U32(Creator)

#undef MAKE_HEADER_GETTER_U32

uint64_t skcms_ICCProfile_getDeviceAttributes(const skcms_ICCProfile* profile) {
    if (!profile || !profile->buf || !profile->len) { return 0; }
    return read_big_u64(((const skcms_ICCHeader*)profile->buf)->DeviceAttributes);
}
