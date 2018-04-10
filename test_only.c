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
#include "src/PortableMath.h"
#include "src/TransferFunction.h"
#include "test_only.h"
#include <stdlib.h>
#include <string.h>

static void dump_transform_to_XYZD50(FILE* fp, const skcms_ICCProfile* profile) {
    // Here are 252 of a random shuffle of all possible bytes.
    // 252 is evenly divisible by 3 and 4.  Only 192, 10, 241, and 43 are missing.
    static const uint8_t k252_bytes[] = {
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

    static const skcms_ICCProfile XYZD50 = {
        .buffer           = NULL,
        .size             = 0,
        .data_color_space = 0x52474220, // 'RGB '
        .pcs              = 0x58595A20, // 'XYZ '
        .tag_count        = 0,
        .has_trc          = true,
        .has_toXYZD50     = true,
        .has_A2B          = false,

        .trc = {
            {{{0, {1,1,0,0,0,0,0}}}},
            {{{0, {1,1,0,0,0,0,0}}}},
            {{{0, {1,1,0,0,0,0,0}}}},
        },

        .toXYZD50 = {{
            {1,0,0},
            {0,1,0},
            {0,0,1},
        }},
    };

    // Interpret as RGB_888 if data color space is RGB or GRAY, RGBA_8888 if CMYK.
    skcms_PixelFormat fmt = skcms_PixelFormat_RGB_888;
    size_t npixels = 84;
    if (profile->data_color_space == 0x434D594B/*CMYK*/) {
        fmt = skcms_PixelFormat_RGBA_8888;
        npixels = 63;
    }

    uint8_t dst[252];

    if (!skcms_Transform(k252_bytes,                fmt, skcms_AlphaFormat_Unpremul, profile,
                         dst, skcms_PixelFormat_RGB_888, skcms_AlphaFormat_Unpremul, &XYZD50,
                         npixels)) {
        fprintf(fp, "We can parse this profile, but not transform it XYZD50!\n");
        return;
    }

    fprintf(fp, "252 random bytes transformed to linear XYZD50 bytes:\n");
    // 252 = 3 * 3 * 7 * 4, so we will print either 9 or 12 rows of 7 XYZ values here.
    for (size_t i = 0; i < npixels; i += 7) {
        fprintf(fp, "\t"
                    "%02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x "
                    "%02x%02x%02x %02x%02x%02x %02x%02x%02x\n",
                dst[3*i+ 0], dst[3*i+ 1], dst[3*i+ 2],
                dst[3*i+ 3], dst[3*i+ 4], dst[3*i+ 5],
                dst[3*i+ 6], dst[3*i+ 7], dst[3*i+ 8],
                dst[3*i+ 9], dst[3*i+10], dst[3*i+11],
                dst[3*i+12], dst[3*i+13], dst[3*i+14],
                dst[3*i+15], dst[3*i+16], dst[3*i+17],
                dst[3*i+18], dst[3*i+19], dst[3*i+20]);
    }
}


static void signature_to_string(uint32_t sig, char* str) {
    str[0] = (char)((sig >> 24) & 0xFF);
    str[1] = (char)((sig >> 16) & 0xFF);
    str[2] = (char)((sig >>  8) & 0xFF);
    str[3] = (char)((sig >>  0) & 0xFF);
    str[4] = 0;
}

static void dump_sig_field(FILE* fp, const char* name, uint32_t val) {
    char valStr[5];
    signature_to_string(val, valStr);
    fprintf(fp, "%20s : 0x%08X : '%s'\n", name, val, valStr);
}

static bool is_sRGB(const skcms_TransferFunction* tf) {
    return tf->g == 157286 / 65536.0f
        && tf->a ==  62119 / 65536.0f
        && tf->b ==   3417 / 65536.0f
        && tf->c ==   5072 / 65536.0f
        && tf->d ==   2651 / 65536.0f
        && tf->e ==      0 / 65536.0f
        && tf->f ==      0 / 65536.0f;
}

static bool is_linear(const skcms_TransferFunction* tf) {
    return tf->g == 1.0f
        && tf->a == 1.0f
        && tf->b == 0.0f
        && tf->c == 0.0f
        && tf->d == 0.0f
        && tf->e == 0.0f
        && tf->f == 0.0f;
}

static void dump_transfer_function(FILE* fp, const char* name, const skcms_TransferFunction* tf) {
    fprintf(fp, "%4s : %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g", name,
           tf->g, tf->a, tf->b, tf->c, tf->d, tf->e, tf->f);
    if (is_sRGB(tf)) {
        fprintf(fp, " (sRGB)");
    } else if (is_linear(tf)) {
        fprintf(fp, " (Linear)");
    }
    fprintf(fp, "\n");
}

static void dump_approx_transfer_function(FILE* fp, const skcms_TransferFunction* tf,
                                          float max_error, bool for_unit_test) {
    if (for_unit_test) {
        fprintf(fp, "  ~= : %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g  (Max error: %.2g)",
                tf->g, tf->a, tf->b, tf->c, tf->d, tf->e, tf->f, max_error);
    } else {
        fprintf(fp, "  ~= : %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g  (Max error: %.9g)",
                tf->g, tf->a, tf->b, tf->c, tf->d, tf->e, tf->f, max_error);
    }
    if (tf->d > 0) {
        // Has both linear and nonlinear sections, include the discontinuity at D
        float l_at_d = (tf->c * tf->d + tf->f);
        float n_at_d = powf_(tf->a * tf->d + tf->b, tf->g) + tf->e;
        fprintf(fp, " (D-gap: %.*g)", for_unit_test ? 2 : 9, (n_at_d - l_at_d));
    }
    if (is_linear(tf)) {
        fprintf(fp, " (Linear)");
    }
    fprintf(fp, "\n");
}

static void dump_approx_tf13(FILE* fp, const skcms_TF13* tf,
                             float max_error, bool for_unit_test) {
    (void)for_unit_test;
    fprintf(fp, "  ~= : %.4gx^3 + %.4gx^2 + %.4gx (Max error: %.4g)\n",
            tf->A, tf->B, (1 - tf->A - tf->B), max_error);
}

static void dump_curve(FILE* fp, const char* name, const skcms_Curve* curve, bool for_unit_test) {
    if (curve->table_entries) {
        fprintf(fp, "%4s : %d-bit table with %u entries\n", name,
                curve->table_8 ? 8 : 16, curve->table_entries);
        skcms_TransferFunction tf;
        float max_error;
        if (skcms_ApproximateCurve(curve, &tf, &max_error)) {
            dump_approx_transfer_function(fp, &tf, max_error, for_unit_test);
        }
        skcms_TF13 tf13;
        if (skcms_ApproximateCurve13(curve, &tf13, &max_error)) {
            dump_approx_tf13(fp, &tf13, max_error, for_unit_test);
        }
    } else {
        dump_transfer_function(fp, name, &curve->parametric);
    }
}

static bool has_single_transfer_function(const skcms_ICCProfile* profile,
                                         skcms_TransferFunction* tf) {
    const skcms_Curve* trc = profile->trc;
    if (profile->has_trc &&
            trc[0].table_entries == 0 &&
            trc[1].table_entries == 0 &&
            trc[2].table_entries == 0) {

        if (0 != memcmp(&trc[0].parametric, &trc[1].parametric, sizeof(skcms_TransferFunction)) ||
            0 != memcmp(&trc[0].parametric, &trc[2].parametric, sizeof(skcms_TransferFunction))) {
            return false;
        }

        memcpy(tf, &trc[0].parametric, sizeof(skcms_TransferFunction));
        return true;
    }
    return false;
}

void dump_profile(const skcms_ICCProfile* profile, FILE* fp, bool for_unit_test) {
    fprintf(fp, "%20s : 0x%08X : %u\n", "Size", profile->size, profile->size);
    dump_sig_field(fp, "Data color space", profile->data_color_space);
    dump_sig_field(fp, "PCS", profile->pcs);
    fprintf(fp, "%20s : 0x%08X : %u\n", "Tag count", profile->tag_count, profile->tag_count);

    fprintf(fp, "\n");

    fprintf(fp, " Tag    : Type   : Size   : Offset\n");
    fprintf(fp, " ------ : ------ : ------ : --------\n");
    for (uint32_t i = 0; i < profile->tag_count; ++i) {
        skcms_ICCTag tag;
        skcms_GetTagByIndex(profile, i, &tag);
        char tagSig[5];
        char typeSig[5];
        signature_to_string(tag.signature, tagSig);
        signature_to_string(tag.type, typeSig);
        fprintf(fp, " '%s' : '%s' : %6u : %u\n", tagSig, typeSig, tag.size,
                (uint32_t)(tag.buf - profile->buffer));
    }

    fprintf(fp, "\n");

    skcms_TransferFunction tf;
    if (has_single_transfer_function(profile, &tf)) {
        dump_transfer_function(fp, "TRC", &tf);
    } else if (profile->has_trc) {
        const char* trcNames[3] = { "rTRC", "gTRC", "bTRC" };
        for (int i = 0; i < 3; ++i) {
            dump_curve(fp, trcNames[i], &profile->trc[i], for_unit_test);
        }
    }

    if (profile->has_toXYZD50) {
        skcms_Matrix3x3 toXYZ = profile->toXYZD50;
        fprintf(fp, " XYZ : | %.9f %.9f %.9f |\n"
                    "       | %.9f %.9f %.9f |\n"
                    "       | %.9f %.9f %.9f |\n",
               toXYZ.vals[0][0], toXYZ.vals[0][1], toXYZ.vals[0][2],
               toXYZ.vals[1][0], toXYZ.vals[1][1], toXYZ.vals[1][2],
               toXYZ.vals[2][0], toXYZ.vals[2][1], toXYZ.vals[2][2]);
    }

    if (profile->has_A2B) {
        const skcms_A2B* a2b = &profile->A2B;
        fprintf(fp, " A2B : %s%s\"B\"\n", a2b->input_channels ? "\"A\", CLUT, " : "",
                                          a2b->matrix_channels ? "\"M\", Matrix, " : "");
        if (a2b->input_channels) {
            fprintf(fp, "%4s : %u inputs\n", "\"A\"", a2b->input_channels);
            const char* curveNames[4] = { "A0", "A1", "A2", "A3" };
            for (uint32_t i = 0; i < a2b->input_channels; ++i) {
                dump_curve(fp, curveNames[i], &a2b->input_curves[i], for_unit_test);
            }
            fprintf(fp, "%4s : ", "CLUT");
            const char* sep = "";
            for (uint32_t i = 0; i < a2b->input_channels; ++i) {
                fprintf(fp, "%s%u", sep, a2b->grid_points[i]);
                sep = " x ";
            }
            fprintf(fp, " (%d bpp)\n", a2b->grid_8 ? 8 : 16);
        }

        if (a2b->matrix_channels) {
            fprintf(fp, "%4s : %u inputs\n", "\"M\"", a2b->matrix_channels);
            const char* curveNames[4] = { "M0", "M1", "M2" };
            for (uint32_t i = 0; i < a2b->matrix_channels; ++i) {
                dump_curve(fp, curveNames[i], &a2b->matrix_curves[i], for_unit_test);
            }
            const skcms_Matrix3x4* m = &a2b->matrix;
            fprintf(fp, "Mtrx : | %.9f %.9f %.9f %.9f |\n"
                        "       | %.9f %.9f %.9f %.9f |\n"
                        "       | %.9f %.9f %.9f %.9f |\n",
                   m->vals[0][0], m->vals[0][1], m->vals[0][2], m->vals[0][3],
                   m->vals[1][0], m->vals[1][1], m->vals[1][2], m->vals[1][3],
                   m->vals[2][0], m->vals[2][1], m->vals[2][2], m->vals[2][3]);
        }

        {
            fprintf(fp, "%4s : %u outputs\n", "\"B\"", a2b->output_channels);
            const char* curveNames[3] = { "B0", "B1", "B2" };
            for (uint32_t i = 0; i < a2b->output_channels; ++i) {
                dump_curve(fp, curveNames[i], &a2b->output_curves[i], for_unit_test);
            }
        }
    }

    dump_transform_to_XYZD50(fp, profile);
}

bool load_file_fp(FILE* fp, void** buf, size_t* len) {
    if (fseek(fp, 0L, SEEK_END) != 0) {
        return false;
    }
    long size = ftell(fp);
    if (size <= 0) {
        return false;
    }
    *len = (size_t)size;
    rewind(fp);

    *buf = malloc(*len);
    if (!*buf) {
        return false;
    }

    if (fread(*buf, 1, *len, fp) != *len) {
        free(*buf);
        return false;
    }
    return true;
}

bool load_file(const char* filename, void** buf, size_t* len) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }
    bool result = load_file_fp(fp, buf, len);
    fclose(fp);
    return result;
}

bool write_file(const char* filename, void* buf, size_t len) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        return false;
    }
    bool result = (fwrite(buf, 1, len, fp) == len);
    fclose(fp);
    return result;
}
