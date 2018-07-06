/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms.h"
#include "skcms_internal.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// sizeof(x) will return size_t, which is 32-bit on some machines and 64-bit on others.
// We have better testing on 64-bit machines, so force 32-bit machines to behave like 64-bit.
//
// Please do not use sizeof() directly, and size_t only when required.
// (We have no way of enforcing these requests...)
#define SAFE_SIZEOF(x) ((uint64_t)sizeof(x))

// Same sort of thing for _Layout structs with a variable sized array at the end (named "variable").
#define SAFE_FIXED_SIZE(type) ((uint64_t)offsetof(type, variable))

static const union {
    uint32_t bits;
    float    f;
} inf_ = { 0x7f800000 };
#define INFINITY_ inf_.f

static float fmaxf_(float x, float y) { return x > y ? x : y; }
static float fminf_(float x, float y) { return x < y ? x : y; }

static bool isfinitef_(float x) { return 0 == x*0; }

static float minus_1_ulp(float x) {
    int32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bits = bits - 1;
    memcpy(&x, &bits, sizeof(bits));
    return x;
}

static float eval_curve(const skcms_Curve* curve, float x) {
    if (curve->table_entries == 0) {
        return skcms_TransferFunction_eval(&curve->parametric, x);
    }

    float ix = fmaxf_(0, fminf_(x, 1)) * (curve->table_entries - 1);
    int   lo = (int)                   ix        ,
          hi = (int)(float)minus_1_ulp(ix + 1.0f);
    float t = ix - (float)lo;

    float l, h;
    if (curve->table_8) {
        l = curve->table_8[lo] * (1/255.0f);
        h = curve->table_8[hi] * (1/255.0f);
    } else {
        uint16_t be_l, be_h;
        memcpy(&be_l, curve->table_16 + 2*lo, 2);
        memcpy(&be_h, curve->table_16 + 2*hi, 2);
        uint16_t le_l = ((be_l << 8) | (be_l >> 8)) & 0xffff;
        uint16_t le_h = ((be_h << 8) | (be_h >> 8)) & 0xffff;
        l = le_l * (1/65535.0f);
        h = le_h * (1/65535.0f);
    }
    return l + (h-l)*t;
}

static float max_roundtrip_error(const skcms_Curve* curve, const skcms_TransferFunction* inv_tf) {
    uint32_t N = curve->table_entries > 256 ? curve->table_entries : 256;
    const float dx = 1.0f / (N - 1);
    float err = 0;
    for (uint32_t i = 0; i < N; i++) {
        float x = i * dx,
              y = eval_curve(curve, x);
        err = fmaxf_(err, fabsf_(x - skcms_TransferFunction_eval(inv_tf, y)));
    }
    return err;
}

bool skcms_AreApproximateInverses(const skcms_Curve* curve, const skcms_TransferFunction* inv_tf) {
    return max_roundtrip_error(curve, inv_tf) < (1/512.0f);
}

// Additional ICC signature values that are only used internally
enum {
    // File signature
    skcms_Signature_acsp = 0x61637370,

    // Tag signatures
    skcms_Signature_rTRC = 0x72545243,
    skcms_Signature_gTRC = 0x67545243,
    skcms_Signature_bTRC = 0x62545243,
    skcms_Signature_kTRC = 0x6B545243,

    skcms_Signature_rXYZ = 0x7258595A,
    skcms_Signature_gXYZ = 0x6758595A,
    skcms_Signature_bXYZ = 0x6258595A,

    skcms_Signature_A2B0 = 0x41324230,
    skcms_Signature_A2B1 = 0x41324231,
    skcms_Signature_mAB  = 0x6D414220,

    skcms_Signature_CHAD = 0x63686164,

    // Type signatures
    skcms_Signature_curv = 0x63757276,
    skcms_Signature_mft1 = 0x6D667431,
    skcms_Signature_mft2 = 0x6D667432,
    skcms_Signature_para = 0x70617261,
    skcms_Signature_sf32 = 0x73663332,
    // XYZ is also a PCS signature, so it's defined in skcms.h
    // skcms_Signature_XYZ = 0x58595A20,
};

static uint16_t read_big_u16(const uint8_t* ptr) {
    uint16_t be;
    memcpy(&be, ptr, sizeof(be));
#if defined(_MSC_VER)
    return _byteswap_ushort(be);
#else
    return __builtin_bswap16(be);
#endif
}

static uint32_t read_big_u32(const uint8_t* ptr) {
    uint32_t be;
    memcpy(&be, ptr, sizeof(be));
#if defined(_MSC_VER)
    return _byteswap_ulong(be);
#else
    return __builtin_bswap32(be);
#endif
}

static int32_t read_big_i32(const uint8_t* ptr) {
    return (int32_t)read_big_u32(ptr);
}

static float read_big_fixed(const uint8_t* ptr) {
    return read_big_i32(ptr) * (1.0f / 65536.0f);
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
} header_Layout;

typedef struct {
    uint8_t signature [4];
    uint8_t offset    [4];
    uint8_t size      [4];
} tag_Layout;

static const tag_Layout* get_tag_table(const skcms_ICCProfile* profile) {
    return (const tag_Layout*)(profile->buffer + SAFE_SIZEOF(header_Layout));
}

// s15Fixed16ArrayType is technically variable sized, holding N values. However, the only valid
// use of the type is for the CHAD tag that stores exactly nine values.
typedef struct {
    uint8_t type     [ 4];
    uint8_t reserved [ 4];
    uint8_t values   [36];
} sf32_Layout;

bool skcms_GetCHAD(const skcms_ICCProfile* profile, skcms_Matrix3x3* m) {
    skcms_ICCTag tag;
    if (!skcms_GetTagBySignature(profile, skcms_Signature_CHAD, &tag)) {
        return false;
    }

    if (tag.type != skcms_Signature_sf32 || tag.size < SAFE_SIZEOF(sf32_Layout)) {
        return false;
    }

    const sf32_Layout* sf32Tag = (const sf32_Layout*)tag.buf;
    const uint8_t* values = sf32Tag->values;
    for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c, values += 4) {
        m->vals[r][c] = read_big_fixed(values);
    }
    return true;
}

// XYZType is technically variable sized, holding N XYZ triples. However, the only valid uses of
// the type are for tags/data that store exactly one triple.
typedef struct {
    uint8_t type     [4];
    uint8_t reserved [4];
    uint8_t X        [4];
    uint8_t Y        [4];
    uint8_t Z        [4];
} XYZ_Layout;

static bool read_tag_xyz(const skcms_ICCTag* tag, float* x, float* y, float* z) {
    if (tag->type != skcms_Signature_XYZ || tag->size < SAFE_SIZEOF(XYZ_Layout)) {
        return false;
    }

    const XYZ_Layout* xyzTag = (const XYZ_Layout*)tag->buf;

    *x = read_big_fixed(xyzTag->X);
    *y = read_big_fixed(xyzTag->Y);
    *z = read_big_fixed(xyzTag->Z);
    return true;
}

static bool read_to_XYZD50(const skcms_ICCTag* rXYZ, const skcms_ICCTag* gXYZ,
                           const skcms_ICCTag* bXYZ, skcms_Matrix3x3* toXYZ) {
    return read_tag_xyz(rXYZ, &toXYZ->vals[0][0], &toXYZ->vals[1][0], &toXYZ->vals[2][0]) &&
           read_tag_xyz(gXYZ, &toXYZ->vals[0][1], &toXYZ->vals[1][1], &toXYZ->vals[2][1]) &&
           read_tag_xyz(bXYZ, &toXYZ->vals[0][2], &toXYZ->vals[1][2], &toXYZ->vals[2][2]);
}

static bool tf_is_valid(const skcms_TransferFunction* tf) {
    // Reject obviously malformed inputs
    if (!isfinitef_(tf->a + tf->b + tf->c + tf->d + tf->e + tf->f + tf->g)) {
        return false;
    }

    // All of these parameters should be non-negative
    if (tf->a < 0 || tf->c < 0 || tf->d < 0 || tf->g < 0) {
        return false;
    }

    return true;
}

typedef struct {
    uint8_t type          [4];
    uint8_t reserved_a    [4];
    uint8_t function_type [2];
    uint8_t reserved_b    [2];
    uint8_t variable      [1/*variable*/];  // 1, 3, 4, 5, or 7 s15.16, depending on function_type
} para_Layout;

static bool read_curve_para(const uint8_t* buf, uint32_t size,
                            skcms_Curve* curve, uint32_t* curve_size) {
    if (size < SAFE_FIXED_SIZE(para_Layout)) {
        return false;
    }

    const para_Layout* paraTag = (const para_Layout*)buf;

    enum { kG = 0, kGAB = 1, kGABC = 2, kGABCD = 3, kGABCDEF = 4 };
    uint16_t function_type = read_big_u16(paraTag->function_type);
    if (function_type > kGABCDEF) {
        return false;
    }

    static const uint32_t curve_bytes[] = { 4, 12, 16, 20, 28 };
    if (size < SAFE_FIXED_SIZE(para_Layout) + curve_bytes[function_type]) {
        return false;
    }

    if (curve_size) {
        *curve_size = SAFE_FIXED_SIZE(para_Layout) + curve_bytes[function_type];
    }

    curve->table_entries = 0;
    curve->parametric.a  = 1.0f;
    curve->parametric.b  = 0.0f;
    curve->parametric.c  = 0.0f;
    curve->parametric.d  = 0.0f;
    curve->parametric.e  = 0.0f;
    curve->parametric.f  = 0.0f;
    curve->parametric.g  = read_big_fixed(paraTag->variable);

    switch (function_type) {
        case kGAB:
            curve->parametric.a = read_big_fixed(paraTag->variable + 4);
            curve->parametric.b = read_big_fixed(paraTag->variable + 8);
            if (curve->parametric.a == 0) {
                return false;
            }
            curve->parametric.d = -curve->parametric.b / curve->parametric.a;
            break;
        case kGABC:
            curve->parametric.a = read_big_fixed(paraTag->variable + 4);
            curve->parametric.b = read_big_fixed(paraTag->variable + 8);
            curve->parametric.e = read_big_fixed(paraTag->variable + 12);
            if (curve->parametric.a == 0) {
                return false;
            }
            curve->parametric.d = -curve->parametric.b / curve->parametric.a;
            curve->parametric.f = curve->parametric.e;
            break;
        case kGABCD:
            curve->parametric.a = read_big_fixed(paraTag->variable + 4);
            curve->parametric.b = read_big_fixed(paraTag->variable + 8);
            curve->parametric.c = read_big_fixed(paraTag->variable + 12);
            curve->parametric.d = read_big_fixed(paraTag->variable + 16);
            break;
        case kGABCDEF:
            curve->parametric.a = read_big_fixed(paraTag->variable + 4);
            curve->parametric.b = read_big_fixed(paraTag->variable + 8);
            curve->parametric.c = read_big_fixed(paraTag->variable + 12);
            curve->parametric.d = read_big_fixed(paraTag->variable + 16);
            curve->parametric.e = read_big_fixed(paraTag->variable + 20);
            curve->parametric.f = read_big_fixed(paraTag->variable + 24);
            break;
    }
    return tf_is_valid(&curve->parametric);
}

typedef struct {
    uint8_t type          [4];
    uint8_t reserved      [4];
    uint8_t value_count   [4];
    uint8_t variable      [1/*variable*/];  // value_count, 8.8 if 1, uint16 (n*65535) if > 1
} curv_Layout;

static bool read_curve_curv(const uint8_t* buf, uint32_t size,
                            skcms_Curve* curve, uint32_t* curve_size) {
    if (size < SAFE_FIXED_SIZE(curv_Layout)) {
        return false;
    }

    const curv_Layout* curvTag = (const curv_Layout*)buf;

    uint32_t value_count = read_big_u32(curvTag->value_count);
    if (size < SAFE_FIXED_SIZE(curv_Layout) + value_count * SAFE_SIZEOF(uint16_t)) {
        return false;
    }

    if (curve_size) {
        *curve_size = SAFE_FIXED_SIZE(curv_Layout) + value_count * SAFE_SIZEOF(uint16_t);
    }

    if (value_count < 2) {
        curve->table_entries = 0;
        curve->parametric.a  = 1.0f;
        curve->parametric.b  = 0.0f;
        curve->parametric.c  = 0.0f;
        curve->parametric.d  = 0.0f;
        curve->parametric.e  = 0.0f;
        curve->parametric.f  = 0.0f;
        if (value_count == 0) {
            // Empty tables are a shorthand for an identity curve
            curve->parametric.g = 1.0f;
        } else {
            // Single entry tables are a shorthand for simple gamma
            curve->parametric.g = read_big_u16(curvTag->variable) * (1.0f / 256.0f);
        }
    } else {
        curve->table_8       = nullptr;
        curve->table_16      = curvTag->variable;
        curve->table_entries = value_count;
    }

    return true;
}

// Parses both curveType and parametricCurveType data. Ensures that at most 'size' bytes are read.
// If curve_size is not nullptr, writes the number of bytes used by the curve in (*curve_size).
static bool read_curve(const uint8_t* buf, uint32_t size,
                       skcms_Curve* curve, uint32_t* curve_size) {
    if (!buf || size < 4 || !curve) {
        return false;
    }

    uint32_t type = read_big_u32(buf);
    if (type == skcms_Signature_para) {
        return read_curve_para(buf, size, curve, curve_size);
    } else if (type == skcms_Signature_curv) {
        return read_curve_curv(buf, size, curve, curve_size);
    }

    return false;
}

// mft1 and mft2 share a large chunk of data
typedef struct {
    uint8_t type                 [ 4];
    uint8_t reserved_a           [ 4];
    uint8_t input_channels       [ 1];
    uint8_t output_channels      [ 1];
    uint8_t grid_points          [ 1];
    uint8_t reserved_b           [ 1];
    uint8_t matrix               [36];
} mft_CommonLayout;

typedef struct {
    mft_CommonLayout common      [1];

    uint8_t variable             [1/*variable*/];
} mft1_Layout;

typedef struct {
    mft_CommonLayout common      [1];

    uint8_t input_table_entries  [2];
    uint8_t output_table_entries [2];
    uint8_t variable             [1/*variable*/];
} mft2_Layout;

static bool read_mft_common(const mft_CommonLayout* mftTag, skcms_A2B* a2b) {
    // MFT matrices are applied before the first set of curves, but must be identity unless the
    // input is PCSXYZ. We don't support PCSXYZ profiles, so we ignore this matrix. Note that the
    // matrix in skcms_A2B is applied later in the pipe, so supporting this would require another
    // field/flag.
    a2b->matrix_channels = 0;

    a2b->input_channels  = mftTag->input_channels[0];
    a2b->output_channels = mftTag->output_channels[0];

    // We require exactly three (ie XYZ/Lab/RGB) output channels
    if (a2b->output_channels != ARRAY_COUNT(a2b->output_curves)) {
        return false;
    }
    // We require at least one, and no more than four (ie CMYK) input channels
    if (a2b->input_channels < 1 || a2b->input_channels > ARRAY_COUNT(a2b->input_curves)) {
        return false;
    }

    for (uint32_t i = 0; i < a2b->input_channels; ++i) {
        a2b->grid_points[i] = mftTag->grid_points[0];
    }
    // The grid only makes sense with at least two points along each axis
    if (a2b->grid_points[0] < 2) {
        return false;
    }

    return true;
}

