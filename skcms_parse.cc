/*
 * Copyright 2026 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/skcms_public.h"  // NO_G3_REWRITE
#include "src/skcms_internals.h"  // NO_G3_REWRITE
#include <stdint.h>
#include <string.h>

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
    return static_cast<float>(read_big_i32(ptr)) * (1.0f / 65536.0f);
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

bool skcms_GetWTPT(const skcms_ICCProfile* profile, float xyz[3]) {
    skcms_ICCTag tag;
    return skcms_GetTagBySignature(profile, skcms_Signature_WTPT, &tag) &&
           read_tag_xyz(&tag, &xyz[0], &xyz[1], &xyz[2]);
}

static int data_color_space_channel_count(uint32_t data_color_space) {
    switch (data_color_space) {
        case skcms_Signature_CMYK:   return 4;
        case skcms_Signature_Gray:   return 1;
        case skcms_Signature_RGB:    return 3;
        case skcms_Signature_Lab:    return 3;
        case skcms_Signature_XYZ:    return 3;
        case skcms_Signature_CIELUV: return 3;
        case skcms_Signature_YCbCr:  return 3;
        case skcms_Signature_CIEYxy: return 3;
        case skcms_Signature_HSV:    return 3;
        case skcms_Signature_HLS:    return 3;
        case skcms_Signature_CMY:    return 3;
        case skcms_Signature_2CLR:   return 2;
        case skcms_Signature_3CLR:   return 3;
        case skcms_Signature_4CLR:   return 4;
        case skcms_Signature_5CLR:   return 5;
        case skcms_Signature_6CLR:   return 6;
        case skcms_Signature_7CLR:   return 7;
        case skcms_Signature_8CLR:   return 8;
        case skcms_Signature_9CLR:   return 9;
        case skcms_Signature_10CLR:  return 10;
        case skcms_Signature_11CLR:  return 11;
        case skcms_Signature_12CLR:  return 12;
        case skcms_Signature_13CLR:  return 13;
        case skcms_Signature_14CLR:  return 14;
        case skcms_Signature_15CLR:  return 15;
        default:                     return -1;
    }
}

int skcms_GetInputChannelCount(const skcms_ICCProfile* profile) {
    int a2b_count = 0;
    if (profile->has_A2B) {
        a2b_count = profile->A2B.input_channels != 0
                        ? static_cast<int>(profile->A2B.input_channels)
                        : 3;
    }

    skcms_ICCTag tag;
    int trc_count = 0;
    if (skcms_GetTagBySignature(profile, skcms_Signature_kTRC, &tag)) {
        trc_count = 1;
    } else if (profile->has_trc) {
        trc_count = 3;
    }

    int dcs_count = data_color_space_channel_count(profile->data_color_space);

    if (dcs_count < 0) {
        return -1;
    }

    if (a2b_count > 0 && a2b_count != dcs_count) {
        return -1;
    }
    if (trc_count > 0 && trc_count != dcs_count) {
        return -1;
    }

    return dcs_count;
}

static bool read_to_XYZD50(const skcms_ICCTag* rXYZ, const skcms_ICCTag* gXYZ,
                           const skcms_ICCTag* bXYZ, skcms_Matrix3x3* toXYZ) {
    return read_tag_xyz(rXYZ, &toXYZ->vals[0][0], &toXYZ->vals[1][0], &toXYZ->vals[2][0]) &&
           read_tag_xyz(gXYZ, &toXYZ->vals[0][1], &toXYZ->vals[1][1], &toXYZ->vals[2][1]) &&
           read_tag_xyz(bXYZ, &toXYZ->vals[0][2], &toXYZ->vals[1][2], &toXYZ->vals[2][2]);
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
    return skcms_TransferFunction_isSRGBish(&curve->parametric);
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
        return true;
    }
    if (value_count > kMaxTableEntries) {
        return false;
    }
    curve->table_8       = nullptr;
    curve->table_16      = curvTag->variable;
    curve->table_entries = value_count;
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
    a2b-> input_channels = mftTag-> input_channels[0];
    a2b->output_channels = mftTag->output_channels[0];

    // We require exactly three (ie XYZ/Lab/RGB) output channels
    if (a2b->output_channels != ARRAY_COUNT(a2b->output_curves)) {
        return false;
    }
    // We require at least one, and no more than four (ie CMYK) input channels
    if (a2b->input_channels < 1 || a2b->input_channels > ARRAY_COUNT(a2b->input_curves)) {
        return false;
    }

    uint64_t total_grid_points = 1;
    for (uint32_t i = 0; i < a2b->input_channels; ++i) {
        a2b->grid_points[i] = mftTag->grid_points[0];
        total_grid_points *= a2b->grid_points[i];
    }
    // The grid only makes sense with at least two points along each axis
    if (a2b->grid_points[0] < 2 || total_grid_points > SKCMS_MAX_GRID_POINTS) {
        return false;
    }
    return true;
}

// All as the A2B version above, except where noted.
static bool read_mft_common(const mft_CommonLayout* mftTag, skcms_B2A* b2a) {
    // Same as A2B.
    b2a->matrix_channels = 0;
    b2a-> input_channels = mftTag-> input_channels[0];
    b2a->output_channels = mftTag->output_channels[0];


    // For B2A, exactly 3 input channels (XYZ) and 3 (RGB) or 4 (CMYK) output channels.
    if (b2a->input_channels != ARRAY_COUNT(b2a->input_curves)) {
        return false;
    }
    if (b2a->output_channels < 3 || b2a->output_channels > ARRAY_COUNT(b2a->output_curves)) {
        return false;
    }

    // Same as A2B.
    uint64_t total_grid_points = 1;
    for (uint32_t i = 0; i < b2a->input_channels; ++i) {
        b2a->grid_points[i] = mftTag->grid_points[0];
        total_grid_points *= b2a->grid_points[i];
    }
    if (b2a->grid_points[0] < 2 || total_grid_points > SKCMS_MAX_GRID_POINTS) {
        return false;
    }
    return true;
}

template <typename A2B_or_B2A>
static bool init_tables(const uint8_t* table_base, uint64_t max_tables_len, uint32_t byte_width,
                        uint32_t input_table_entries, uint32_t output_table_entries,
                        A2B_or_B2A* out) {
    // byte_width is 1 or 2, [input|output]_table_entries are in [2, 4096], so no overflow
    uint32_t byte_len_per_input_table  = input_table_entries * byte_width;
    uint32_t byte_len_per_output_table = output_table_entries * byte_width;

    // [input|output]_channels are <= 4, so still no overflow
    uint32_t byte_len_all_input_tables  = out->input_channels * byte_len_per_input_table;
    uint32_t byte_len_all_output_tables = out->output_channels * byte_len_per_output_table;

    uint64_t grid_size = out->output_channels * byte_width;
    for (uint32_t axis = 0; axis < out->input_channels; ++axis) {
        grid_size *= out->grid_points[axis];
    }

    if (max_tables_len < byte_len_all_input_tables + grid_size + byte_len_all_output_tables) {
        return false;
    }

    for (uint32_t i = 0; i < out->input_channels; ++i) {
        out->input_curves[i].table_entries = input_table_entries;
        if (byte_width == 1) {
            out->input_curves[i].table_8  = table_base + i * byte_len_per_input_table;
            out->input_curves[i].table_16 = nullptr;
        } else {
            out->input_curves[i].table_8  = nullptr;
            out->input_curves[i].table_16 = table_base + i * byte_len_per_input_table;
        }
    }

    if (byte_width == 1) {
        out->grid_8  = table_base + byte_len_all_input_tables;
        out->grid_16 = nullptr;
    } else {
        out->grid_8  = nullptr;
        out->grid_16 = table_base + byte_len_all_input_tables;
    }

    const uint8_t* output_table_base = table_base + byte_len_all_input_tables + grid_size;
    for (uint32_t i = 0; i < out->output_channels; ++i) {
        out->output_curves[i].table_entries = output_table_entries;
        if (byte_width == 1) {
            out->output_curves[i].table_8  = output_table_base + i * byte_len_per_output_table;
            out->output_curves[i].table_16 = nullptr;
        } else {
            out->output_curves[i].table_8  = nullptr;
            out->output_curves[i].table_16 = output_table_base + i * byte_len_per_output_table;
        }
    }

    return true;
}

template <typename A2B_or_B2A>
static bool read_tag_mft1(const skcms_ICCTag* tag, A2B_or_B2A* out) {
    if (tag->size < SAFE_FIXED_SIZE(mft1_Layout)) {
        return false;
    }

    const mft1_Layout* mftTag = (const mft1_Layout*)tag->buf;
    if (!read_mft_common(mftTag->common, out)) {
        return false;
    }

    uint32_t input_table_entries  = 256;
    uint32_t output_table_entries = 256;

    return init_tables(mftTag->variable, tag->size - SAFE_FIXED_SIZE(mft1_Layout), 1,
                       input_table_entries, output_table_entries, out);
}

template <typename A2B_or_B2A>
static bool read_tag_mft2(const skcms_ICCTag* tag, A2B_or_B2A* out) {
    if (tag->size < SAFE_FIXED_SIZE(mft2_Layout)) {
        return false;
    }

    const mft2_Layout* mftTag = (const mft2_Layout*)tag->buf;
    if (!read_mft_common(mftTag->common, out)) {
        return false;
    }

    uint32_t input_table_entries = read_big_u16(mftTag->input_table_entries);
    uint32_t output_table_entries = read_big_u16(mftTag->output_table_entries);

    // ICC spec mandates that 2 <= table_entries <= 4096
    if (input_table_entries < 2 || input_table_entries > 4096 ||
        output_table_entries < 2 || output_table_entries > 4096) {
        return false;
    }

    return init_tables(mftTag->variable, tag->size - SAFE_FIXED_SIZE(mft2_Layout), 2,
                       input_table_entries, output_table_entries, out);
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

// mAB and mBA tags use the same encoding, including color lookup tables.
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
} mAB_or_mBA_Layout;

typedef struct {
    uint8_t grid_points          [16];
    uint8_t grid_byte_width      [ 1];
    uint8_t reserved             [ 3];
    uint8_t variable             [1/*variable*/];
} CLUT_Layout;

