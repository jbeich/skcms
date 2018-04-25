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
#include "src/RandomBytes.h"
#include "src/TransferFunction.h"
#include "test_only.h"
#include <stdlib.h>
#include <string.h>

static void dump_transform_to_XYZD50(FILE* fp,
                                     const skcms_ICCProfile* profile,
                                     const skcms_ICCProfile* optimized) {
    // Interpret as RGB_888 if data color space is RGB or GRAY, RGBA_8888 if CMYK.
    skcms_PixelFormat fmt = skcms_PixelFormat_RGB_888;
    size_t npixels = 84;
    if (profile->data_color_space == 0x434D594B/*CMYK*/) {
        fmt = skcms_PixelFormat_RGBA_8888;
        npixels = 63;
    }

    uint8_t dst[252];

    if (!skcms_Transform(
                skcms_252_random_bytes,    fmt, skcms_AlphaFormat_Unpremul, profile,
                dst, skcms_PixelFormat_RGB_888, skcms_AlphaFormat_Unpremul, &skcms_XYZD50_profile,
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

    // Print out optimize profile diff if relevant.
    if (0 == memcmp(profile, optimized, sizeof(*profile))) {
        return;
    }

    uint8_t opt[252];
    if (!skcms_Transform(
                skcms_252_random_bytes,    fmt, skcms_AlphaFormat_Unpremul, optimized,
                opt, skcms_PixelFormat_RGB_888, skcms_AlphaFormat_Unpremul, &skcms_XYZD50_profile,
                npixels)) {
        fprintf(fp, "We cannot transform the optimized profile!  THIS IS REALLY BAD.\n");
        return;
    }

    bool print = true;
    for (int i = 0; i < 252; i++) {
        if (opt[i] != dst[i]) {
            if (print) {
                fprintf(fp, "summary of diffs at each byte when profile is optimized:\n");
                print = false;
            }
            fprintf(fp, "  %3d: %02x -> %02x, %+d\n",
                    i, dst[i], opt[i], opt[i] - dst[i]);
        }
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

static bool is_identity(const skcms_TransferFunction* tf) {
    return tf->g == 1.0f
        && tf->a == 1.0f
        && tf->b == 0.0f
        && tf->c == 0.0f
        && tf->d == 0.0f
        && tf->e == 0.0f
        && tf->f == 0.0f;
}

static void dump_transfer_function(FILE* fp, const char* name,
                                   const skcms_TransferFunction* tf, float max_error) {
    fprintf(fp, "%4s : %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g", name,
            tf->g, tf->a, tf->b, tf->c, tf->d, tf->e, tf->f);

    if (max_error > 0) {
        fprintf(fp, " (Max error: %.6g)", max_error);
    }

    if (tf->d > 0) {
        // Has both linear and nonlinear sections, include the discontinuity at D
        float l_at_d = (tf->c * tf->d + tf->f);
        float n_at_d = powf_(tf->a * tf->d + tf->b, tf->g) + tf->e;
        fprintf(fp, " (D-gap: %.6g)", (n_at_d - l_at_d));
    }

    if (is_sRGB(tf)) {
        fprintf(fp, " (sRGB)");
    } else if (is_identity(tf)) {
        fprintf(fp, " (Identity)");
    }
    fprintf(fp, "\n");
}

static void dump_curve(FILE* fp, const char* name, const skcms_Curve* curve) {
    if (curve->table_entries == 0) {
        dump_transfer_function(fp, name, &curve->parametric, 0);
    } else {
        fprintf(fp, "%4s : %d-bit table with %u entries\n", name,
                curve->table_8 ? 8 : 16, curve->table_entries);
        float max_error;
        skcms_TransferFunction tf;
        if (skcms_ApproximateCurve(curve, &tf, &max_error)) {
            dump_transfer_function(fp, "~=", &tf, max_error);
        }
    }
}

void dump_profile(const skcms_ICCProfile* profile, FILE* fp) {
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

    if (profile->has_trc) {
        const char* trcNames[3] = { "rTRC", "gTRC", "bTRC" };
        for (int i = 0; i < 3; ++i) {
            dump_curve(fp, trcNames[i], &profile->trc[i]);
        }
    }

    skcms_ICCProfile best_single_curve = *profile;
    skcms_EnsureUsableAsDestinationWithSingleCurve(&best_single_curve, &skcms_sRGB_profile);
    dump_transfer_function(fp, "Best", &best_single_curve.trc[0].parametric, 0.0f);

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
                dump_curve(fp, curveNames[i], &a2b->input_curves[i]);
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
                dump_curve(fp, curveNames[i], &a2b->matrix_curves[i]);
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
                dump_curve(fp, curveNames[i], &a2b->output_curves[i]);
            }
        }
    }

    skcms_ICCProfile opt = *profile;
    skcms_OptimizeForSpeed(&opt);

    dump_transform_to_XYZD50(fp, profile, &opt);
    for (int i = 0; i < 3; i++) {
        if (opt.has_poly_tf[i]) {
            fprintf(fp, "polyTF[%d] = %g %g %g %g\n",
                    i, opt.poly_tf[i].A, opt.poly_tf[i].B, opt.poly_tf[i].C, opt.poly_tf[i].D);
        }
    }

    if (skcms_ApproximatelyEqualProfiles(profile, &skcms_sRGB_profile)) {
        fprintf(fp, "This profile ≈ sRGB.\n");
    }
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