static bool init_a2b_tables(const uint8_t* table_base, uint64_t max_tables_len, uint32_t byte_width,
                            uint32_t input_table_entries, uint32_t output_table_entries,
                            skcms_A2B* a2b) {
    // byte_width is 1 or 2, [input|output]_table_entries are in [2, 4096], so no overflow
    uint32_t byte_len_per_input_table  = input_table_entries * byte_width;
    uint32_t byte_len_per_output_table = output_table_entries * byte_width;

    // [input|output]_channels are <= 4, so still no overflow
    uint32_t byte_len_all_input_tables  = a2b->input_channels * byte_len_per_input_table;
    uint32_t byte_len_all_output_tables = a2b->output_channels * byte_len_per_output_table;

    uint64_t grid_size = a2b->output_channels * byte_width;
    for (uint32_t axis = 0; axis < a2b->input_channels; ++axis) {
        grid_size *= a2b->grid_points[axis];
    }

    if (max_tables_len < byte_len_all_input_tables + grid_size + byte_len_all_output_tables) {
        return false;
    }

    for (uint32_t i = 0; i < a2b->input_channels; ++i) {
        a2b->input_curves[i].table_entries = input_table_entries;
        if (byte_width == 1) {
            a2b->input_curves[i].table_8  = table_base + i * byte_len_per_input_table;
            a2b->input_curves[i].table_16 = nullptr;
        } else {
            a2b->input_curves[i].table_8  = nullptr;
            a2b->input_curves[i].table_16 = table_base + i * byte_len_per_input_table;
        }
    }

    if (byte_width == 1) {
        a2b->grid_8  = table_base + byte_len_all_input_tables;
        a2b->grid_16 = nullptr;
    } else {
        a2b->grid_8  = nullptr;
        a2b->grid_16 = table_base + byte_len_all_input_tables;
    }

    const uint8_t* output_table_base = table_base + byte_len_all_input_tables + grid_size;
    for (uint32_t i = 0; i < a2b->output_channels; ++i) {
        a2b->output_curves[i].table_entries = output_table_entries;
        if (byte_width == 1) {
            a2b->output_curves[i].table_8  = output_table_base + i * byte_len_per_output_table;
            a2b->output_curves[i].table_16 = nullptr;
        } else {
            a2b->output_curves[i].table_8  = nullptr;
            a2b->output_curves[i].table_16 = output_table_base + i * byte_len_per_output_table;
        }
    }

    return true;
}

static bool read_tag_mft1(const skcms_ICCTag* tag, skcms_A2B* a2b) {
    if (tag->size < SAFE_FIXED_SIZE(mft1_Layout)) {
        return false;
    }

    const mft1_Layout* mftTag = (const mft1_Layout*)tag->buf;
    if (!read_mft_common(mftTag->common, a2b)) {
        return false;
    }

    uint32_t input_table_entries  = 256;
    uint32_t output_table_entries = 256;

    return init_a2b_tables(mftTag->variable, tag->size - SAFE_FIXED_SIZE(mft1_Layout), 1,
                           input_table_entries, output_table_entries, a2b);
}

static bool read_tag_mft2(const skcms_ICCTag* tag, skcms_A2B* a2b) {
    if (tag->size < SAFE_FIXED_SIZE(mft2_Layout)) {
        return false;
    }

    const mft2_Layout* mftTag = (const mft2_Layout*)tag->buf;
    if (!read_mft_common(mftTag->common, a2b)) {
        return false;
    }

    uint32_t input_table_entries = read_big_u16(mftTag->input_table_entries);
    uint32_t output_table_entries = read_big_u16(mftTag->output_table_entries);

    // ICC spec mandates that 2 <= table_entries <= 4096
    if (input_table_entries < 2 || input_table_entries > 4096 ||
        output_table_entries < 2 || output_table_entries > 4096) {
        return false;
    }

    return init_a2b_tables(mftTag->variable, tag->size - SAFE_FIXED_SIZE(mft2_Layout), 2,
                           input_table_entries, output_table_entries, a2b);
}

static bool read_curves(const uint8_t* buf, uint32_t size, uint32_t curve_offset,
                        uint32_t num_curves, skcms_Curve* curves) {
    for (uint32_t i = 0; i < num_curves; ++i) {
        if (curve_offset > size) {
            return false;
        }

        uint32_t curve_bytes;
        if (!read_curve(buf + curve_offset, size - curve_offset, &curves[i], &curve_bytes)) {
            return false;
        }

        if (curve_bytes > UINT32_MAX - 3) {
            return false;
        }
        curve_bytes = (curve_bytes + 3) & ~3U;

        uint64_t new_offset_64 = (uint64_t)curve_offset + curve_bytes;
        curve_offset = (uint32_t)new_offset_64;
        if (new_offset_64 != curve_offset) {
            return false;
        }
    }

    return true;
}

typedef struct {
    uint8_t type                 [ 4];
    uint8_t reserved_a           [ 4];
    uint8_t input_channels       [ 1];
    uint8_t output_channels      [ 1];
    uint8_t reserved_b           [ 2];
    uint8_t b_curve_offset       [ 4];
    uint8_t matrix_offset        [ 4];
    uint8_t m_curve_offset       [ 4];
    uint8_t clut_offset          [ 4];
    uint8_t a_curve_offset       [ 4];
} mAB_Layout;

typedef struct {
    uint8_t grid_points          [16];
    uint8_t grid_byte_width      [ 1];
    uint8_t reserved             [ 3];
    uint8_t variable             [1/*variable*/];
} mABCLUT_Layout;

static bool read_tag_mab(const skcms_ICCTag* tag, skcms_A2B* a2b, bool pcs_is_xyz) {
    if (tag->size < SAFE_SIZEOF(mAB_Layout)) {
        return false;
    }

    const mAB_Layout* mABTag = (const mAB_Layout*)tag->buf;

    a2b->input_channels  = mABTag->input_channels[0];
    a2b->output_channels = mABTag->output_channels[0];

    // We require exactly three (ie XYZ/Lab/RGB) output channels
    if (a2b->output_channels != ARRAY_COUNT(a2b->output_curves)) {
        return false;
    }
    // We require no more than four (ie CMYK) input channels
    if (a2b->input_channels > ARRAY_COUNT(a2b->input_curves)) {
        return false;
    }

    uint32_t b_curve_offset = read_big_u32(mABTag->b_curve_offset);
    uint32_t matrix_offset  = read_big_u32(mABTag->matrix_offset);
    uint32_t m_curve_offset = read_big_u32(mABTag->m_curve_offset);
    uint32_t clut_offset    = read_big_u32(mABTag->clut_offset);
    uint32_t a_curve_offset = read_big_u32(mABTag->a_curve_offset);

    // "B" curves must be present
    if (0 == b_curve_offset) {
        return false;
    }

    if (!read_curves(tag->buf, tag->size, b_curve_offset, a2b->output_channels,
                     a2b->output_curves)) {
        return false;
    }

    // "M" curves and Matrix must be used together
    if (0 != m_curve_offset) {
        if (0 == matrix_offset) {
            return false;
        }
        a2b->matrix_channels = a2b->output_channels;
        if (!read_curves(tag->buf, tag->size, m_curve_offset, a2b->matrix_channels,
                         a2b->matrix_curves)) {
            return false;
        }

        // Read matrix, which is stored as a row-major 3x3, followed by the fourth column
        if (tag->size < matrix_offset + 12 * SAFE_SIZEOF(uint32_t)) {
            return false;
        }
        float encoding_factor = pcs_is_xyz ? 65535 / 32768.0f : 1.0f;
        const uint8_t* mtx_buf = tag->buf + matrix_offset;
        a2b->matrix.vals[0][0] = encoding_factor * read_big_fixed(mtx_buf + 0);
        a2b->matrix.vals[0][1] = encoding_factor * read_big_fixed(mtx_buf + 4);
        a2b->matrix.vals[0][2] = encoding_factor * read_big_fixed(mtx_buf + 8);
        a2b->matrix.vals[1][0] = encoding_factor * read_big_fixed(mtx_buf + 12);
        a2b->matrix.vals[1][1] = encoding_factor * read_big_fixed(mtx_buf + 16);
        a2b->matrix.vals[1][2] = encoding_factor * read_big_fixed(mtx_buf + 20);
        a2b->matrix.vals[2][0] = encoding_factor * read_big_fixed(mtx_buf + 24);
        a2b->matrix.vals[2][1] = encoding_factor * read_big_fixed(mtx_buf + 28);
        a2b->matrix.vals[2][2] = encoding_factor * read_big_fixed(mtx_buf + 32);
        a2b->matrix.vals[0][3] = encoding_factor * read_big_fixed(mtx_buf + 36);
        a2b->matrix.vals[1][3] = encoding_factor * read_big_fixed(mtx_buf + 40);
        a2b->matrix.vals[2][3] = encoding_factor * read_big_fixed(mtx_buf + 44);
    } else {
        if (0 != matrix_offset) {
            return false;
        }
        a2b->matrix_channels = 0;
    }

    // "A" curves and CLUT must be used together
    if (0 != a_curve_offset) {
        if (0 == clut_offset) {
            return false;
        }
        if (!read_curves(tag->buf, tag->size, a_curve_offset, a2b->input_channels,
                         a2b->input_curves)) {
            return false;
        }

        if (tag->size < clut_offset + SAFE_FIXED_SIZE(mABCLUT_Layout)) {
            return false;
        }
        const mABCLUT_Layout* clut = (const mABCLUT_Layout*)(tag->buf + clut_offset);

        if (clut->grid_byte_width[0] == 1) {
            a2b->grid_8  = clut->variable;
            a2b->grid_16 = nullptr;
        } else if (clut->grid_byte_width[0] == 2) {
            a2b->grid_8  = nullptr;
            a2b->grid_16 = clut->variable;
        } else {
            return false;
        }

        uint64_t grid_size = a2b->output_channels * clut->grid_byte_width[0];
        for (uint32_t i = 0; i < a2b->input_channels; ++i) {
            a2b->grid_points[i] = clut->grid_points[i];
            // The grid only makes sense with at least two points along each axis
            if (a2b->grid_points[i] < 2) {
                return false;
            }
            grid_size *= a2b->grid_points[i];
        }
        if (tag->size < clut_offset + SAFE_FIXED_SIZE(mABCLUT_Layout) + grid_size) {
            return false;
        }
    } else {
        if (0 != clut_offset) {
            return false;
        }

        // If there is no CLUT, the number of input and output channels must match
        if (a2b->input_channels != a2b->output_channels) {
            return false;
        }

        // Zero out the number of input channels to signal that we're skipping this stage
        a2b->input_channels = 0;
    }

    return true;
}

static int fit_linear(const skcms_Curve* curve, int N, float tol, float* c, float* d, float* f) {
    assert(N > 1);
    // We iteratively fit the first points to the TF's linear piece.
    // We want the cx + f line to pass through the first and last points we fit exactly.
    //
    // As we walk along the points we find the minimum and maximum slope of the line before the
    // error would exceed our tolerance.  We stop when the range [slope_min, slope_max] becomes
    // emtpy, when we definitely can't add any more points.
    //
    // Some points' error intervals may intersect the running interval but not lie fully
    // within it.  So we keep track of the last point we saw that is a valid end point candidate,
    // and once the search is done, back up to build the line through *that* point.
    const float dx = 1.0f / (N - 1);

    int lin_points = 1;
    *f = eval_curve(curve, 0);

    float slope_min = -INFINITY_;
    float slope_max = +INFINITY_;
    for (int i = 1; i < N; ++i) {
        float x = i * dx;
        float y = eval_curve(curve, x);

        float slope_max_i = (y + tol - *f) / x,
              slope_min_i = (y - tol - *f) / x;
        if (slope_max_i < slope_min || slope_max < slope_min_i) {
            // Slope intervals would no longer overlap.
            break;
        }
        slope_max = fminf_(slope_max, slope_max_i);
        slope_min = fmaxf_(slope_min, slope_min_i);

        float cur_slope = (y - *f) / x;
        if (slope_min <= cur_slope && cur_slope <= slope_max) {
            lin_points = i + 1;
            *c = cur_slope;
        }
    }

    // Set D to the last point that met our tolerance.
    *d = (lin_points - 1) * dx;
    return lin_points;
}

static bool read_a2b(const skcms_ICCTag* tag, skcms_A2B* a2b, bool pcs_is_xyz) {
    bool ok = false;
    if (tag->type == skcms_Signature_mft1) {
        ok = read_tag_mft1(tag, a2b);
    } else if (tag->type == skcms_Signature_mft2) {
        ok = read_tag_mft2(tag, a2b);
    } else if (tag->type == skcms_Signature_mAB) {
        ok = read_tag_mab(tag, a2b, pcs_is_xyz);
    }
    if (!ok) {
        return false;
    }

    // Detect and canonicalize identity tables.
    skcms_Curve* curves[] = {
        a2b->input_channels  > 0 ? a2b->input_curves  + 0 : nullptr,
        a2b->input_channels  > 1 ? a2b->input_curves  + 1 : nullptr,
        a2b->input_channels  > 2 ? a2b->input_curves  + 2 : nullptr,
        a2b->input_channels  > 3 ? a2b->input_curves  + 3 : nullptr,
        a2b->matrix_channels > 0 ? a2b->matrix_curves + 0 : nullptr,
        a2b->matrix_channels > 1 ? a2b->matrix_curves + 1 : nullptr,
        a2b->matrix_channels > 2 ? a2b->matrix_curves + 2 : nullptr,
        a2b->output_channels > 0 ? a2b->output_curves + 0 : nullptr,
        a2b->output_channels > 1 ? a2b->output_curves + 1 : nullptr,
        a2b->output_channels > 2 ? a2b->output_curves + 2 : nullptr,
    };

    for (int i = 0; i < ARRAY_COUNT(curves); i++) {
        skcms_Curve* curve = curves[i];

        if (curve && curve->table_entries && curve->table_entries <= (uint32_t)INT_MAX) {
            int N = (int)curve->table_entries;

            float c,d,f;
            if (N == fit_linear(curve, N, 1.0f/(2*N), &c,&d,&f)
                && c == 1.0f
                && f == 0.0f) {
                curve->table_entries = 0;
                curve->table_8       = nullptr;
                curve->table_16      = nullptr;
                curve->parametric    = skcms_TransferFunction{1,1,0,0,0,0,0};
            }
        }
    }

    return true;
}

void skcms_GetTagByIndex(const skcms_ICCProfile* profile, uint32_t idx, skcms_ICCTag* tag) {
    if (!profile || !profile->buffer || !tag) { return; }
    if (idx > profile->tag_count) { return; }
    const tag_Layout* tags = get_tag_table(profile);
    tag->signature = read_big_u32(tags[idx].signature);
    tag->size      = read_big_u32(tags[idx].size);
    tag->buf       = read_big_u32(tags[idx].offset) + profile->buffer;
    tag->type      = read_big_u32(tag->buf);
}

bool skcms_GetTagBySignature(const skcms_ICCProfile* profile, uint32_t sig, skcms_ICCTag* tag) {
    if (!profile || !profile->buffer || !tag) { return false; }
    const tag_Layout* tags = get_tag_table(profile);
    for (uint32_t i = 0; i < profile->tag_count; ++i) {
        if (read_big_u32(tags[i].signature) == sig) {
            tag->signature = sig;
            tag->size      = read_big_u32(tags[i].size);
            tag->buf       = read_big_u32(tags[i].offset) + profile->buffer;
            tag->type      = read_big_u32(tag->buf);
            return true;
        }
    }
    return false;
}

static bool usable_as_src(const skcms_ICCProfile* profile) {
    return profile->has_A2B
       || (profile->has_trc && profile->has_toXYZD50);
}