static bool read_tag_mab(const skcms_ICCTag* tag, skcms_A2B* a2b, bool pcs_is_xyz,
                         const uint8_t* endOfBuffer) {
    if (tag->size < SAFE_SIZEOF(mAB_or_mBA_Layout)) {
        return false;
    }

    const mAB_or_mBA_Layout* mABTag = (const mAB_or_mBA_Layout*)tag->buf;

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
        float encoding_factor = pcs_is_xyz ? (65535 / 32768.0f) : 1.0f;
        const uint8_t* mtx_buf = tag->buf + matrix_offset;
        a2b->matrix.vals[0][0] = encoding_factor * read_big_fixed(mtx_buf +  0);
        a2b->matrix.vals[0][1] = encoding_factor * read_big_fixed(mtx_buf +  4);
        a2b->matrix.vals[0][2] = encoding_factor * read_big_fixed(mtx_buf +  8);
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

        if (tag->size < clut_offset + SAFE_FIXED_SIZE(CLUT_Layout)) {
            return false;
        }
        const CLUT_Layout* clut = (const CLUT_Layout*)(tag->buf + clut_offset);

        if (clut->grid_byte_width[0] == 1) {
            a2b->grid_8  = clut->variable;
            a2b->grid_16 = nullptr;
        } else if (clut->grid_byte_width[0] == 2) {
            a2b->grid_8  = nullptr;
            a2b->grid_16 = clut->variable;
        } else {
            return false;
        }

        uint64_t grid_size = a2b->output_channels * clut->grid_byte_width[0];  // the payload
        uint64_t total_grid_points = 1;
        for (uint32_t i = 0; i < a2b->input_channels; ++i) {
            a2b->grid_points[i] = clut->grid_points[i];
            // The grid only makes sense with at least two points along each axis
            if (a2b->grid_points[i] < 2) {
                return false;
            }
            grid_size *= a2b->grid_points[i];
            total_grid_points *= a2b->grid_points[i];
        }

        if (total_grid_points > SKCMS_MAX_GRID_POINTS) {
            return false;
        }

        const uint64_t table_size = clut_offset + SAFE_FIXED_SIZE(CLUT_Layout) + grid_size;
        if (table_size > tag->size) {
            return false;
        }

        // gather_24 and gather_48 read 1 or 2 extra bytes.
        // We must ensure that those extra bytes are within the provided buffer limit.
        uint32_t slack = 0;
        if (a2b->output_channels == 3) {
            slack = clut->grid_byte_width[0] == 1 ? 1 : 2;
        }
        if (tag->buf + table_size + slack > endOfBuffer) {
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

// Exactly the same as read_tag_mab(), except where there are comments.
// TODO: refactor the two to eliminate common code?
static bool read_tag_mba(const skcms_ICCTag* tag, skcms_B2A* b2a, bool pcs_is_xyz,
                         const uint8_t* endOfBuffer) {
    if (tag->size < SAFE_SIZEOF(mAB_or_mBA_Layout)) {
        return false;
    }

    const mAB_or_mBA_Layout* mBATag = (const mAB_or_mBA_Layout*)tag->buf;

    b2a->input_channels  = mBATag->input_channels[0];
    b2a->output_channels = mBATag->output_channels[0];

    // Require exactly 3 inputs (XYZ) and 3 (RGB) or 4 (CMYK) outputs.
    if (b2a->input_channels != ARRAY_COUNT(b2a->input_curves)) {
        return false;
    }
    if (b2a->output_channels < 3 || b2a->output_channels > ARRAY_COUNT(b2a->output_curves)) {
        return false;
    }

    uint32_t b_curve_offset = read_big_u32(mBATag->b_curve_offset);
    uint32_t matrix_offset  = read_big_u32(mBATag->matrix_offset);
    uint32_t m_curve_offset = read_big_u32(mBATag->m_curve_offset);
    uint32_t clut_offset    = read_big_u32(mBATag->clut_offset);
    uint32_t a_curve_offset = read_big_u32(mBATag->a_curve_offset);

    if (0 == b_curve_offset) {
        return false;
    }

    // "B" curves are our inputs, not outputs.
    if (!read_curves(tag->buf, tag->size, b_curve_offset, b2a->input_channels,
                     b2a->input_curves)) {
        return false;
    }

    if (0 != m_curve_offset) {
        if (0 == matrix_offset) {
            return false;
        }
        // Matrix channels is tied to input_channels (3), not output_channels.
        b2a->matrix_channels = b2a->input_channels;

        if (!read_curves(tag->buf, tag->size, m_curve_offset, b2a->matrix_channels,
                         b2a->matrix_curves)) {
            return false;
        }

        if (tag->size < matrix_offset + 12 * SAFE_SIZEOF(uint32_t)) {
            return false;
        }
        float encoding_factor = pcs_is_xyz ? (32768 / 65535.0f) : 1.0f;  // TODO: understand
        const uint8_t* mtx_buf = tag->buf + matrix_offset;
        b2a->matrix.vals[0][0] = encoding_factor * read_big_fixed(mtx_buf +  0);
        b2a->matrix.vals[0][1] = encoding_factor * read_big_fixed(mtx_buf +  4);
        b2a->matrix.vals[0][2] = encoding_factor * read_big_fixed(mtx_buf +  8);
        b2a->matrix.vals[1][0] = encoding_factor * read_big_fixed(mtx_buf + 12);
        b2a->matrix.vals[1][1] = encoding_factor * read_big_fixed(mtx_buf + 16);
        b2a->matrix.vals[1][2] = encoding_factor * read_big_fixed(mtx_buf + 20);
        b2a->matrix.vals[2][0] = encoding_factor * read_big_fixed(mtx_buf + 24);
        b2a->matrix.vals[2][1] = encoding_factor * read_big_fixed(mtx_buf + 28);
        b2a->matrix.vals[2][2] = encoding_factor * read_big_fixed(mtx_buf + 32);
        b2a->matrix.vals[0][3] = encoding_factor * read_big_fixed(mtx_buf + 36);
        b2a->matrix.vals[1][3] = encoding_factor * read_big_fixed(mtx_buf + 40);
        b2a->matrix.vals[2][3] = encoding_factor * read_big_fixed(mtx_buf + 44);
    } else {
        if (0 != matrix_offset) {
            return false;
        }
        b2a->matrix_channels = 0;
    }

    if (0 != a_curve_offset) {
        if (0 == clut_offset) {
            return false;
        }

        // "A" curves are our output, not input.
        if (!read_curves(tag->buf, tag->size, a_curve_offset, b2a->output_channels,
                         b2a->output_curves)) {
            return false;
        }

        if (tag->size < clut_offset + SAFE_FIXED_SIZE(CLUT_Layout)) {
            return false;
        }
        const CLUT_Layout* clut = (const CLUT_Layout*)(tag->buf + clut_offset);

        if (clut->grid_byte_width[0] == 1) {
            b2a->grid_8  = clut->variable;
            b2a->grid_16 = nullptr;
        } else if (clut->grid_byte_width[0] == 2) {
            b2a->grid_8  = nullptr;
            b2a->grid_16 = clut->variable;
        } else {
            return false;
        }

        uint64_t grid_size = b2a->output_channels * clut->grid_byte_width[0];
        uint64_t total_grid_points = 1;
        for (uint32_t i = 0; i < b2a->input_channels; ++i) {
            b2a->grid_points[i] = clut->grid_points[i];
            if (b2a->grid_points[i] < 2) {
                return false;
            }
            grid_size *= b2a->grid_points[i];
            total_grid_points *= b2a->grid_points[i];
        }

        if (total_grid_points > SKCMS_MAX_GRID_POINTS) {
            return false;
        }

        if (tag->size < clut_offset + SAFE_FIXED_SIZE(CLUT_Layout) + grid_size) {
            return false;
        }

        // gather_24 and gather_48 read 1 or 2 extra bytes.
        // We must ensure that those extra bytes are within the provided buffer limit.
        uint32_t slack = 0;
        if (b2a->output_channels == 3) {
            slack = clut->grid_byte_width[0] == 1 ? 1 : 2;
        }
        if (tag->buf + clut_offset + SAFE_FIXED_SIZE(CLUT_Layout) + grid_size + slack > endOfBuffer) {
            return false;
        }
    } else {
        if (0 != clut_offset) {
            return false;
        }

        if (b2a->input_channels != b2a->output_channels) {
            return false;
        }

        // Zero out *output* channels to skip this stage.
        b2a->output_channels = 0;
    }
    return true;
}

static bool read_a2b(const skcms_ICCTag* tag, skcms_A2B* a2b, bool pcs_is_xyz, const uint8_t* eob) {
    if (tag->type == skcms_Signature_mft1) { return read_tag_mft1(tag, a2b); }
    if (tag->type == skcms_Signature_mft2) { return read_tag_mft2(tag, a2b); }
    if (tag->type == skcms_Signature_mAB ) { return read_tag_mab(tag, a2b, pcs_is_xyz, eob); }
    return false;
}

static bool read_b2a(const skcms_ICCTag* tag, skcms_B2A* b2a, bool pcs_is_xyz, const uint8_t* eob) {
    if (tag->type == skcms_Signature_mft1) { return read_tag_mft1(tag, b2a); }
    if (tag->type == skcms_Signature_mft2) { return read_tag_mft2(tag, b2a); }
    if (tag->type == skcms_Signature_mBA ) { return read_tag_mba(tag, b2a, pcs_is_xyz, eob); }
    return false;
}

typedef struct {
    uint8_t type                     [4];
    uint8_t reserved                 [4];
    uint8_t color_primaries          [1];
    uint8_t transfer_characteristics [1];
    uint8_t matrix_coefficients      [1];
    uint8_t video_full_range_flag    [1];
} CICP_Layout;

static bool read_cicp(const skcms_ICCTag* tag, skcms_CICP* cicp) {
    if (tag->type != skcms_Signature_CICP || tag->size < SAFE_SIZEOF(CICP_Layout)) {
        return false;
    }

    const CICP_Layout* cicpTag = (const CICP_Layout*)tag->buf;

    cicp->color_primaries          = cicpTag->color_primaries[0];
    cicp->transfer_characteristics = cicpTag->transfer_characteristics[0];
    cicp->matrix_coefficients      = cicpTag->matrix_coefficients[0];
    cicp->video_full_range_flag    = cicpTag->video_full_range_flag[0];
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

bool skcms_ParseWithA2BPriority_internal(const void* buf, size_t len,
                                         const int priority[], const int priorities,
                                         skcms_ICCProfile* profile) {
    static_assert(SAFE_SIZEOF(header_Layout) == 132, "need to update header code");

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

    const uint8_t* endOfBuffer = (const uint8_t*)buf + len;
    for (int i = 0; i < priorities; i++) {
        // enum { perceptual, relative_colormetric, saturation }
        if (priority[i] < 0 || priority[i] > 2) {
            return false;
        }
        uint32_t sig = skcms_Signature_A2B0 + static_cast<uint32_t>(priority[i]);
        skcms_ICCTag tag;
        if (skcms_GetTagBySignature(profile, sig, &tag)) {
            if (!read_a2b(&tag, &profile->A2B, pcs_is_xyz, endOfBuffer)) {
                // Malformed A2B tag
                return false;
            }
            profile->has_A2B = true;
            break;
        }
    }

    for (int i = 0; i < priorities; i++) {
        // enum { perceptual, relative_colormetric, saturation }
        if (priority[i] < 0 || priority[i] > 2) {
            return false;
        }
        uint32_t sig = skcms_Signature_B2A0 + static_cast<uint32_t>(priority[i]);
        skcms_ICCTag tag;
        if (skcms_GetTagBySignature(profile, sig, &tag)) {
            if (!read_b2a(&tag, &profile->B2A, pcs_is_xyz, endOfBuffer)) {
                // Malformed B2A tag
                return false;
            }
            profile->has_B2A = true;
            break;
        }
    }

    skcms_ICCTag cicp_tag;
    if (skcms_GetTagBySignature(profile, skcms_Signature_CICP, &cicp_tag)) {
        if (!read_cicp(&cicp_tag, &profile->CICP)) {
            // Malformed CICP tag
            return false;
        }
        profile->has_CICP = true;
    }

    return usable_as_src(profile);
}
