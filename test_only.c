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
#include "src/TransferFunction.h"
#include "test_only.h"
#include <stdlib.h>
#include <string.h>

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
           (double)tf->g, (double)tf->a, (double)tf->b, (double)tf->c,
           (double)tf->d, (double)tf->e, (double)tf->f);
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
                (double)tf->g, (double)tf->a, (double)tf->b, (double)tf->c,
                (double)tf->d, (double)tf->e, (double)tf->f, (double)max_error);
    } else {
        fprintf(fp, "  ~= : %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g  (Max error: %.9g)",
                (double)tf->g, (double)tf->a, (double)tf->b, (double)tf->c,
                (double)tf->d, (double)tf->e, (double)tf->f, (double)max_error);
    }
    if (is_linear(tf)) {
        fprintf(fp, " (Linear)");
    }
    fprintf(fp, "\n");
}

static void dump_curve(FILE* fp, const char* name, const skcms_Curve* curve, bool show_approx,
                       bool for_unit_test) {
    if (curve->table_entries) {
        fprintf(fp, "%4s : %d-bit table with %u entries\n", name,
                curve->table_8 ? 8 : 16, curve->table_entries);
        skcms_TransferFunction tf;
        float max_error;
        if ((show_approx || true) && skcms_ApproximateCurve(curve, &tf, &max_error)) {
            dump_approx_transfer_function(fp, &tf, max_error, for_unit_test);
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
    dump_sig_field(fp, "CMM type", profile->cmm_type);
    fprintf(fp, "%20s : 0x%08X : %u.%u.%u\n", "Version", profile->version,
            profile->version >> 24, (profile->version >> 20) & 0xF,
            (profile->version >> 16) & 0xF);
    dump_sig_field(fp, "Profile class", profile->profile_class);
    dump_sig_field(fp, "Data color space", profile->data_color_space);
    dump_sig_field(fp, "PCS", profile->pcs);
    fprintf(fp, "%20s :            : %u-%02u-%02u %02u:%02u:%02u\n", "Creation date/time",
            profile->creation_date_time.year, profile->creation_date_time.month,
            profile->creation_date_time.day, profile->creation_date_time.hour,
            profile->creation_date_time.minute, profile->creation_date_time.second);
    dump_sig_field(fp, "Signature", profile->signature);
    dump_sig_field(fp, "Platform", profile->platform);
    fprintf(fp, "%20s : 0x%08X\n", "Flags", profile->flags);
    dump_sig_field(fp, "Device manufacturer", profile->device_manufacturer);
    dump_sig_field(fp, "Device model", profile->device_model);
    fprintf(fp, "%20s : 0x%08X\n", "Device attributes",
            (uint32_t)(profile->device_attributes & 0xFFFFFFFF));
    fprintf(fp, "%20s : 0x%08X\n", "", (uint32_t)(profile->device_attributes >> 32));
    fprintf(fp, "%20s : 0x%08X : %u\n", "Rendering intent", profile->rendering_intent,
            profile->rendering_intent);
    fprintf(fp, "%20s :            : %.9g\n", "Illuminant X", (double)profile->illuminant_X);
    fprintf(fp, "%20s :            : %.9g\n", "Illuminant Y", (double)profile->illuminant_Y);
    fprintf(fp, "%20s :            : %.9g\n", "Illuminant Z", (double)profile->illuminant_Z);
    dump_sig_field(fp, "Creator", profile->creator);
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
            dump_curve(fp, trcNames[i], &profile->trc[i], true, for_unit_test);
        }
    }

    if (profile->has_toXYZD50) {
        skcms_Matrix3x3 toXYZ = profile->toXYZD50;
        fprintf(fp, " XYZ : | %.9f %.9f %.9f |\n"
                    "       | %.9f %.9f %.9f |\n"
                    "       | %.9f %.9f %.9f |\n",
               (double)toXYZ.vals[0][0], (double)toXYZ.vals[0][1], (double)toXYZ.vals[0][2],
               (double)toXYZ.vals[1][0], (double)toXYZ.vals[1][1], (double)toXYZ.vals[1][2],
               (double)toXYZ.vals[2][0], (double)toXYZ.vals[2][1], (double)toXYZ.vals[2][2]);
    }

    if (profile->has_A2B) {
        const skcms_A2B* a2b = &profile->A2B;
        fprintf(fp, " A2B : %s%s\"B\"\n", a2b->input_channels ? "\"A\", CLUT, " : "",
                                          a2b->matrix_channels ? "\"M\", Matrix, " : "");
        if (a2b->input_channels) {
            fprintf(fp, "%4s : %u inputs\n", "\"A\"", a2b->input_channels);
            const char* curveNames[4] = { "A0", "A1", "A2", "A3" };
            for (uint32_t i = 0; i < a2b->input_channels; ++i) {
                dump_curve(fp, curveNames[i], &a2b->input_curves[i], !for_unit_test,
                           for_unit_test);
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
                dump_curve(fp, curveNames[i], &a2b->matrix_curves[i], !for_unit_test,
                           for_unit_test);
            }
            const skcms_Matrix3x4* m = &a2b->matrix;
            fprintf(fp, "Mtrx : | %.9f %.9f %.9f %.9f |\n"
                        "       | %.9f %.9f %.9f %.9f |\n"
                        "       | %.9f %.9f %.9f %.9f |\n",
                   (double)m->vals[0][0], (double)m->vals[0][1], (double)m->vals[0][2], (double)m->vals[0][3],
                   (double)m->vals[1][0], (double)m->vals[1][1], (double)m->vals[1][2], (double)m->vals[1][3],
                   (double)m->vals[2][0], (double)m->vals[2][1], (double)m->vals[2][2], (double)m->vals[2][3]);
        }

        {
            fprintf(fp, "%4s : %u outputs\n", "\"B\"", a2b->output_channels);
            const char* curveNames[3] = { "B0", "B1", "B2" };
            for (uint32_t i = 0; i < a2b->output_channels; ++i) {
                dump_curve(fp, curveNames[i], &a2b->output_curves[i], !for_unit_test,
                           for_unit_test);
            }
        }
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