bool skcms_Parse(const void* buf, size_t len, skcms_ICCProfile* profile) {
    assert(SAFE_SIZEOF(header_Layout) == 132);

    if (!profile) {
        return false;
    }
    memset(profile, 0, SAFE_SIZEOF(*profile));

    if (len < SAFE_SIZEOF(header_Layout)) {
        return false;
    }

    // Byte-swap all header fields
    const header_Layout* header  = (const header_Layout*)buf;
    profile->buffer              = (const uint8_t*)buf;
    profile->size                = read_big_u32(header->size);
    uint32_t version             = read_big_u32(header->version);
    profile->data_color_space    = read_big_u32(header->data_color_space);
    profile->pcs                 = read_big_u32(header->pcs);
    uint32_t signature           = read_big_u32(header->signature);
    float illuminant_X           = read_big_fixed(header->illuminant_X);
    float illuminant_Y           = read_big_fixed(header->illuminant_Y);
    float illuminant_Z           = read_big_fixed(header->illuminant_Z);
    profile->tag_count           = read_big_u32(header->tag_count);

    // Validate signature, size (smaller than buffer, large enough to hold tag table),
    // and major version
    uint64_t tag_table_size = profile->tag_count * SAFE_SIZEOF(tag_Layout);
    if (signature != skcms_Signature_acsp ||
        profile->size > len ||
        profile->size < SAFE_SIZEOF(header_Layout) + tag_table_size ||
        (version >> 24) > 4) {
        return false;
    }

    // Validate that illuminant is D50 white
    if (fabsf_(illuminant_X - 0.9642f) > 0.0100f ||
        fabsf_(illuminant_Y - 1.0000f) > 0.0100f ||
        fabsf_(illuminant_Z - 0.8249f) > 0.0100f) {
        return false;
    }

    // Validate that all tag entries have sane offset + size
    const tag_Layout* tags = get_tag_table(profile);
    for (uint32_t i = 0; i < profile->tag_count; ++i) {
        uint32_t tag_offset = read_big_u32(tags[i].offset);
        uint32_t tag_size   = read_big_u32(tags[i].size);
        uint64_t tag_end    = (uint64_t)tag_offset + (uint64_t)tag_size;
        if (tag_size < 4 || tag_end > profile->size) {
            return false;
        }
    }

    if (profile->pcs != skcms_Signature_XYZ && profile->pcs != skcms_Signature_Lab) {
        return false;
    }

    bool pcs_is_xyz = profile->pcs == skcms_Signature_XYZ;

    // Pre-parse commonly used tags.
    skcms_ICCTag kTRC;
    if (profile->data_color_space == skcms_Signature_Gray &&
        skcms_GetTagBySignature(profile, skcms_Signature_kTRC, &kTRC)) {
        if (!read_curve(kTRC.buf, kTRC.size, &profile->trc[0], nullptr)) {
            // Malformed tag
            return false;
        }
        profile->trc[1] = profile->trc[0];
        profile->trc[2] = profile->trc[0];
        profile->has_trc = true;

        if (pcs_is_xyz) {
            profile->toXYZD50.vals[0][0] = illuminant_X;
            profile->toXYZD50.vals[1][1] = illuminant_Y;
            profile->toXYZD50.vals[2][2] = illuminant_Z;
            profile->has_toXYZD50 = true;
        }
    } else {
        skcms_ICCTag rTRC, gTRC, bTRC;
        if (skcms_GetTagBySignature(profile, skcms_Signature_rTRC, &rTRC) &&
            skcms_GetTagBySignature(profile, skcms_Signature_gTRC, &gTRC) &&
            skcms_GetTagBySignature(profile, skcms_Signature_bTRC, &bTRC)) {
            if (!read_curve(rTRC.buf, rTRC.size, &profile->trc[0], nullptr) ||
                !read_curve(gTRC.buf, gTRC.size, &profile->trc[1], nullptr) ||
                !read_curve(bTRC.buf, bTRC.size, &profile->trc[2], nullptr)) {
                // Malformed TRC tags
                return false;
            }
            profile->has_trc = true;
        }

        skcms_ICCTag rXYZ, gXYZ, bXYZ;
        if (skcms_GetTagBySignature(profile, skcms_Signature_rXYZ, &rXYZ) &&
            skcms_GetTagBySignature(profile, skcms_Signature_gXYZ, &gXYZ) &&
            skcms_GetTagBySignature(profile, skcms_Signature_bXYZ, &bXYZ)) {
            if (!read_to_XYZD50(&rXYZ, &gXYZ, &bXYZ, &profile->toXYZD50)) {
                // Malformed XYZ tags
                return false;
            }
            profile->has_toXYZD50 = true;
        }
    }

    skcms_ICCTag a2b_tag;

    // For now, we're preferring A2B0, like Skia does and the ICC spec tells us to.
    // TODO: prefer A2B1 (relative colormetric) over A2B0 (perceptual)?
    // This breaks with the ICC spec, but we think it's a good idea, given that TRC curves
    // and all our known users are thinking exclusively in terms of relative colormetric.
    const uint32_t sigs[] = { skcms_Signature_A2B0, skcms_Signature_A2B1 };
    for (int i = 0; i < ARRAY_COUNT(sigs); i++) {
        if (skcms_GetTagBySignature(profile, sigs[i], &a2b_tag)) {
            if (!read_a2b(&a2b_tag, &profile->A2B, pcs_is_xyz)) {
                // Malformed A2B tag
                return false;
            }
            profile->has_A2B = true;
            break;
        }
    }

    return usable_as_src(profile);
}


const skcms_ICCProfile* skcms_sRGB_profile() {
    static const skcms_ICCProfile sRGB_profile = {
        nullptr,               // buffer, moot here

        0,                     // size, moot here
        skcms_Signature_RGB,   // data_color_space
        skcms_Signature_XYZ,   // pcs
        0,                     // tag count, moot here

        // We choose to represent sRGB with its canonical transfer function,
        // and with its canonical XYZD50 gamut matrix.
        true,  // has_trc, followed by the 3 trc curves
        {
            {{0, {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0}}},
            {{0, {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0}}},
            {{0, {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0}}},
        },

        true,  // has_toXYZD50, followed by 3x3 toXYZD50 matrix
        {{
            { 0.436065674f, 0.385147095f, 0.143066406f },
            { 0.222488403f, 0.716873169f, 0.060607910f },
            { 0.013916016f, 0.097076416f, 0.714096069f },
        }},

        false, // has_A2B, followed by a2b itself which we don't care about.
        {
            0,
            {
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
            },
            {0,0,0,0},
            nullptr,
            nullptr,

            0,
            {
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
            },
            {{
                { 1,0,0,0 },
                { 0,1,0,0 },
                { 0,0,1,0 },
            }},

            0,
            {
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
            },
        },
    };
    return &sRGB_profile;
}

const skcms_ICCProfile* skcms_XYZD50_profile() {
    // Just like sRGB above, but with identity transfer functions and toXYZD50 matrix.
    static const skcms_ICCProfile XYZD50_profile = {
        nullptr,               // buffer, moot here

        0,                     // size, moot here
        skcms_Signature_RGB,   // data_color_space
        skcms_Signature_XYZ,   // pcs
        0,                     // tag count, moot here

        true,  // has_trc, followed by the 3 trc curves
        {
            {{0, {1,1, 0,0,0,0,0}}},
            {{0, {1,1, 0,0,0,0,0}}},
            {{0, {1,1, 0,0,0,0,0}}},
        },

        true,  // has_toXYZD50, followed by 3x3 toXYZD50 matrix
        {{
            { 1,0,0 },
            { 0,1,0 },
            { 0,0,1 },
        }},

        false, // has_A2B, followed by a2b itself which we don't care about.
        {
            0,
            {
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
            },
            {0,0,0,0},
            nullptr,
            nullptr,

            0,
            {
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
            },
            {{
                { 1,0,0,0 },
                { 0,1,0,0 },
                { 0,0,1,0 },
            }},

            0,
            {
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
                {{0, {1,1, 0,0,0,0,0}}},
            },
        },
    };

    return &XYZD50_profile;
}

const skcms_TransferFunction* skcms_sRGB_TransferFunction() {
    return &skcms_sRGB_profile()->trc[0].parametric;
}

const skcms_TransferFunction* skcms_sRGB_Inverse_TransferFunction() {
    static const skcms_TransferFunction sRGB_inv =
        { (float)(1/2.4), 1.137119f, 0, 12.92f, 0.0031308f, -0.055f, 0 };
    return &sRGB_inv;
}

const skcms_TransferFunction* skcms_Identity_TransferFunction() {
    static const skcms_TransferFunction identity = {1,1,0,0,0,0,0};
    return &identity;
}

const uint8_t skcms_252_random_bytes[] = {
    8, 179, 128, 204, 253, 38, 134, 184, 68, 102, 32, 138, 99, 39, 169, 215,
    119, 26, 3, 223, 95, 239, 52, 132, 114, 74, 81, 234, 97, 116, 244, 205, 30,
    154, 173, 12, 51, 159, 122, 153, 61, 226, 236, 178, 229, 55, 181, 220, 191,
    194, 160, 126, 168, 82, 131, 18, 180, 245, 163, 22, 246, 69, 235, 252, 57,
    108, 14, 6, 152, 240, 255, 171, 242, 20, 227, 177, 238, 96, 85, 16, 211,
    70, 200, 149, 155, 146, 127, 145, 100, 151, 109, 19, 165, 208, 195, 164,
    137, 254, 182, 248, 64, 201, 45, 209, 5, 147, 207, 210, 113, 162, 83, 225,
    9, 31, 15, 231, 115, 37, 58, 53, 24, 49, 197, 56, 120, 172, 48, 21, 214,
    129, 111, 11, 50, 187, 196, 34, 60, 103, 71, 144, 47, 203, 77, 80, 232,
    140, 222, 250, 206, 166, 247, 139, 249, 221, 72, 106, 27, 199, 117, 54,
    219, 135, 118, 40, 79, 41, 251, 46, 93, 212, 92, 233, 148, 28, 121, 63,
    123, 158, 105, 59, 29, 42, 143, 23, 0, 107, 176, 87, 104, 183, 156, 193,
    189, 90, 188, 65, 190, 17, 198, 7, 186, 161, 1, 124, 78, 125, 170, 133,
    174, 218, 67, 157, 75, 101, 89, 217, 62, 33, 141, 228, 25, 35, 91, 230, 4,
    2, 13, 73, 86, 167, 237, 84, 243, 44, 185, 66, 130, 110, 150, 142, 216, 88,
    112, 36, 224, 136, 202, 76, 94, 98, 175, 213
};

bool skcms_ApproximatelyEqualProfiles(const skcms_ICCProfile* A, const skcms_ICCProfile* B) {
    // For now this is the essentially the same strategy we use in test_only.c
    // for our skcms_Transform() smoke tests:
    //    1) transform A to XYZD50
    //    2) transform B to XYZD50
    //    3) return true if they're similar enough
    // Our current criterion in 3) is maximum 1 bit error per XYZD50 byte.

    // Here are 252 of a random shuffle of all possible bytes.
    // 252 is evenly divisible by 3 and 4.  Only 192, 10, 241, and 43 are missing.

    if (A->data_color_space != B->data_color_space) {
        return false;
    }

    // Interpret as RGB_888 if data color space is RGB or GRAY, RGBA_8888 if CMYK.
    skcms_PixelFormat fmt = skcms_PixelFormat_RGB_888;
    size_t npixels = 84;
    if (A->data_color_space == skcms_Signature_CMYK) {
        fmt = skcms_PixelFormat_RGBA_8888;
        npixels = 63;
    }

    uint8_t dstA[252],
            dstB[252];
    if (!skcms_Transform(
                skcms_252_random_bytes,     fmt, skcms_AlphaFormat_Unpremul, A,
                dstA, skcms_PixelFormat_RGB_888, skcms_AlphaFormat_Unpremul, skcms_XYZD50_profile(),
                npixels)) {
        return false;
    }
    if (!skcms_Transform(
                skcms_252_random_bytes,     fmt, skcms_AlphaFormat_Unpremul, B,
                dstB, skcms_PixelFormat_RGB_888, skcms_AlphaFormat_Unpremul, skcms_XYZD50_profile(),
                npixels)) {
        return false;
    }

    for (size_t i = 0; i < 252; i++) {
        if (abs((int)dstA[i] - (int)dstB[i]) > 1) {
            return false;
        }
    }
    return true;
}

bool skcms_TRCs_AreApproximateInverse(const skcms_ICCProfile* profile,
                                      const skcms_TransferFunction* inv_tf) {
    if (!profile || !profile->has_trc) {
        return false;
    }

    return skcms_AreApproximateInverses(&profile->trc[0], inv_tf) &&
           skcms_AreApproximateInverses(&profile->trc[1], inv_tf) &&
           skcms_AreApproximateInverses(&profile->trc[2], inv_tf);
}

static bool is_zero_to_one(float x) {
    return 0 <= x && x <= 1;
}

typedef struct { float vals[3]; } skcms_Vector3;

static skcms_Vector3 mv_mul(const skcms_Matrix3x3* m, const skcms_Vector3* v) {
    skcms_Vector3 dst = {{0,0,0}};
    for (int row = 0; row < 3; ++row) {
        dst.vals[row] = m->vals[row][0] * v->vals[0]
                      + m->vals[row][1] * v->vals[1]
                      + m->vals[row][2] * v->vals[2];
    }
    return dst;
}

bool skcms_PrimariesToXYZD50(float rx, float ry,
                             float gx, float gy,
                             float bx, float by,
                             float wx, float wy,
                             skcms_Matrix3x3* toXYZD50) {
    if (!is_zero_to_one(rx) || !is_zero_to_one(ry) ||
        !is_zero_to_one(gx) || !is_zero_to_one(gy) ||
        !is_zero_to_one(bx) || !is_zero_to_one(by) ||
        !is_zero_to_one(wx) || !is_zero_to_one(wy) ||
        !toXYZD50) {
        return false;
    }

    // First, we need to convert xy values (primaries) to XYZ.
    skcms_Matrix3x3 primaries = {{
        { rx, gx, bx },
        { ry, gy, by },
        { 1 - rx - ry, 1 - gx - gy, 1 - bx - by },
    }};
    skcms_Matrix3x3 primaries_inv;
    if (!skcms_Matrix3x3_invert(&primaries, &primaries_inv)) {
        return false;
    }

    // Assumes that Y is 1.0f.
    skcms_Vector3 wXYZ = { { wx / wy, 1, (1 - wx - wy) / wy } };
    skcms_Vector3 XYZ = mv_mul(&primaries_inv, &wXYZ);

    skcms_Matrix3x3 toXYZ = {{
        { XYZ.vals[0],           0,           0 },
        {           0, XYZ.vals[1],           0 },
        {           0,           0, XYZ.vals[2] },
    }};
    toXYZ = skcms_Matrix3x3_concat(&primaries, &toXYZ);

    // Now convert toXYZ matrix to toXYZD50.
    skcms_Vector3 wXYZD50 = { { 0.96422f, 1.0f, 0.82521f } };

    // Calculate the chromatic adaptation matrix.  We will use the Bradford method, thus
    // the matrices below.  The Bradford method is used by Adobe and is widely considered
    // to be the best.
    skcms_Matrix3x3 xyz_to_lms = {{
        {  0.8951f,  0.2664f, -0.1614f },
        { -0.7502f,  1.7135f,  0.0367f },
        {  0.0389f, -0.0685f,  1.0296f },
    }};
    skcms_Matrix3x3 lms_to_xyz = {{
        {  0.9869929f, -0.1470543f, 0.1599627f },
        {  0.4323053f,  0.5183603f, 0.0492912f },
        { -0.0085287f,  0.0400428f, 0.9684867f },
    }};

    skcms_Vector3 srcCone = mv_mul(&xyz_to_lms, &wXYZ);
    skcms_Vector3 dstCone = mv_mul(&xyz_to_lms, &wXYZD50);

    skcms_Matrix3x3 DXtoD50 = {{
        { dstCone.vals[0] / srcCone.vals[0], 0, 0 },
        { 0, dstCone.vals[1] / srcCone.vals[1], 0 },
        { 0, 0, dstCone.vals[2] / srcCone.vals[2] },
    }};
    DXtoD50 = skcms_Matrix3x3_concat(&DXtoD50, &xyz_to_lms);
    DXtoD50 = skcms_Matrix3x3_concat(&lms_to_xyz, &DXtoD50);

    *toXYZD50 = skcms_Matrix3x3_concat(&DXtoD50, &toXYZ);
    return true;
}


bool skcms_Matrix3x3_invert(const skcms_Matrix3x3* src, skcms_Matrix3x3* dst) {
    double a00 = src->vals[0][0],
           a01 = src->vals[1][0],
           a02 = src->vals[2][0],
           a10 = src->vals[0][1],
           a11 = src->vals[1][1],
           a12 = src->vals[2][1],
           a20 = src->vals[0][2],
           a21 = src->vals[1][2],
           a22 = src->vals[2][2];

    double b0 = a00*a11 - a01*a10,
           b1 = a00*a12 - a02*a10,
           b2 = a01*a12 - a02*a11,
           b3 = a20,
           b4 = a21,
           b5 = a22;

    double determinant = b0*b5
                       - b1*b4
                       + b2*b3;

    if (determinant == 0) {
        return false;
    }

    double invdet = 1.0 / determinant;
    if (invdet > +FLT_MAX || invdet < -FLT_MAX || !isfinitef_((float)invdet)) {
        return false;
    }

    b0 *= invdet;
    b1 *= invdet;
    b2 *= invdet;
    b3 *= invdet;
    b4 *= invdet;
    b5 *= invdet;

    dst->vals[0][0] = (float)( a11*b5 - a12*b4 );
    dst->vals[1][0] = (float)( a02*b4 - a01*b5 );
    dst->vals[2][0] = (float)(        +     b2 );
    dst->vals[0][1] = (float)( a12*b3 - a10*b5 );
    dst->vals[1][1] = (float)( a00*b5 - a02*b3 );
    dst->vals[2][1] = (float)(        -     b1 );
    dst->vals[0][2] = (float)( a10*b4 - a11*b3 );
    dst->vals[1][2] = (float)( a01*b3 - a00*b4 );
    dst->vals[2][2] = (float)(        +     b0 );

    for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
        if (!isfinitef_(dst->vals[r][c])) {
            return false;
        }
    }
    return true;
}

skcms_Matrix3x3 skcms_Matrix3x3_concat(const skcms_Matrix3x3* A, const skcms_Matrix3x3* B) {
    skcms_Matrix3x3 m = { { { 0,0,0 },{ 0,0,0 },{ 0,0,0 } } };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            m.vals[r][c] = A->vals[r][0] * B->vals[0][c]
                         + A->vals[r][1] * B->vals[1][c]
                         + A->vals[r][2] * B->vals[2][c];
        }
    return m;
}

#if defined(__clang__) || defined(__GNUC__)
    #define small_memcpy __builtin_memcpy
#else
    #define small_memcpy memcpy
#endif

static float log2f_(float x) {
    // The first approximation of log2(x) is its exponent 'e', minus 127.
    int32_t bits;
    small_memcpy(&bits, &x, sizeof(bits));

    float e = (float)bits * (1.0f / (1<<23));

    // If we use the mantissa too we can refine the error signficantly.
    int32_t m_bits = (bits & 0x007fffff) | 0x3f000000;
    float m;
    small_memcpy(&m, &m_bits, sizeof(m));

    return (e - 124.225514990f
              -   1.498030302f*m
              -   1.725879990f/(0.3520887068f + m));
}

static float exp2f_(float x) {
    float fract = x - floorf_(x);

    float fbits = (1.0f * (1<<23)) * (x + 121.274057500f
                                        -   1.490129070f*fract
                                        +  27.728023300f/(4.84252568f - fract));
    if (fbits > INT_MAX) {
        return INFINITY_;
    } else if (fbits < INT_MIN) {
        return -INFINITY_;
    }
    int32_t bits = (int32_t)fbits;
    small_memcpy(&x, &bits, sizeof(x));
    return x;
}

float powf_(float x, float y) {
    return (x == 0) || (x == 1) ? x
                                : exp2f_(log2f_(x) * y);
}

float skcms_TransferFunction_eval(const skcms_TransferFunction* tf, float x) {
    float sign = x < 0 ? -1.0f : 1.0f;
    x *= sign;

    return sign * (x < tf->d ? tf->c * x + tf->f
                             : powf_(tf->a * x + tf->b, tf->g) + tf->e);
}

// TODO: Adjust logic here? This still assumes that purely linear inputs will have D > 1, which
// we never generate. It also emits inverted linear using the same formulation. Standardize on
// G == 1 here, too?
bool skcms_TransferFunction_invert(const skcms_TransferFunction* src, skcms_TransferFunction* dst) {
    // Original equation is:       y = (ax + b)^g + e   for x >= d
    //                             y = cx + f           otherwise
    //
    // so 1st inverse is:          (y - e)^(1/g) = ax + b
    //                             x = ((y - e)^(1/g) - b) / a
    //
    // which can be re-written as: x = (1/a)(y - e)^(1/g) - b/a
    //                             x = ((1/a)^g)^(1/g) * (y - e)^(1/g) - b/a
    //                             x = ([(1/a)^g]y + [-((1/a)^g)e]) ^ [1/g] + [-b/a]
    //
    // and 2nd inverse is:         x = (y - f) / c
    // which can be re-written as: x = [1/c]y + [-f/c]
    //
    // and now both can be expressed in terms of the same parametric form as the
    // original - parameters are enclosed in square brackets.
    skcms_TransferFunction tf_inv = { 0, 0, 0, 0, 0, 0, 0 };

    // This rejects obviously malformed inputs, as well as decreasing functions
    if (!tf_is_valid(src)) {
        return false;
    }

    // There are additional constraints to be invertible
    bool has_nonlinear = (src->d <= 1);
    bool has_linear = (src->d > 0);

    // Is the linear section not invertible?
    if (has_linear && src->c == 0) {
        return false;
    }

    // Is the nonlinear section not invertible?
    if (has_nonlinear && (src->a == 0 || src->g == 0)) {
        return false;
    }

    // If both segments are present, they need to line up
    if (has_linear && has_nonlinear) {
        float l_at_d = src->c * src->d + src->f;
        float n_at_d = powf_(src->a * src->d + src->b, src->g) + src->e;
        if (fabsf_(l_at_d - n_at_d) > (1 / 512.0f)) {
            return false;
        }
    }

    // Invert linear segment
    if (has_linear) {
        tf_inv.c = 1.0f / src->c;
        tf_inv.f = -src->f / src->c;
    }

    // Invert nonlinear segment
    if (has_nonlinear) {
        tf_inv.g = 1.0f / src->g;
        tf_inv.a = powf_(1.0f / src->a, src->g);
        tf_inv.b = -tf_inv.a * src->e;
        tf_inv.e = -src->b / src->a;
    }

    if (!has_linear) {
        tf_inv.d = 0;
    } else if (!has_nonlinear) {
        // Any value larger than 1 works
        tf_inv.d = 2.0f;
    } else {
        tf_inv.d = src->c * src->d + src->f;
    }

    *dst = tf_inv;
    return true;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ //

// From here below we're approximating an skcms_Curve with an skcms_TransferFunction{g,a,b,c,d,e,f}:
//
//   tf(x) =  cx + f          x < d
//   tf(x) = (ax + b)^g + e   x ≥ d
//
// When fitting, we add the additional constraint that both pieces meet at d:
//
//   cd + f = (ad + b)^g + e
//
// Solving for e and folding it through gives an alternate formulation of the non-linear piece:
//
//   tf(x) =                           cx + f   x < d
//   tf(x) = (ax + b)^g - (ad + b)^g + cd + f   x ≥ d
//
// Our overall strategy is then:
//    For a couple tolerances,
//       - fit_linear():    fit c,d,f iteratively to as many points as our tolerance allows
//       - invert c,d,f
//       - fit_nonlinear(): fit g,a,b using Gauss-Newton given those inverted c,d,f
//                          (and by constraint, inverted e) to the inverse of the table.
//    Return the parameters with least maximum error.
//
// To run Gauss-Newton to find g,a,b, we'll also need the gradient of the residuals
// of round-trip f_inv(x), the inverse of the non-linear piece of f(x).
//
//    let y = Table(x)
//    r(x) = x - f_inv(y)
//
//    ∂r/∂g = ln(ay + b)*(ay + b)^g
//          - ln(ad + b)*(ad + b)^g
//    ∂r/∂a = yg(ay + b)^(g-1)
//          - dg(ad + b)^(g-1)
//    ∂r/∂b =  g(ay + b)^(g-1)
//          -  g(ad + b)^(g-1)

// Return the residual of roundtripping skcms_Curve(x) through f_inv(y) with parameters P,
// and fill out the gradient of the residual into dfdP.
static float rg_nonlinear(float x,
                          const skcms_Curve* curve,
                          const skcms_TransferFunction* tf,
                          const float P[3],
                          float dfdP[3]) {
    const float y = eval_curve(curve, x);

    const float g = P[0],  a = P[1],  b = P[2],
                c = tf->c, d = tf->d, f = tf->f;

    const float Y = fmaxf_(a*y + b, 0.0f),
                D =        a*d + b;
    assert (D >= 0);

    // The gradient.
    dfdP[0] = 0.69314718f*log2f_(Y)*powf_(Y, g)
            - 0.69314718f*log2f_(D)*powf_(D, g);
    dfdP[1] = y*g*powf_(Y, g-1)
            - d*g*powf_(D, g-1);
    dfdP[2] =   g*powf_(Y, g-1)
            -   g*powf_(D, g-1);

    // The residual.
    const float f_inv = powf_(Y, g)
                      - powf_(D, g)
                      + c*d + f;
    return x - f_inv;
}

static bool gauss_newton_step(const skcms_Curve* curve,
                              const skcms_TransferFunction* tf,
                              float P[3],
                              float x0, float dx, int N) {
    // We'll sample x from the range [x0,x1] (both inclusive) N times with even spacing.
    //
    // We want to do P' = P + (Jf^T Jf)^-1 Jf^T r(P),
    //   where r(P) is the residual vector
    //   and Jf is the Jacobian matrix of f(), ∂r/∂P.
    //
    // Let's review the shape of each of these expressions:
    //   r(P)   is [N x 1], a column vector with one entry per value of x tested
    //   Jf     is [N x 3], a matrix with an entry for each (x,P) pair
    //   Jf^T   is [3 x N], the transpose of Jf
    //
    //   Jf^T Jf   is [3 x N] * [N x 3] == [3 x 3], a 3x3 matrix,
    //                                              and so is its inverse (Jf^T Jf)^-1
    //   Jf^T r(P) is [3 x N] * [N x 1] == [3 x 1], a column vector with the same shape as P
    //
    // Our implementation strategy to get to the final ∆P is
    //   1) evaluate Jf^T Jf,   call that lhs
    //   2) evaluate Jf^T r(P), call that rhs
    //   3) invert lhs
    //   4) multiply inverse lhs by rhs
    //
    // This is a friendly implementation strategy because we don't have to have any
    // buffers that scale with N, and equally nice don't have to perform any matrix
    // operations that are variable size.
    //
    // Other implementation strategies could trade this off, e.g. evaluating the
    // pseudoinverse of Jf ( (Jf^T Jf)^-1 Jf^T ) directly, then multiplying that by
    // the residuals.  That would probably require implementing singular value
    // decomposition, and would create a [3 x N] matrix to be multiplied by the
    // [N x 1] residual vector, but on the upside I think that'd eliminate the
    // possibility of this gauss_newton_step() function ever failing.

    // 0) start off with lhs and rhs safely zeroed.
    skcms_Matrix3x3 lhs = {{ {0,0,0}, {0,0,0}, {0,0,0} }};
    skcms_Vector3   rhs = {  {0,0,0} };

    // 1,2) evaluate lhs and evaluate rhs
    //   We want to evaluate Jf only once, but both lhs and rhs involve Jf^T,
    //   so we'll have to update lhs and rhs at the same time.
    for (int i = 0; i < N; i++) {
        float x = x0 + i*dx;

        float dfdP[3] = {0,0,0};
        float resid = rg_nonlinear(x,curve,tf,P, dfdP);

        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                lhs.vals[r][c] += dfdP[r] * dfdP[c];
            }
            rhs.vals[r] += dfdP[r] * resid;
        }
    }

    // If any of the 3 P parameters are unused, this matrix will be singular.
    // Detect those cases and fix them up to indentity instead, so we can invert.
    for (int k = 0; k < 3; k++) {
        if (lhs.vals[0][k]==0 && lhs.vals[1][k]==0 && lhs.vals[2][k]==0 &&
            lhs.vals[k][0]==0 && lhs.vals[k][1]==0 && lhs.vals[k][2]==0) {
            lhs.vals[k][k] = 1;
        }
    }

    // 3) invert lhs
    skcms_Matrix3x3 lhs_inv;
    if (!skcms_Matrix3x3_invert(&lhs, &lhs_inv)) {
        return false;
    }

    // 4) multiply inverse lhs by rhs
    skcms_Vector3 dP = mv_mul(&lhs_inv, &rhs);
    P[0] += dP.vals[0];
    P[1] += dP.vals[1];
    P[2] += dP.vals[2];
    return isfinitef_(P[0]) && isfinitef_(P[1]) && isfinitef_(P[2]);
}


// Fit the points in [L,N) to the non-linear piece of tf, or return false if we can't.
static bool fit_nonlinear(const skcms_Curve* curve, int L, int N, skcms_TransferFunction* tf) {
    float P[3] = { tf->g, tf->a, tf->b };

    // No matter where we start, dx should always represent N even steps from 0 to 1.
    const float dx = 1.0f / (N-1);

    for (int j = 0; j < 3/*TODO: tune*/; j++) {
        // These extra constraints a >= 0 and ad+b >= 0 are not modeled in the optimization.
        // We don't really know how to fix up a if it goes negative.
        if (P[1] < 0) {
            return false;
        }
        // If ad+b goes negative, we feel just barely not uneasy enough to tweak b so ad+b is zero.
        if (P[1] * tf->d + P[2] < 0) {
            P[2] = -P[1] * tf->d;
        }
        assert (P[1] >= 0 &&
                P[1] * tf->d + P[2] >= 0);

        if (!gauss_newton_step(curve, tf,
                               P,
                               L*dx, dx, N-L)) {
            return false;
        }
    }

    // We need to apply our fixups one last time
    if (P[1] < 0) {
        return false;
    }
    if (P[1] * tf->d + P[2] < 0) {
        P[2] = -P[1] * tf->d;
    }

    tf->g = P[0];
    tf->a = P[1];
    tf->b = P[2];
    tf->e =   tf->c*tf->d + tf->f
      - powf_(tf->a*tf->d + tf->b, tf->g);
    return true;
}

bool skcms_ApproximateCurve(const skcms_Curve* curve,
                            skcms_TransferFunction* approx,
                            float* max_error) {
    if (!curve || !approx || !max_error) {
        return false;
    }

    if (curve->table_entries == 0) {
        // No point approximating an skcms_TransferFunction with an skcms_TransferFunction!
        return false;
    }

    if (curve->table_entries == 1 || curve->table_entries > (uint32_t)INT_MAX) {
        // We need at least two points, and must put some reasonable cap on the maximum number.
        return false;
    }

    int N = (int)curve->table_entries;
    const float dx = 1.0f / (N - 1);

    *max_error = INFINITY_;
    const float kTolerances[] = { 1.5f / 65535.0f, 1.0f / 512.0f };
    for (int t = 0; t < ARRAY_COUNT(kTolerances); t++) {
        skcms_TransferFunction tf,
                               tf_inv;
        int L = fit_linear(curve, N, kTolerances[t], &tf.c, &tf.d, &tf.f);

        if (L == N) {
            // If the entire data set was linear, move the coefficients to the nonlinear portion
            // with G == 1.  This lets use a canonical representation with d == 0.
            tf.g = 1;
            tf.a = tf.c;
            tf.b = tf.f;
            tf.c = tf.d = tf.e = tf.f = 0;
        } else if (L == N - 1) {
            // Degenerate case with only two points in the nonlinear segment. Solve directly.
            tf.g = 1;
            tf.a = (eval_curve(curve, (N-1)*dx) -
                    eval_curve(curve, (N-2)*dx))
                 / dx;
            tf.b = eval_curve(curve, (N-2)*dx)
                 - tf.a * (N-2)*dx;
            tf.e = 0;
        } else {
            // Start by guessing a gamma-only curve through the midpoint.
            int mid = (L + N) / 2;
            float mid_x = mid / (N - 1.0f);
            float mid_y = eval_curve(curve, mid_x);
            tf.g = log2f_(mid_y) / log2f_(mid_x);;
            tf.a = 1;
            tf.b = 0;
            tf.e =    tf.c*tf.d + tf.f
              - powf_(tf.a*tf.d + tf.b, tf.g);


            if (!skcms_TransferFunction_invert(&tf, &tf_inv) ||
                !fit_nonlinear(curve, L,N, &tf_inv)) {
                continue;
            }

            // We fit tf_inv, so calculate tf to keep in sync.
            if (!skcms_TransferFunction_invert(&tf_inv, &tf)) {
                continue;
            }
        }

        // We find our error by roundtripping the table through tf_inv.
        //
        // (The most likely use case for this approximation is to be inverted and
        // used as the transfer function for a destination color space.)
        //
        // We've kept tf and tf_inv in sync above, but we can't guarantee that tf is
        // invertible, so re-verify that here (and use the new inverse for testing).
        if (!skcms_TransferFunction_invert(&tf, &tf_inv)) {
            continue;
        }

        float err = max_roundtrip_error(curve, &tf_inv);
        if (*max_error > err) {
            *max_error = err;
            *approx    = tf;
        }
    }
    return isfinitef_(*max_error);
}

// ~~~~ Impl. of skcms_Transform() ~~~~

typedef enum {
    Op_noop,

    Op_load_a8,
    Op_load_g8,
    Op_load_4444,
    Op_load_565,
    Op_load_888,
    Op_load_8888,
    Op_load_1010102,
    Op_load_161616,
    Op_load_16161616,
    Op_load_hhh,
    Op_load_hhhh,
    Op_load_fff,
    Op_load_ffff,

    Op_swap_rb,
    Op_clamp,
    Op_invert,
    Op_force_opaque,
    Op_premul,
    Op_unpremul,
    Op_matrix_3x3,
    Op_matrix_3x4,
    Op_lab_to_xyz,

    Op_tf_r,
    Op_tf_g,
    Op_tf_b,
    Op_tf_a,

    Op_table_8_r,
    Op_table_8_g,
    Op_table_8_b,
    Op_table_8_a,

    Op_table_16_r,
    Op_table_16_g,
    Op_table_16_b,
    Op_table_16_a,

    Op_clut_3D_8,
    Op_clut_3D_16,
    Op_clut_4D_8,
    Op_clut_4D_16,

    Op_store_a8,
    Op_store_g8,
    Op_store_4444,
    Op_store_565,
    Op_store_888,
    Op_store_8888,
    Op_store_1010102,
    Op_store_161616,
    Op_store_16161616,
    Op_store_hhh,
    Op_store_hhhh,
    Op_store_fff,
    Op_store_ffff,
} Op;

// Friendly always-safe unaligned load and store routines.
template <typename T>
static T load(const void* ptr) {
    T val;
    small_memcpy(&val, ptr, sizeof(val));
    return val;
}
template <typename T>
static void store(void* ptr, T val) {
    small_memcpy(ptr, &val, sizeof(val));
}

// (D)src is sometimes a cast and sometimes a bit pun, but cast() and bit_pun() are always sane.
template <typename D, typename S> static D cast   (S src) { return (D)src; }
template <typename D, typename S> static D bit_pun(S src) {
    static_assert(sizeof(D) == sizeof(S), "");
    return load<D>(&src);
}

// A hook so we can implement (cond ? t : e) for vector types too.
template <typename T> static T if_then_else(bool cond, T t, T e) { return cond ? t : e; }

#if defined(__clang__)
    // Our basic vector type of N T's, e.g. Vec<4,float> is 4 parallel floats.
    template <int N, typename T> using Vec = T __attribute__((ext_vector_type(N)));

    // Vector versions of cast, if_then_else, minus_1_ulp.
    // (The generic bit_pun() works fine for vectors.)
    template <typename D, typename S, int N>
    static Vec<N,D> cast(Vec<N,S> src) {
        return __builtin_convertvector(src, Vec<N,D>);
    }

    template <typename C, typename T>
    static T if_then_else(C cond, T t, T e) {
        return bit_pun<T>(( cond & bit_pun<C>(t)) |
                          (~cond & bit_pun<C>(e)) );
    }

    template <int N>
    static Vec<N,float> minus_1_ulp(Vec<N,float> v) {
        auto bits = bit_pun<Vec<N,int>>(v);
        bits = bits - 1;
        return bit_pun<Vec<N,float>>(bits);
    };
#endif

template <int K, typename P>
struct StridedLoad {
    const P* ptr;

    template <typename T>
    operator T() const { return (T)ptr[0]; }

#if defined(__clang__)
    template <typename T>
    operator Vec<4,T>() const {
        return {
            (T)ptr[ 0*K], (T)ptr[ 1*K], (T)ptr[ 2*K], (T)ptr[ 3*K],
        };
    }

    template <typename T>
    operator Vec<8,T>() const {
        return {
            (T)ptr[ 0*K], (T)ptr[ 1*K], (T)ptr[ 2*K], (T)ptr[ 3*K],
            (T)ptr[ 4*K], (T)ptr[ 5*K], (T)ptr[ 6*K], (T)ptr[ 7*K],
        };
    }

    template <typename T>
    operator Vec<16,T>() const {
        return {
            (T)ptr[ 0*K], (T)ptr[ 1*K], (T)ptr[ 2*K], (T)ptr[ 3*K],
            (T)ptr[ 4*K], (T)ptr[ 5*K], (T)ptr[ 6*K], (T)ptr[ 7*K],
            (T)ptr[ 8*K], (T)ptr[ 9*K], (T)ptr[10*K], (T)ptr[11*K],
            (T)ptr[12*K], (T)ptr[13*K], (T)ptr[14*K], (T)ptr[15*K],
        };
    }
#endif
};

// These load_N handle both scalar T and vector T by dispataching to StridedLoad.
template <typename T, typename P> static T load_3(const P* ptr) { return (T)StridedLoad<3,P>{ptr}; }
template <typename T, typename P> static T load_4(const P* ptr) { return (T)StridedLoad<4,P>{ptr}; }

// These store_N and gather work only for scalar T, but are specialized just below for vectors.
template <typename T, typename P> static void store_3(P* ptr, T v) { *ptr = v; }
template <typename T, typename P> static void store_4(P* ptr, T v) { *ptr = v; }
template <typename T>
static T gather(const uint8_t* ptr, int32_t ix) { return load<T>(ptr + (int)sizeof(T)*ix); }

#if defined(__clang__)
    template <int N, typename T, typename P>
    static void store_3(P* ptr, Vec<N,T> v) {
        #pragma unroll
        for (int i = 0; i < N; i++) {
            *ptr = v[i]; ptr += 3;
        }
    }
    template <int N, typename T, typename P>
    static void store_4(P* ptr, Vec<N,T> v) {
        #pragma unroll
        for (int i = 0; i < N; i++) {
            *ptr = v[i]; ptr += 4;
        }
    }

    // While generic, these gathers are exclusively used with T = uint8_t or uint16_t,
    // so as of 2018 there aren't really any platform specific specializations yet possible.
    template <typename T>
    static Vec<4,T> gather(const uint8_t* ptr, Vec<4,int32_t> ix) {
        auto ld = [=](int i) { return load<T>(ptr + (int)sizeof(T)*i); };
        return { ld(ix[0]), ld(ix[1]), ld(ix[2]), ld(ix[3]) };
    }
    template <typename T>
    static Vec<8,T> gather(const uint8_t* ptr, Vec<8,int32_t> ix) {
        auto ld = [=](int i) { return load<T>(ptr + (int)sizeof(T)*i); };
        return {
            ld(ix[0]), ld(ix[1]), ld(ix[2]), ld(ix[3]),
            ld(ix[4]), ld(ix[5]), ld(ix[6]), ld(ix[7]),
        };
    }
    template <typename T>
    static Vec<16,T> gather(const uint8_t* ptr, Vec<16,int32_t> ix) {
        auto ld = [=](int i) { return load<T>(ptr + (int)sizeof(T)*i); };
        return {
            ld(ix[ 0]), ld(ix[ 1]), ld(ix[ 2]), ld(ix[ 3]),
            ld(ix[ 4]), ld(ix[ 5]), ld(ix[ 6]), ld(ix[ 7]),
            ld(ix[ 8]), ld(ix[ 9]), ld(ix[10]), ld(ix[11]),
            ld(ix[12]), ld(ix[13]), ld(ix[14]), ld(ix[15]),
        };
    }
#endif

template <typename U16>
static U16 swap_endian_16(U16 v) {
    // All 16-bit ICC values are big-endian, so we byte swap before converting to float.
    // MSVC catches the "loss" of data in the portable path, so we also make sure to mask.
    return (U16)( ((v<<8)|(v>>8)) & 0xffff );
}

#if defined(USING_NEON)
    static Vec<4,uint16_t> swap_endian_16(Vec<4,uint16_t> v) {
        return (Vec<4,uint16_t>)vrev16_u8((uint8x8_t) v);
    }
#endif

// Passing by U64* instead of U64 avoids ABI warnings.  It's all moot when inlined.
// TODO: try returning U64 again?
template <typename U64>
static void swap_endian_16(U64* rgba) {
    *rgba = (*rgba & 0x00ff00ff00ff00ff) << 8
          | (*rgba & 0xff00ff00ff00ff00) >> 8;
};

template <typename F> static F min(F x, F y) { return if_then_else(x < y, x, y); };
template <typename F> static F max(F x, F y) { return if_then_else(x < y, y, x); };

// This clut and struct is really just a function, but we use structs
// to allow template specialization on D and T while letting F and I32 remain unspecialized.

template <int D, typename T>
struct clut {
    template <typename F, typename I32>
    void operator()(const void* arg, F& r, F& g, F& b, F a, I32 index, I32 stride) {
        auto a2b = (const skcms_A2B*)arg;
        // We want to sample this dimension at 'x'.
        F src;
        switch (D) {
            case 1: src = r; break;
            case 2: src = g; break;
            case 3: src = b; break;
            case 4: src = a; break;
            default: return;  // unreachable
        }
        I32 limit = a2b->grid_points[D-1];
        F x = max(F(0), min(src, F(1))) * cast<float>(limit - 1);

        // We can't sample at x directly.  Instead interpolate between lo and hi.
        I32 lo = cast<int32_t>(            x      ),
            hi = cast<int32_t>(minus_1_ulp(x+1.0f));

        F lr = r, lg = g, lb = b,
          hr = r, hg = g, hb = b;
        clut<D-1,T>{}(a2b, lr,lg,lb,a, stride*lo + index, stride*limit);
        clut<D-1,T>{}(a2b, hr,hg,hb,a, stride*hi + index, stride*limit);

        F t = x - cast<float>(lo);
        r = lr + (hr-lr)*t;
        g = lg + (hg-lg)*t;
        b = lb + (hb-lb)*t;
    }
};

// Bottom out recursion at 0 dimensions, i.e. return the colors at index.
template <>
struct clut<0,uint8_t> {
    template <typename F, typename I32>
    void operator()(const void* arg, F& r, F& g, F& b, F /*a*/, I32 index, I32 /*stride*/) {
        auto a2b = (const skcms_A2B*)arg;
    #if 1
        r = cast<float>(gather<uint8_t>(a2b->grid_8, 3*index+0)) * (1/255.0f);
        g = cast<float>(gather<uint8_t>(a2b->grid_8, 3*index+1)) * (1/255.0f);
        b = cast<float>(gather<uint8_t>(a2b->grid_8, 3*index+2)) * (1/255.0f);
    #else
        // TODO: gather_24
        U32 rgb = gather_24(a2b->grid_8, ix);
        *r = CAST(F, (rgb >>  0) & 0xff) * (1/255.0f);
        *g = CAST(F, (rgb >>  8) & 0xff) * (1/255.0f);
        *b = CAST(F, (rgb >> 16) & 0xff) * (1/255.0f);
    #endif
    }
};
template <>
struct clut<0,uint16_t> {
    template <typename F, typename I32>
    void operator()(const void* arg, F& r, F& g, F& b, F /*a*/, I32 index, I32 /*stride*/) {
        auto a2b = (const skcms_A2B*)arg;
    #if 1 || defined(__arm__)
        // This is up to 2x faster on 32-bit ARM than the #else-case gather_48() fast path.
        r = cast<float>(swap_endian_16(gather<uint16_t>(a2b->grid_16, 3*index+0))) * (1/65535.0f);
        g = cast<float>(swap_endian_16(gather<uint16_t>(a2b->grid_16, 3*index+1))) * (1/65535.0f);
        b = cast<float>(swap_endian_16(gather<uint16_t>(a2b->grid_16, 3*index+2))) * (1/65535.0f);
    #else
        // TODO: gather_48
        // This strategy is much faster for 64-bit builds, and fine for 32-bit x86 too.
        U64 rgb;
        gather_48(a2b->grid_16, ix, &rgb);
        swap_endian_16x4(&rgb);

        *r = CAST(F, (rgb >>  0) & 0xffff) * (1/65535.0f);
        *g = CAST(F, (rgb >> 16) & 0xffff) * (1/65535.0f);
        *b = CAST(F, (rgb >> 32) & 0xffff) * (1/65535.0f);
    #endif
    }
};

template <typename ISA>
static void run_program(const Op* program, const void** arguments,
                           const char* real_src, char* real_dst, int n) {
    using F   = typename ISA::F;
    using I32 = typename ISA::I32;
    using U64 = typename ISA::U64;
    using U32 = typename ISA::U32;
    using U16 = typename ISA::U16;
    using U8  = typename ISA::U8;

    static const int N = sizeof(F) / sizeof(float);

    auto exec_ops = [](const Op* ops, const void** args, const char* src, char* dst, int i) {

        auto to_fixed = [](F x) { return cast<int32_t>(x + 0.5f); };

        auto approx_log2 = [](F x) {
            // The first approximation of log2(x) is its exponent 'e', minus 127.
            I32 bits = bit_pun<I32>(x);

            F e = cast<float>(bits) * (1.0f / (1<<23));

            // If we use the mantissa too we can refine the error signficantly.
            F m = bit_pun<F>( (bits & 0x007fffff) | 0x3f000000 );

            return e - 124.225514990f
                     -   1.498030302f*m
                     -   1.725879990f/(0.3520887068f + m);
        };
        auto approx_exp2 = [](F x) {
            F fract = x - ISA::Floor(x);

            I32 bits = cast<int32_t>((1.0f * (1<<23)) * (x + 121.274057500f
                                                           -   1.490129070f*fract
                                                           +  27.728023300f/(4.84252568f - fract)));
            return bit_pun<F>(bits);
        };

        auto approx_pow = [&](F x, F y) {
            return if_then_else((x == 0) | (x == 1), x
                                                   , approx_exp2(approx_log2(x) * y));
        };

        auto apply_tf = [&](const skcms_TransferFunction* tf, F x) {
            F sign = if_then_else(x < 0, F(-1), F(1));
            x *= sign;

            F linear    =            tf->c*x + tf->f;
            F nonlinear = approx_pow(tf->a*x + tf->b, tf->g) + tf->e;

            return sign * if_then_else(x < tf->d, linear, nonlinear);
        };

        auto table_8 = [&](const skcms_Curve* curve, F v) {
            // Clamp the input to [0,1], then scale to a table index.
            F ix = max(F(0), min(v, F(1))) * (float)(curve->table_entries - 1);

            // We'll look up (equal or adjacent) entries at lo and hi,
            // then lerp by t between the two.
            I32 lo = cast<int32_t>(            ix   ),
                hi = cast<int32_t>(minus_1_ulp(ix+1));
            F t = ix - cast<float>(lo);  // i.e. the fractional part of ix.

            // TODO: can we load l and h simultaneously?  Each entry in 'h' is either
            // the same as in 'l' or adjacent.  We have a rough idea that's it'd always be safe
            // to read adjacent entries and perhaps underflow the table by a byte or two
            // (it'd be junk, but always safe to read).  Not sure how to lerp yet.
            F l = cast<float>(gather<uint8_t>(curve->table_8, lo)) * (1/255.0f),
              h = cast<float>(gather<uint8_t>(curve->table_8, hi)) * (1/255.0f);
            return l + (h-l)*t;
        };

        auto table_16 = [&](const skcms_Curve* curve, F v) {
            // All just as in table_8() until the gathers.
            F ix = max(F(0), min(v, F(1))) * (float)(curve->table_entries - 1);

            I32 lo = cast<int32_t>(            ix   ),
                hi = cast<int32_t>(minus_1_ulp(ix+1));
            F t = ix - cast<float>(lo);

            // TODO: as above, load l and h simultaneously?
            // Here we could even use AVX2-style 32-bit gathers.
            F l = cast<float>(swap_endian_16(gather<uint16_t>(curve->table_16, lo))) * (1/65535.0f),
              h = cast<float>(swap_endian_16(gather<uint16_t>(curve->table_16, hi))) * (1/65535.0f);
            return l + (h-l)*t;
        };

        F r = 0, g = 0, b = 0, a = 0;
        while (true) {
            switch (*ops++) {
                case Op_noop: break;

                case Op_load_a8:{
                    a = cast<float>(load<U8>(src + 1*i)) * (1/255.0f);
                } break;

                case Op_load_g8:{
                    r = g = b = cast<float>(load<U8>(src + 1*i)) * (1/255.0f);
                } break;

                case Op_load_4444:{
                    U16 abgr = load<U16>(src + 2*i);

                    r = cast<float>((abgr >> 12) & 0xf) * (1/15.0f);
                    g = cast<float>((abgr >>  8) & 0xf) * (1/15.0f);
                    b = cast<float>((abgr >>  4) & 0xf) * (1/15.0f);
                    a = cast<float>((abgr >>  0) & 0xf) * (1/15.0f);
                } break;

                case Op_load_565:{
                    U16 rgb = load<U16>(src + 2*i);

                    r = cast<float>(rgb & (uint16_t)(31<< 0)) * (1.0f / (31<< 0));
                    g = cast<float>(rgb & (uint16_t)(63<< 5)) * (1.0f / (63<< 5));
                    b = cast<float>(rgb & (uint16_t)(31<<11)) * (1.0f / (31<<11));
                    a = 1;
                } break;

                case Op_load_888:{
                    const uint8_t* rgb = (const uint8_t*)(src + 3*i);
                #if defined(USING_NEON)
                    // There's no uint8x4x3_t or vld3 load for it, so we'll load each rgb pixel one at
                    // a time.  Since we're doing that, we might as well load them into 16-bit lanes.
                    // (We'd even load into 32-bit lanes, but that's not possible on ARMv7.)
                    uint8x8x3_t v = {{ vdup_n_u8(0), vdup_n_u8(0), vdup_n_u8(0) }};
                    v = vld3_lane_u8(rgb+0, v, 0);
                    v = vld3_lane_u8(rgb+3, v, 2);
                    v = vld3_lane_u8(rgb+6, v, 4);
                    v = vld3_lane_u8(rgb+9, v, 6);

                    // Now if we squint, those 3 uint8x8_t we constructed are really U16s, easy to
                    // convert to F.  (Again, U32 would be even better here if drop ARMv7 or split
                    // ARMv7 and ARMv8 impls.)
                    r = cast<float>((U16)v.val[0]) * (1/255.0f);
                    g = cast<float>((U16)v.val[1]) * (1/255.0f);
                    b = cast<float>((U16)v.val[2]) * (1/255.0f);
                #else
                    r = cast<float>(load_3<U32>(rgb+0)) * (1/255.0f);
                    g = cast<float>(load_3<U32>(rgb+1)) * (1/255.0f);
                    b = cast<float>(load_3<U32>(rgb+2)) * (1/255.0f);
                #endif
                    a = 1;
                } break;

                case Op_load_8888:{
                    U32 rgba = load<U32>(src + 4*i);

                    r = cast<float>((rgba >>  0) & 0xff) * (1/255.0f);
                    g = cast<float>((rgba >>  8) & 0xff) * (1/255.0f);
                    b = cast<float>((rgba >> 16) & 0xff) * (1/255.0f);
                    a = cast<float>((rgba >> 24) & 0xff) * (1/255.0f);
                } break;

                case Op_load_1010102:{
                    U32 rgba = load<U32>(src + 4*i);

                    r = cast<float>((rgba >>  0) & 0x3ff) * (1/1023.0f);
                    g = cast<float>((rgba >> 10) & 0x3ff) * (1/1023.0f);
                    b = cast<float>((rgba >> 20) & 0x3ff) * (1/1023.0f);
                    a = cast<float>((rgba >> 30) & 0x3  ) * (1/   3.0f);
                } break;

                case Op_load_161616:{
                    uintptr_t ptr = (uintptr_t)(src + 6*i);
                    assert( (ptr & 1) == 0 );                   // src must be 2-byte aligned for this
                    const uint16_t* rgb = (const uint16_t*)ptr; // cast to const uint16_t* to be safe.
                #if defined(USING_NEON)
                    uint16x4x3_t v = vld3_u16(rgb);
                    r = cast<float>(swap_endian_16((U16)v.val[0])) * (1/65535.0f);
                    g = cast<float>(swap_endian_16((U16)v.val[1])) * (1/65535.0f);
                    b = cast<float>(swap_endian_16((U16)v.val[2])) * (1/65535.0f);
                #else
                    U32 R = load_3<U32>(rgb+0),
                        G = load_3<U32>(rgb+1),
                        B = load_3<U32>(rgb+2);
                    // R,G,B are big-endian 16-bit, so byte swap them before converting to float.
                    r = cast<float>((R & 0x00ff)<<8 | (R & 0xff00)>>8) * (1/65535.0f);
                    g = cast<float>((G & 0x00ff)<<8 | (G & 0xff00)>>8) * (1/65535.0f);
                    b = cast<float>((B & 0x00ff)<<8 | (B & 0xff00)>>8) * (1/65535.0f);
                #endif
                    a = 1;
                } break;

                case Op_load_16161616:{
                    uintptr_t ptr = (uintptr_t)(src + 8*i);
                    assert( (ptr & 1) == 0 );                    // src must be 2-byte aligned for this
                    const uint16_t* rgba = (const uint16_t*)ptr; // cast to const uint16_t* to be safe.
                #if defined(USING_NEON)
                    uint16x4x4_t v = vld4_u16(rgba);
                    r = cast<float>(swap_endian_16((U16)v.val[0])) * (1/65535.0f);
                    g = cast<float>(swap_endian_16((U16)v.val[1])) * (1/65535.0f);
                    b = cast<float>(swap_endian_16((U16)v.val[2])) * (1/65535.0f);
                    a = cast<float>(swap_endian_16((U16)v.val[3])) * (1/65535.0f);
                #else
                    U64 px = load<U64>(rgba);

                    swap_endian_16(&px);
                    r = cast<float>((px >>  0) & 0xffff) * (1/65535.0f);
                    g = cast<float>((px >> 16) & 0xffff) * (1/65535.0f);
                    b = cast<float>((px >> 32) & 0xffff) * (1/65535.0f);
                    a = cast<float>((px >> 48) & 0xffff) * (1/65535.0f);
                #endif
                } break;

                case Op_load_hhh:{
                    uintptr_t ptr = (uintptr_t)(src + 6*i);
                    assert( (ptr & 1) == 0 );                   // src must be 2-byte aligned for this
                    const uint16_t* rgb = (const uint16_t*)ptr; // cast to const uint16_t* to be safe.
                #if defined(USING_NEON)
                    uint16x4x3_t v = vld3_u16(rgb);
                    U16 R = (U16)v.val[0],
                        G = (U16)v.val[1],
                        B = (U16)v.val[2];
                #else
                    U16 R = load_3<U16>(rgb+0),
                        G = load_3<U16>(rgb+1),
                        B = load_3<U16>(rgb+2);
                #endif
                    r = ISA::F_from_Half(R);
                    g = ISA::F_from_Half(G);
                    b = ISA::F_from_Half(B);
                    a = 1;
                } break;

                case Op_load_hhhh:{
                    uintptr_t ptr = (uintptr_t)(src + 8*i);
                    assert( (ptr & 1) == 0 );                    // src must be 2-byte aligned for this
                    const uint16_t* rgba = (const uint16_t*)ptr; // cast to const uint16_t* to be safe.
                #if defined(USING_NEON)
                    uint16x4x4_t v = vld4_u16(rgba);
                    U16 R = (U16)v.val[0],
                        G = (U16)v.val[1],
                        B = (U16)v.val[2],
                        A = (U16)v.val[3];
                #else
                    U64 px = load<U64>(rgba);
                    U16 R = cast<uint16_t>((px >>  0) & 0xffff),
                        G = cast<uint16_t>((px >> 16) & 0xffff),
                        B = cast<uint16_t>((px >> 32) & 0xffff),
                        A = cast<uint16_t>((px >> 48) & 0xffff);
                #endif
                    r = ISA::F_from_Half(R);
                    g = ISA::F_from_Half(G);
                    b = ISA::F_from_Half(B);
                    a = ISA::F_from_Half(A);
                } break;

                case Op_load_fff:{
                    uintptr_t ptr = (uintptr_t)(src + 12*i);
                    assert( (ptr & 3) == 0 );                   // src must be 4-byte aligned for this
                    const float* rgb = (const float*)ptr;       // cast to const float* to be safe.
                #if defined(USING_NEON)
                    float32x4x3_t v = vld3q_f32(rgb);
                    r = (F)v.val[0];
                    g = (F)v.val[1];
                    b = (F)v.val[2];
                #else
                    r = load_3<F>(rgb+0);
                    g = load_3<F>(rgb+1);
                    b = load_3<F>(rgb+2);
                #endif
                    a = 1;
                } break;

                case Op_load_ffff:{
                    uintptr_t ptr = (uintptr_t)(src + 16*i);
                    assert( (ptr & 3) == 0 );                   // src must be 4-byte aligned for this
                    const float* rgba = (const float*)ptr;      // cast to const float* to be safe.
                #if defined(USING_NEON)
                    float32x4x4_t v = vld4q_f32(rgba);
                    r = (F)v.val[0];
                    g = (F)v.val[1];
                    b = (F)v.val[2];
                    a = (F)v.val[3];
                #else
                    r = load_4<F>(rgba+0);
                    g = load_4<F>(rgba+1);
                    b = load_4<F>(rgba+2);
                    a = load_4<F>(rgba+3);
                #endif
                } break;

                case Op_swap_rb:{
                    F t = r;
                    r = b;
                    b = t;
                } break;

                case Op_clamp:{
                    r = max(F(0), min(r, F(1)));
                    g = max(F(0), min(g, F(1)));
                    b = max(F(0), min(b, F(1)));
                    a = max(F(0), min(a, F(1)));
                } break;

                case Op_invert:{
                    r = 1 - r;
                    g = 1 - g;
                    b = 1 - b;
                    a = 1 - a;
                } break;

                case Op_force_opaque:{
                    a = 1;
                } break;

                case Op_premul:{
                    r *= a;
                    g *= a;
                    b *= a;
                } break;

                case Op_unpremul:{
                    F scale = if_then_else(1.0f / a < INFINITY_, 1.0f / a, F(0));
                    r *= scale;
                    g *= scale;
                    b *= scale;
                } break;

                case Op_matrix_3x3:{
                    const skcms_Matrix3x3* matrix = (const skcms_Matrix3x3*) *args++;
                    const float* m = &matrix->vals[0][0];

                    F R = m[0]*r + m[1]*g + m[2]*b,
                      G = m[3]*r + m[4]*g + m[5]*b,
                      B = m[6]*r + m[7]*g + m[8]*b;

                    r = R;
                    g = G;
                    b = B;
                } break;

                case Op_matrix_3x4:{
                    const skcms_Matrix3x4* matrix = (const skcms_Matrix3x4*) *args++;
                    const float* m = &matrix->vals[0][0];

                    F R = m[0]*r + m[1]*g + m[ 2]*b + m[ 3],
                      G = m[4]*r + m[5]*g + m[ 6]*b + m[ 7],
                      B = m[8]*r + m[9]*g + m[10]*b + m[11];

                    r = R;
                    g = G;
                    b = B;
                } break;

                case Op_lab_to_xyz:{
                    // The L*a*b values are in r,g,b, but normalized to [0,1].  Reconstruct them:
                    F L = r * 100.0f,
                      A = g * 255.0f - 128.0f,
                      B = b * 255.0f - 128.0f;

                    // Convert to CIE XYZ.
                    F Y = (L + 16.0f) * (1/116.0f),
                      X = Y + A*(1/500.0f),
                      Z = Y - B*(1/200.0f);

                    X = if_then_else(X*X*X > 0.008856f, X*X*X, (X - (16/116.0f)) * (1/7.787f));
                    Y = if_then_else(Y*Y*Y > 0.008856f, Y*Y*Y, (Y - (16/116.0f)) * (1/7.787f));
                    Z = if_then_else(Z*Z*Z > 0.008856f, Z*Z*Z, (Z - (16/116.0f)) * (1/7.787f));

                    // Adjust to XYZD50 illuminant, and stuff back into r,g,b for the next op.
                    r = X * 0.9642f;
                    g = Y          ;
                    b = Z * 0.8249f;
                } break;

                case Op_tf_r:{ r = apply_tf((const skcms_TransferFunction*)*args++, r); } break;
                case Op_tf_g:{ g = apply_tf((const skcms_TransferFunction*)*args++, g); } break;
                case Op_tf_b:{ b = apply_tf((const skcms_TransferFunction*)*args++, b); } break;
                case Op_tf_a:{ a = apply_tf((const skcms_TransferFunction*)*args++, a); } break;

                case Op_table_8_r: { r = table_8((const skcms_Curve*)*args++, r); } break;
                case Op_table_8_g: { g = table_8((const skcms_Curve*)*args++, g); } break;
                case Op_table_8_b: { b = table_8((const skcms_Curve*)*args++, b); } break;
                case Op_table_8_a: { a = table_8((const skcms_Curve*)*args++, a); } break;

                case Op_table_16_r:{ r = table_16((const skcms_Curve*)*args++, r); } break;
                case Op_table_16_g:{ g = table_16((const skcms_Curve*)*args++, g); } break;
                case Op_table_16_b:{ b = table_16((const skcms_Curve*)*args++, b); } break;
                case Op_table_16_a:{ a = table_16((const skcms_Curve*)*args++, a); } break;

                case Op_clut_3D_8:  clut<3, uint8_t>{}(*args++, r,g,b,a, I32(0),I32(1)); break;
                case Op_clut_3D_16: clut<3,uint16_t>{}(*args++, r,g,b,a, I32(0),I32(1)); break;
                case Op_clut_4D_8:  clut<4, uint8_t>{}(*args++, r,g,b,a, I32(0),I32(1));
                                    a = 1; // 'a' was really CMYK K, so our output is opaque.
                                    break;
                case Op_clut_4D_16: clut<4,uint16_t>{}(*args++, r,g,b,a, I32(0),I32(1));
                                    a = 1; // 'a' was really CMYK K, so our output is opaque.
                                    break;

        // Notice, from here on down the store_ ops all return, ending the loop.

                case Op_store_a8: {
                    store(dst + 1*i, cast<uint8_t>(to_fixed(a * 255)));
                } return;

                case Op_store_g8: {
                    // g should be holding luminance (Y) (r,g,b ~~~> X,Y,Z)
                    store(dst + 1*i, cast<uint8_t>(to_fixed(g * 255)));
                } return;

                case Op_store_4444: {
                    store(dst + 2*i, cast<uint16_t>(to_fixed(r * 15) << 12)
                                   | cast<uint16_t>(to_fixed(g * 15) <<  8)
                                   | cast<uint16_t>(to_fixed(b * 15) <<  4)
                                   | cast<uint16_t>(to_fixed(a * 15) <<  0));
                } return;

                case Op_store_565: {
                    store(dst + 2*i, cast<uint16_t>(to_fixed(r * 31) <<  0 )
                                   | cast<uint16_t>(to_fixed(g * 63) <<  5 )
                                   | cast<uint16_t>(to_fixed(b * 31) << 11 ));
                } return;

                case Op_store_888: {
                    uint8_t* rgb = (uint8_t*)dst + 3*i;
                #if defined(USING_NEON)
                    // Same deal as load_888 but in reverse... we'll store using uint8x8x3_t, but
                    // get there via U16 to save some instructions converting to float.  And just
                    // like load_888, we'd prefer to go via U32 but for ARMv7 support.
                    U16 R = cast<uint16_t>(to_fixed(r * 255)),
                        G = cast<uint16_t>(to_fixed(g * 255)),
                        B = cast<uint16_t>(to_fixed(b * 255));

                    uint8x8x3_t v = {{ (uint8x8_t)R, (uint8x8_t)G, (uint8x8_t)B }};
                    vst3_lane_u8(rgb+0, v, 0);
                    vst3_lane_u8(rgb+3, v, 2);
                    vst3_lane_u8(rgb+6, v, 4);
                    vst3_lane_u8(rgb+9, v, 6);
                #else
                    store_3(rgb+0, cast<uint8_t>(to_fixed(r * 255)) );
                    store_3(rgb+1, cast<uint8_t>(to_fixed(g * 255)) );
                    store_3(rgb+2, cast<uint8_t>(to_fixed(b * 255)) );
                #endif
                } return;

                case Op_store_8888: {
                    store(dst + 4*i, cast<uint32_t>(to_fixed(r * 255) <<  0)
                                   | cast<uint32_t>(to_fixed(g * 255) <<  8)
                                   | cast<uint32_t>(to_fixed(b * 255) << 16)
                                   | cast<uint32_t>(to_fixed(a * 255) << 24));
                } return;

                case Op_store_1010102: {
                    store(dst + 4*i, cast<uint32_t>(to_fixed(r * 1023) <<  0)
                                   | cast<uint32_t>(to_fixed(g * 1023) << 10)
                                   | cast<uint32_t>(to_fixed(b * 1023) << 20)
                                   | cast<uint32_t>(to_fixed(a *    3) << 30));
                } return;

                case Op_store_161616: {
                    uintptr_t ptr = (uintptr_t)(dst + 6*i);
                    assert( (ptr & 1) == 0 );                // The dst pointer must be 2-byte aligned
                    uint16_t* rgb = (uint16_t*)ptr;          // for this cast to uint16_t* to be safe.
                #if defined(USING_NEON)
                    uint16x4x3_t v = {{
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(r * 65535))),
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(g * 65535))),
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(b * 65535))),
                    }};
                    vst3_u16(rgb, v);
                #else
                    I32 R = to_fixed(r * 65535),
                        G = to_fixed(g * 65535),
                        B = to_fixed(b * 65535);
                    store_3(rgb+0, cast<uint16_t>((R & 0x00ff) << 8 | (R & 0xff00) >> 8) );
                    store_3(rgb+1, cast<uint16_t>((G & 0x00ff) << 8 | (G & 0xff00) >> 8) );
                    store_3(rgb+2, cast<uint16_t>((B & 0x00ff) << 8 | (B & 0xff00) >> 8) );
                #endif
                } return;

                case Op_store_16161616: {
                    uintptr_t ptr = (uintptr_t)(dst + 8*i);
                    assert( (ptr & 1) == 0 );               // The dst pointer must be 2-byte aligned
                    uint16_t* rgba = (uint16_t*)ptr;        // for this cast to uint16_t* to be safe.
                #if defined(USING_NEON)
                    uint16x4x4_t v = {{
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(r * 65535))),
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(g * 65535))),
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(b * 65535))),
                        (uint16x4_t)swap_endian_16(cast<uint16_t>(to_fixed(a * 65535))),
                    }};
                    vst4_u16(rgba, v);
                #else
                    U64 px = cast<uint64_t>(to_fixed(r * 65535)) <<  0
                           | cast<uint64_t>(to_fixed(g * 65535)) << 16
                           | cast<uint64_t>(to_fixed(b * 65535)) << 32
                           | cast<uint64_t>(to_fixed(a * 65535)) << 48;
                    swap_endian_16(&px);
                    store(rgba, px);
                #endif
                } return;

                case Op_store_hhh: {
                    uintptr_t ptr = (uintptr_t)(dst + 6*i);
                    assert( (ptr & 1) == 0 );                // The dst pointer must be 2-byte aligned
                    uint16_t* rgb = (uint16_t*)ptr;          // for this cast to uint16_t* to be safe.

                    U16 R = ISA::Half_from_F(r),
                        G = ISA::Half_from_F(g),
                        B = ISA::Half_from_F(b);
                #if defined(USING_NEON)
                    uint16x4x3_t v = {{
                        (uint16x4_t)R,
                        (uint16x4_t)G,
                        (uint16x4_t)B,
                    }};
                    vst3_u16(rgb, v);
                #else
                    store_3(rgb+0, R);
                    store_3(rgb+1, G);
                    store_3(rgb+2, B);
                #endif
                } return;

                case Op_store_hhhh: {
                    uintptr_t ptr = (uintptr_t)(dst + 8*i);
                    assert( (ptr & 1) == 0 );                // The dst pointer must be 2-byte aligned
                    uint16_t* rgba = (uint16_t*)ptr;         // for this cast to uint16_t* to be safe.

                    U16 R = ISA::Half_from_F(r),
                        G = ISA::Half_from_F(g),
                        B = ISA::Half_from_F(b),
                        A = ISA::Half_from_F(a);
                #if defined(USING_NEON)
                    uint16x4x4_t v = {{
                        (uint16x4_t)R,
                        (uint16x4_t)G,
                        (uint16x4_t)B,
                        (uint16x4_t)A,
                    }};
                    vst4_u16(rgba, v);
                #else
                    U64 px = cast<uint64_t>(R) <<  0
                           | cast<uint64_t>(G) << 16
                           | cast<uint64_t>(B) << 32
                           | cast<uint64_t>(A) << 48;
                    store(rgba, px);
                #endif
                } return;

                case Op_store_fff: {
                    uintptr_t ptr = (uintptr_t)(dst + 12*i);
                    assert( (ptr & 3) == 0 );                // The dst pointer must be 4-byte aligned
                    float* rgb = (float*)ptr;                // for this cast to float* to be safe.
                #if defined(USING_NEON)
                    float32x4x3_t v = {{
                        (float32x4_t)r,
                        (float32x4_t)g,
                        (float32x4_t)b,
                    }};
                    vst3q_f32(rgb, v);
                #else
                    store_3(rgb+0, r);
                    store_3(rgb+1, g);
                    store_3(rgb+2, b);
                #endif
                } return;

                case Op_store_ffff: {
                    uintptr_t ptr = (uintptr_t)(dst + 16*i);
                    assert( (ptr & 3) == 0 );                // The dst pointer must be 4-byte aligned
                    float* rgba = (float*)ptr;               // for this cast to float* to be safe.
                #if defined(USING_NEON)
                    float32x4x4_t v = {{
                        (float32x4_t)r,
                        (float32x4_t)g,
                        (float32x4_t)b,
                        (float32x4_t)a,
                    }};
                    vst4q_f32(rgba, v);
                #else
                    store_4(rgba+0, r);
                    store_4(rgba+1, g);
                    store_4(rgba+2, b);
                    store_4(rgba+3, a);
                #endif
                } return;
            }
        }
    };

    for (int i = 0; n >= N; i += N,
                            n -= N) {
        exec_ops(program, arguments, real_src, real_dst, i);
    }
}

// Without this wasm would try to use the N=4 128-bit vector code path,
// which while ideal, causes tons of compiler problems.  This would be
// a good thing to revisit as emcc matures (currently 1.38.5).
#if 1 && defined(__EMSCRIPTEN_major__)
    #if !defined(SKCMS_PORTABLE)
        #define  SKCMS_PORTABLE
    #endif
#endif

template <typename F, typename U16>
static F approx_F_from_Half(U16 half) {
    using U32 = decltype(cast<uint32_t>(half));

    // A half is 1-5-10 sign-exponent-mantissa, with 15 exponent bias.
    U32 wide = cast<uint32_t>(half),
         s  = wide & 0x8000,
         em = wide ^ s;

    // Constructing the float is easy if the half is not denormalized.
    F norm = bit_pun<F>( (s<<16) + (em<<13) + ((127-15)<<23) );

    // Simply flush all denorm half floats to zero.
    return if_then_else(em < 0x0400, F(0), norm);
}

template <typename U16, typename F>
static U16 approx_Half_from_F(F f) {
    using U32 = decltype(cast<uint32_t>(f));

    // A float is 1-8-23 sign-exponent-mantissa, with 127 exponent bias.
    U32 sem = bit_pun<U32>(f),
        s   = sem & 0x80000000,
         em = sem ^ s;

    // For simplicity we flush denorm half floats (including all denorm floats) to zero.
    return cast<uint16_t>(if_then_else(em < 0x38800000,
                          U32(0),
                          (s>>16) + (em>>13) - ((127-15)<<10)));
}

struct Scalar {
    using F   =    float;
    using I32 =  int32_t;
    using U64 = uint64_t;
    using U32 = uint32_t;
    using U16 = uint16_t;
    using U8  =  uint8_t;

    static void RunProgram(const Op* program, const void** arguments,
                           const char* src, char* dst, int n) {
        run_program<Scalar>(program, arguments, src,dst,n);
    }

    static F Floor(F x) { return floorf_(x); }

    static F   F_from_Half(U16 half) { return approx_F_from_Half<F>  (half); }
    static U16 Half_from_F(F      f) { return approx_Half_from_F<U16>(   f); }
};

#if !defined(SKCMS_PORTABLE) && defined(__clang__)
    #if defined(__ARM_NEON)
        #include <arm_neon.h>
    #elif defined(__SSE__)
        #include <immintrin.h>
    #endif

    struct Generic_4xFloat {
        using F   = Vec<4,    float>;
        using I32 = Vec<4,  int32_t>;
        using U64 = Vec<4, uint64_t>;
        using U32 = Vec<4, uint32_t>;
        using U16 = Vec<4, uint16_t>;
        using U8  = Vec<4,  uint8_t>;

        static void RunProgram(const Op* program, const void** arguments,
                               const char* src, char* dst, int n) {
            run_program<Generic_4xFloat>(program, arguments, src,dst,n);
        }

        static F Floor(F x) {
        #if defined(__aarch64__)
            return vrndmq_f32(x);
        #elif defined(__SSE4_1__)
            return _mm_floor_ps(x);
        #else
            // This implementation fails for values of x that are outside
            // the range an integer can represent.  We expect most x to be small.

            // Round trip through integers with a truncating cast.
            F roundtrip = cast<float>(cast<int32_t>(x));
            // If x is negative, truncating gives the ceiling instead of the floor.
            return roundtrip - if_then_else(roundtrip > x, F(1), F(0));
        #endif
        }

        #if defined(__ARM_NEON) && (__ARM_FP & 2)
            static F   F_from_Half(U16 half) { return      vcvt_f32_f16((float16x4_t)half); }
            static U16 Half_from_F(F      f) { return (U16)vcvt_f16_f32(                f); }
        #else
            static F   F_from_Half(U16 half) { return approx_F_from_Half<F>  (half); }
            static U16 Half_from_F(F      f) { return approx_Half_from_F<U16>(   f); }
        #endif
    };

    #if defined(__x86_64__)
        struct HSW {
            using F   = Vec<8,    float>;
            using I32 = Vec<8,  int32_t>;
            using U64 = Vec<8, uint64_t>;
            using U32 = Vec<8, uint32_t>;
            using U16 = Vec<8, uint16_t>;
            using U8  = Vec<8,  uint8_t>;

            // We use target("avx2,f16c") instead of target("arch=haswell")
            // so that FMA instructions are _not_ used (for consistency with other platforms).

            __attribute__((target("avx2,f16c"), flatten))
            static void RunProgram(const Op* program, const void** arguments,
                                   const char* src, char* dst, int n) {
                run_program<HSW>(program, arguments, src,dst,n);
            }

            __attribute__((target("avx2,f16c"), flatten))
            static F Floor(F x) {
                return _mm256_floor_ps(x);
            }

            __attribute__((target("avx2,f16c"), flatten))
            static F F_from_Half(U16 half) { return _mm256_cvtph_ps(half); }

            __attribute__((target("avx2,f16c"), flatten))
            static U16 Half_from_F(F f) { return _mm256_cvtps_ph(f, _MM_FROUND_CUR_DIRECTION); }
        };

        struct SKX {
            using F   = Vec<16,    float>;
            using I32 = Vec<16,  int32_t>;
            using U64 = Vec<16, uint64_t>;
            using U32 = Vec<16, uint32_t>;
            using U16 = Vec<16, uint16_t>;
            using U8  = Vec<16,  uint8_t>;

            // TODO: we'll want to work out how to avoid FMA code generation here.

            __attribute__((target("arch=skylake-avx512"), flatten))
            static void RunProgram(const Op* program, const void** arguments,
                                   const char* src, char* dst, int n) {
                run_program<SKX>(program, arguments, src,dst,n);
            }

            __attribute__((target("arch=skylake-avx512"), flatten))
            static F Floor(F x) {
                return _mm512_floor_ps(x);
            }

            __attribute__((target("arch=skylake-avx512"), flatten))
            static F F_from_Half(U16 half) { return _mm512_cvtph_ps(half); }

            __attribute__((target("arch=skylake-avx512"), flatten))
            static U16 Half_from_F(F f) { return _mm512_cvtps_ph(f, _MM_FROUND_CUR_DIRECTION); }
        };

        enum CpuFeatures {
            HSW  = 1 << 0,
            SKX  = 1 << 1,
        };
        static CpuFeatures cpu_features() {
            static const CpuFeatures runtime_features = []{
                CpuFeatures features = (CpuFeatures)0;
                // See http://www.sandpile.org/x86/cpuid.htm

                // First, a basic cpuid(1).
                uint32_t eax, ebx, ecx, edx;
                __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                                             : "0"(1), "2"(0));

                // Sanity check for prerequisites.
                if ((edx & (1<<25)) != (1<<25)) { return features; }   // SSE
                if ((edx & (1<<26)) != (1<<26)) { return features; }   // SSE2
                if ((ecx & (1<< 0)) != (1<< 0)) { return features; }   // SSE3
                if ((ecx & (1<< 9)) != (1<< 9)) { return features; }   // SSSE3
                if ((ecx & (1<<19)) != (1<<19)) { return features; }   // SSE4.1
                if ((ecx & (1<<20)) != (1<<20)) { return features; }   // SSE4.2

                if ((ecx & (3<<26)) != (3<<26)) { return features; }   // XSAVE + OSXSAVE

                {
                    // XMM+YMM state saved?
                    uint32_t eax_xgetbv, edx_xgetbv;
                    __asm__ __volatile__("xgetbv" : "=a"(eax_xgetbv), "=d"(edx_xgetbv) : "c"(0));
                    if ((eax_xgetbv & (3<<1)) != (3<<1)) { return features; }
                }

                if ((ecx & (1<<28)) != (1<<28)) { return features; }   // AVX
                if ((ecx & (1<<29)) != (1<<29)) { return features; }   // F16C
                if ((ecx & (1<<12)) != (1<<12)) { return features; }   // FMA  (not currently used)

                // Call cpuid(7) to check for our final AVX2 feature bit!
                __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                                             : "0"(7), "2"(0));
                if ((ebx & (1<< 5)) != (1<< 5)) { return features; }   // AVX2

                // OK!  We have everything we need to run the HSW code.
                features = (CpuFeatures)( features | CpuFeatures::HSW );

                // TODO: SKX detection
                return features;
            }();

            CpuFeatures features = runtime_features;
        #if defined(__AVX2__)
            features = (CpuFeatures)( features | CpuFeatures::HSW );
        #endif
        #if defined(__AVX512F__)
            features = (CpuFeatures)( features | CpuFeatures::SKX );
        #endif
            return features;
        }

    #endif  // defined(__x86_64__)
#endif // !defined(SKCMS_PORTABLE) && defined(__clang__)

static bool is_identity_tf(const skcms_TransferFunction* tf) {
    return tf->g == 1 && tf->a == 1
        && tf->b == 0 && tf->c == 0 && tf->d == 0 && tf->e == 0 && tf->f == 0;
}

typedef struct {
    Op          op;
    const void* arg;
} OpAndArg;

static OpAndArg select_curve_op(const skcms_Curve* curve, int channel) {
    static const struct { Op parametric, table_8, table_16; } ops[] = {
        { Op_tf_r, Op_table_8_r, Op_table_16_r },
        { Op_tf_g, Op_table_8_g, Op_table_16_g },
        { Op_tf_b, Op_table_8_b, Op_table_16_b },
        { Op_tf_a, Op_table_8_a, Op_table_16_a },
    };

    if (curve->table_entries == 0) {
        return is_identity_tf(&curve->parametric)
            ? OpAndArg{ Op_noop, nullptr }
            : OpAndArg{ ops[channel].parametric, &curve->parametric };
    } else if (curve->table_8) {
        return OpAndArg{ ops[channel].table_8,  curve };
    } else if (curve->table_16) {
        return OpAndArg{ ops[channel].table_16, curve };
    }

    assert(false);
    return OpAndArg{Op_noop,nullptr};
}

static size_t bytes_per_pixel(skcms_PixelFormat fmt) {
    switch (fmt >> 1) {   // ignore rgb/bgr
        case skcms_PixelFormat_A_8           >> 1: return  1;
        case skcms_PixelFormat_G_8           >> 1: return  1;
        case skcms_PixelFormat_ABGR_4444     >> 1: return  2;
        case skcms_PixelFormat_RGB_565       >> 1: return  2;
        case skcms_PixelFormat_RGB_888       >> 1: return  3;
        case skcms_PixelFormat_RGBA_8888     >> 1: return  4;
        case skcms_PixelFormat_RGBA_1010102  >> 1: return  4;
        case skcms_PixelFormat_RGB_161616    >> 1: return  6;
        case skcms_PixelFormat_RGBA_16161616 >> 1: return  8;
        case skcms_PixelFormat_RGB_hhh       >> 1: return  6;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: return  8;
        case skcms_PixelFormat_RGB_fff       >> 1: return 12;
        case skcms_PixelFormat_RGBA_ffff     >> 1: return 16;
    }
    assert(false);
    return 0;
}

static bool prep_for_destination(const skcms_ICCProfile* profile,
                                 skcms_Matrix3x3* fromXYZD50,
                                 skcms_TransferFunction* invR,
                                 skcms_TransferFunction* invG,
                                 skcms_TransferFunction* invB) {
    // We only support destinations with parametric transfer functions
    // and with gamuts that can be transformed from XYZD50.
    return profile->has_trc
        && profile->has_toXYZD50
        && profile->trc[0].table_entries == 0
        && profile->trc[1].table_entries == 0
        && profile->trc[2].table_entries == 0
        && skcms_TransferFunction_invert(&profile->trc[0].parametric, invR)
        && skcms_TransferFunction_invert(&profile->trc[1].parametric, invG)
        && skcms_TransferFunction_invert(&profile->trc[2].parametric, invB)
        && skcms_Matrix3x3_invert(&profile->toXYZD50, fromXYZD50);
}

bool skcms_Transform(const void*             src,
                     skcms_PixelFormat       srcFmt,
                     skcms_AlphaFormat       srcAlpha,
                     const skcms_ICCProfile* srcProfile,
                     void*                   dst,
                     skcms_PixelFormat       dstFmt,
                     skcms_AlphaFormat       dstAlpha,
                     const skcms_ICCProfile* dstProfile,
                     size_t                  nz) {
    const size_t dst_bpp = bytes_per_pixel(dstFmt),
                 src_bpp = bytes_per_pixel(srcFmt);
    // Let's just refuse if the request is absurdly big.
    if (nz * dst_bpp > INT_MAX || nz * src_bpp > INT_MAX) {
        return false;
    }
    int n = (int)nz;

    // Null profiles default to sRGB. Passing null for both is handy when doing format conversion.
    if (!srcProfile) {
        srcProfile = skcms_sRGB_profile();
    }
    if (!dstProfile) {
        dstProfile = skcms_sRGB_profile();
    }

    // We can't transform in place unless the PixelFormats are the same size.
    if (dst == src && (dstFmt >> 1) != (srcFmt >> 1)) {
        return false;
    }
    // TODO: this check lazilly disallows U16 <-> F16, but that would actually be fine.
    // TODO: more careful alias rejection (like, dst == src + 1)?

    Op          program  [32];
    const void* arguments[32];

    Op*          ops  = program;
    const void** args = arguments;

    skcms_TransferFunction inv_dst_tf_r, inv_dst_tf_g, inv_dst_tf_b;
    skcms_Matrix3x3        from_xyz;

    switch (srcFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_A_8           >> 1: *ops++ = Op_load_a8;       break;
        case skcms_PixelFormat_G_8           >> 1: *ops++ = Op_load_g8;       break;
        case skcms_PixelFormat_ABGR_4444     >> 1: *ops++ = Op_load_4444;     break;
        case skcms_PixelFormat_RGB_565       >> 1: *ops++ = Op_load_565;      break;
        case skcms_PixelFormat_RGB_888       >> 1: *ops++ = Op_load_888;      break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ops++ = Op_load_8888;     break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ops++ = Op_load_1010102;  break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ops++ = Op_load_161616;   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ops++ = Op_load_16161616; break;
        case skcms_PixelFormat_RGB_hhh       >> 1: *ops++ = Op_load_hhh;      break;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: *ops++ = Op_load_hhhh;     break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ops++ = Op_load_fff;      break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ops++ = Op_load_ffff;     break;
    }
    if (srcFmt & 1) {
        *ops++ = Op_swap_rb;
    }
    skcms_ICCProfile gray_dst_profile;
    if ((dstFmt >> 1) == (skcms_PixelFormat_G_8 >> 1)) {
        // When transforming to gray, stop at XYZ (by setting toXYZ to identity), then transform
        // luminance (Y) by the destination transfer function.
        gray_dst_profile = *dstProfile;
        skcms_SetXYZD50(&gray_dst_profile, &skcms_XYZD50_profile()->toXYZD50);
        dstProfile = &gray_dst_profile;
    }

    if (srcProfile->data_color_space == skcms_Signature_CMYK) {
        // Photoshop creates CMYK images as inverse CMYK.
        // These happen to be the only ones we've _ever_ seen.
        *ops++ = Op_invert;
        // With CMYK, ignore the alpha type, to avoid changing K or conflating CMY with K.
        srcAlpha = skcms_AlphaFormat_Unpremul;
    }

    if (srcAlpha == skcms_AlphaFormat_Opaque) {
        *ops++ = Op_force_opaque;
    } else if (srcAlpha == skcms_AlphaFormat_PremulAsEncoded) {
        *ops++ = Op_unpremul;
    }

    // TODO: We can skip this work if both srcAlpha and dstAlpha are PremulLinear, and the profiles
    // are the same. Also, if dstAlpha is PremulLinear, and SrcAlpha is Opaque.
    if (dstProfile != srcProfile ||
        srcAlpha == skcms_AlphaFormat_PremulLinear ||
        dstAlpha == skcms_AlphaFormat_PremulLinear) {

        if (!prep_for_destination(dstProfile,
                                  &from_xyz, &inv_dst_tf_r, &inv_dst_tf_b, &inv_dst_tf_g)) {
            return false;
        }

        if (srcProfile->has_A2B) {
            if (srcProfile->A2B.input_channels) {
                for (int i = 0; i < (int)srcProfile->A2B.input_channels; i++) {
                    OpAndArg oa = select_curve_op(&srcProfile->A2B.input_curves[i], i);
                    if (oa.op != Op_noop) {
                        *ops++  = oa.op;
                        *args++ = oa.arg;
                    }
                }
                switch (srcProfile->A2B.input_channels) {
                    case 3: *ops++ = srcProfile->A2B.grid_8 ? Op_clut_3D_8 : Op_clut_3D_16; break;
                    case 4: *ops++ = srcProfile->A2B.grid_8 ? Op_clut_4D_8 : Op_clut_4D_16; break;
                    default: return false;
                }
                *args++ = &srcProfile->A2B;
            }

            if (srcProfile->A2B.matrix_channels == 3) {
                for (int i = 0; i < 3; i++) {
                    OpAndArg oa = select_curve_op(&srcProfile->A2B.matrix_curves[i], i);
                    if (oa.op != Op_noop) {
                        *ops++  = oa.op;
                        *args++ = oa.arg;
                    }
                }

                static const skcms_Matrix3x4 I = {{
                    {1,0,0,0},
                    {0,1,0,0},
                    {0,0,1,0},
                }};
                if (0 != memcmp(&I, &srcProfile->A2B.matrix, sizeof(I))) {
                    *ops++  = Op_matrix_3x4;
                    *args++ = &srcProfile->A2B.matrix;
                }
            }

            if (srcProfile->A2B.output_channels == 3) {
                for (int i = 0; i < 3; i++) {
                    OpAndArg oa = select_curve_op(&srcProfile->A2B.output_curves[i], i);
                    if (oa.op != Op_noop) {
                        *ops++  = oa.op;
                        *args++ = oa.arg;
                    }
                }
            }

            if (srcProfile->pcs == skcms_Signature_Lab) {
                *ops++ = Op_lab_to_xyz;
            }

        } else if (srcProfile->has_trc && srcProfile->has_toXYZD50) {
            for (int i = 0; i < 3; i++) {
                OpAndArg oa = select_curve_op(&srcProfile->trc[i], i);
                if (oa.op != Op_noop) {
                    *ops++  = oa.op;
                    *args++ = oa.arg;
                }
            }
        } else {
            return false;
        }

        // At this point our source colors are linear, either RGB (XYZ-type profiles)
        // or XYZ (A2B-type profiles). Unpremul is a linear operation (multiply by a
        // constant 1/a), so either way we can do it now if needed.
        if (srcAlpha == skcms_AlphaFormat_PremulLinear) {
            *ops++ = Op_unpremul;
        }

        // A2B sources should already be in XYZD50 at this point.
        // Others still need to be transformed using their toXYZD50 matrix.
        // N.B. There are profiles that contain both A2B tags and toXYZD50 matrices.
        // If we use the A2B tags, we need to ignore the XYZD50 matrix entirely.
        assert (srcProfile->has_A2B || srcProfile->has_toXYZD50);
        static const skcms_Matrix3x3 I = {{
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f },
        }};
        const skcms_Matrix3x3* to_xyz = srcProfile->has_A2B ? &I : &srcProfile->toXYZD50;

        // There's a chance the source and destination gamuts are identical,
        // in which case we can skip the gamut transform.
        if (0 != memcmp(&dstProfile->toXYZD50, to_xyz, sizeof(skcms_Matrix3x3))) {
            // Concat the entire gamut transform into from_xyz,
            // now slightly misnamed but it's a handy spot to stash the result.
            from_xyz = skcms_Matrix3x3_concat(&from_xyz, to_xyz);
            *ops++  = Op_matrix_3x3;
            *args++ = &from_xyz;
        }

        if (dstAlpha == skcms_AlphaFormat_PremulLinear) {
            *ops++ = Op_premul;
        }

        // Encode back to dst RGB using its parametric transfer functions.
        if (!is_identity_tf(&inv_dst_tf_r)) { *ops++ = Op_tf_r; *args++ = &inv_dst_tf_r; }
        if (!is_identity_tf(&inv_dst_tf_g)) { *ops++ = Op_tf_g; *args++ = &inv_dst_tf_g; }
        if (!is_identity_tf(&inv_dst_tf_b)) { *ops++ = Op_tf_b; *args++ = &inv_dst_tf_b; }
    }

    if (dstAlpha == skcms_AlphaFormat_Opaque) {
        *ops++ = Op_force_opaque;
    } else if (dstAlpha == skcms_AlphaFormat_PremulAsEncoded) {
        *ops++ = Op_premul;
    }
    if (dstFmt & 1) {
        *ops++ = Op_swap_rb;
    }
    if (dstFmt < skcms_PixelFormat_RGB_hhh) {
        *ops++ = Op_clamp;
    }
    switch (dstFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_A_8           >> 1: *ops++ = Op_store_a8;       break;
        case skcms_PixelFormat_G_8           >> 1: *ops++ = Op_store_g8;       break;
        case skcms_PixelFormat_ABGR_4444     >> 1: *ops++ = Op_store_4444;     break;
        case skcms_PixelFormat_RGB_565       >> 1: *ops++ = Op_store_565;      break;
        case skcms_PixelFormat_RGB_888       >> 1: *ops++ = Op_store_888;      break;
        case skcms_PixelFormat_RGBA_8888     >> 1: *ops++ = Op_store_8888;     break;
        case skcms_PixelFormat_RGBA_1010102  >> 1: *ops++ = Op_store_1010102;  break;
        case skcms_PixelFormat_RGB_161616    >> 1: *ops++ = Op_store_161616;   break;
        case skcms_PixelFormat_RGBA_16161616 >> 1: *ops++ = Op_store_16161616; break;
        case skcms_PixelFormat_RGB_hhh       >> 1: *ops++ = Op_store_hhh;      break;
        case skcms_PixelFormat_RGBA_hhhh     >> 1: *ops++ = Op_store_hhhh;     break;
        case skcms_PixelFormat_RGB_fff       >> 1: *ops++ = Op_store_fff;      break;
        case skcms_PixelFormat_RGBA_ffff     >> 1: *ops++ = Op_store_ffff;     break;
    }

    auto run = Scalar::RunProgram;
    int N = 1;

#if !defined(SKCMS_PORTABLE) && defined(__clang__)
    run = Generic_4xFloat::RunProgram;
    N   = 4;
    #if defined(__x86_64__)
        if (cpu_features() & CpuFeatures::HSW) { run = HSW::RunProgram; N =  8; }
        if (cpu_features() & CpuFeatures::SKX) { run = SKX::RunProgram; N = 16; }
    #endif
#endif

    // Handle as many full strides of N pixels as we can.
    int body = (n / N) * N;
    run(program, arguments, (const char*)src, (char*)dst, body);
    n -= body;

    if (n > 0) {
        // Handle the rest by copying into stride-sized buffers on the stack.
        float tmp_src[4*16] = {0},
              tmp_dst[4*16];
        memcpy(tmp_src, (const char*)src + (size_t)body * src_bpp, (size_t)n * src_bpp);
        run(program, arguments, (const char*)tmp_src, (char*)tmp_dst, N);
        memcpy((char*)dst + (size_t)body * dst_bpp, tmp_dst, (size_t)n * dst_bpp);
    }
    return true;
}

static void assert_usable_as_destination(const skcms_ICCProfile* profile) {
#if defined(NDEBUG)
    (void)profile;
#else
    skcms_Matrix3x3 fromXYZD50;
    skcms_TransferFunction invR, invG, invB;
    assert(prep_for_destination(profile, &fromXYZD50, &invR, &invG, &invB));
#endif
}

bool skcms_MakeUsableAsDestination(skcms_ICCProfile* profile) {
    skcms_Matrix3x3 fromXYZD50;
    if (!profile->has_trc || !profile->has_toXYZD50
        || !skcms_Matrix3x3_invert(&profile->toXYZD50, &fromXYZD50)) {
        return false;
    }

    skcms_TransferFunction tf[3];
    for (int i = 0; i < 3; i++) {
        skcms_TransferFunction inv;
        if (profile->trc[i].table_entries == 0
            && skcms_TransferFunction_invert(&profile->trc[i].parametric, &inv)) {
            tf[i] = profile->trc[i].parametric;
            continue;
        }

        float max_error;
        // Parametric curves from skcms_ApproximateCurve() are guaranteed to be invertible.
        if (!skcms_ApproximateCurve(&profile->trc[i], &tf[i], &max_error)) {
            return false;
        }
    }

    for (int i = 0; i < 3; ++i) {
        profile->trc[i].table_entries = 0;
        profile->trc[i].parametric = tf[i];
    }

    assert_usable_as_destination(profile);
    return true;
}

bool skcms_MakeUsableAsDestinationWithSingleCurve(skcms_ICCProfile* profile) {
    // Operate on a copy of profile, so we can choose the best TF for the original curves
    skcms_ICCProfile result = *profile;
    if (!skcms_MakeUsableAsDestination(&result)) {
        return false;
    }

    int best_tf = 0;
    float min_max_error = INFINITY_;
    for (int i = 0; i < 3; i++) {
        skcms_TransferFunction inv;
        skcms_TransferFunction_invert(&result.trc[i].parametric, &inv);

        float err = 0;
        for (int j = 0; j < 3; ++j) {
            err = fmaxf_(err, max_roundtrip_error(&profile->trc[j], &inv));
        }
        if (min_max_error > err) {
            min_max_error = err;
            best_tf = i;
        }
    }

    for (int i = 0; i < 3; i++) {
        result.trc[i].parametric = result.trc[best_tf].parametric;
    }

    *profile = result;
    assert_usable_as_destination(profile);
    return true;
}
