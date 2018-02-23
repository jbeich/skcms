/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
    #define SKCMS_NORETURN __declspec(noreturn)
#else
    #include <stdnoreturn.h>
    #define SKCMS_NORETURN noreturn
#endif

#include "skcms.h"
#include "src/Macros.h"
#include "src/TransferFunction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SKCMS_NORETURN
static void fatal(const char* msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}

static void signature_to_string(uint32_t sig, char* str) {
    str[0] = (char)(sig >> 24);
    str[1] = (char)(sig >> 16);
    str[2] = (char)(sig >>  8);
    str[3] = (char)(sig >>  0);
    str[4] = 0;
}

static void dump_sig_field(const char* name, uint32_t val) {
    char valStr[5];
    signature_to_string(val, valStr);
    printf("%20s : 0x%08X : '%s'\n", name, val, valStr);
}

static void dump_transfer_function(const char* name, const skcms_TransferFunction* tf) {
    printf("%4s : %f, %f, %f, %f, %f, %f, %f", name,
           (double)tf->g, (double)tf->a, (double)tf->b, (double)tf->c,
           (double)tf->d, (double)tf->e, (double)tf->f);
    if (skcms_IsSRGB(tf)) {
        printf(" (sRGB)");
    }
    printf("\n");
}

static uint16_t read_big_u16(const uint8_t* ptr) {
    uint16_t be;
    memcpy(&be, ptr, sizeof(be));
#if defined(_MSC_VER)
    return _byteswap_ushort(be);
#else
    return __builtin_bswap16(be);
#endif
}

static void dump_curve(const char* name, const skcms_Curve* curve, bool verbose) {
    if (curve->table_entries) {
        printf("%4s : %d-bit table with %u entries", name,
               curve->table_8 ? 8 : 16, curve->table_entries);
        if (verbose) {
            char filename[32];
            snprintf(filename, sizeof(filename), "%s.csv", name);
            FILE* fp = fopen(filename, "wb");
            if (fp) {
                for (uint32_t i = 0; i < curve->table_entries; ++i) {
                    double x = i / (curve->table_entries - 1.0);
                    double t = curve->table_8
                        ? curve->table_8[i] * (1.0 / 255)
                        : read_big_u16(curve->table_16 + 2 * i) * (1.0 / 65535);
                    fprintf(fp, "%f,%f\n", x, t);
                }
                fclose(fp);
                printf(" (wrote to %s)", filename);
            }
        }
        printf("\n");
    } else {
        dump_transfer_function(name, &curve->parametric);
    }
}

static const double kSVGMarginLeft   = 100.0;
static const double kSVGMarginRight  = 10.0;
static const double kSVGMarginTop    = 10.0;
static const double kSVGMarginBottom = 50.0;

static const double kSVGScaleX = 800.0;
static const double kSVGScaleY = 800.0;

static double svg_map_x(double x) {
    return x * kSVGScaleX + kSVGMarginLeft;
}

static double svg_map_y(double y) {
    return (1.0 - y) * kSVGScaleY + kSVGMarginTop;
}

static void dump_curves_svg(const char* name, const skcms_Curve* curve) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s.svg", name);
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        return;
    }

    fprintf(fp, "<svg width=\"%f\" height=\"%f\" xmlns=\"http://www.w3.org/2000/svg\">\n",
            kSVGMarginLeft + kSVGScaleX + kSVGMarginRight,
            kSVGMarginTop + kSVGScaleY + kSVGMarginBottom);
    // Axes
    fprintf(fp, "<polyline fill=\"none\" stroke=\"black\" points=\"%f,%f %f,%f %f,%f\"/>\n",
            svg_map_x(0), svg_map_y(1), svg_map_x(0), svg_map_y(0), svg_map_x(1), svg_map_y(0));

    // Curves
    static const char* colors[3] = { "red", "green", "blue" };
    for (int c = 0; c < 3; ++c) {
        uint32_t num_entries = curve[c].table_entries ? curve[c].table_entries : 256;
        double yScale = curve[c].table_8 ? (1.0 / 255) : curve[c].table_16 ? (1.0 / 65535) : 1.0;

        fprintf(fp, "<polyline fill=\"none\" stroke=\"%s\" vector-effect=\"non-scaling-stroke\" "
                    "transform=\"matrix(%f 0 0 %f %f %f)\" points=\"\n",
                colors[c],
                kSVGScaleX / (num_entries - 1.0), -kSVGScaleY * yScale,
                kSVGMarginLeft, kSVGScaleY + kSVGMarginTop);

        for (uint32_t i = 0; i < num_entries; ++i) {
            if (curve[c].table_8) {
                fprintf(fp, "%3u, %3u\n", i, curve[c].table_8[i]);
            } else if (curve[c].table_16) {
                fprintf(fp, "%4u, %5u\n", i, read_big_u16(curve[c].table_16 + 2 * i));
            } else {
                double x = i / (num_entries - 1.0);
                double t = (double)skcms_TransferFunction_eval(&curve[c].parametric, (float)x);
                fprintf(fp, "%f, %f\n", x, t);
            }
        }
        fprintf(fp, "\"/>\n");
    }

    fprintf(fp, "</svg>\n");
    fclose(fp);
}

static void dump_curve_test_data(const skcms_Curve* c) {
    const skcms_TransferFunction* p = &c->parametric;
    printf("        { { %ff, %ff, %ff, %ff, %ff, %ff, %ff }, %s, %s, %u },\n",
           p->g, p->a, p->b, p->c, p->d, p->e, p->f,
           c->table_8 ? "NOT_NULL" : "NULL",
           c->table_16 ? "NOT_NULL" : "NULL",
           c->table_entries);
}

static void dump_curves_test_data(uint32_t num_channels, uint32_t num_curves,
                                  const skcms_Curve* curves) {
    printf("    %u, {\n", num_channels);
    for (uint32_t i = 0; i < num_curves; ++i) {
        dump_curve_test_data(curves + i);
    }
    printf("    },\n");
}

static void dump_a2b_test_data(const char* filename, const skcms_A2B* a2b) {
    char* namecopy = malloc(strlen(filename) + 1);
    if (!namecopy) {
        fatal("malloc failed");
    }
    strcpy(namecopy, filename);
    char* ext = strrchr(namecopy, '.');
    if (ext) {
        *ext = '\0';
    }
    char* basename = namecopy;
    char* sep1 = strrchr(namecopy, '/');
    if (sep1) {
        basename = sep1 + 1;
    }
    char* sep2 = strrchr(namecopy, '\\');
    if (sep2 && sep2 > basename) {
        basename = sep2 + 1;
    }

    printf("static const skcms_A2B %s_A2B = {\n", basename);
    dump_curves_test_data(a2b->input_channels, ARRAY_COUNT(a2b->input_curves), a2b->input_curves);
    printf("    { %u, %u, %u, %u },\n",
           a2b->grid_points[0], a2b->grid_points[1], a2b->grid_points[2], a2b->grid_points[3]);
    printf("    %s, %s,\n", a2b->grid_8 ? "NOT_NULL" : "NULL", a2b->grid_16 ? "NOT_NULL" : "NULL");

    dump_curves_test_data(a2b->matrix_channels, ARRAY_COUNT(a2b->matrix_curves),
                          a2b->matrix_curves);
    const skcms_Matrix3x4* m = &a2b->matrix;
    printf("    { { { %ff, %ff, %ff, %ff },\n",
           m->vals[0][0], m->vals[0][1], m->vals[0][2], m->vals[0][3]);
    printf("        { %ff, %ff, %ff, %ff },\n",
           m->vals[1][0], m->vals[1][1], m->vals[1][2], m->vals[1][3]);
    printf("        { %ff, %ff, %ff, %ff } } },\n",
           m->vals[2][0], m->vals[2][1], m->vals[2][2], m->vals[2][3]);

    dump_curves_test_data(a2b->output_channels, ARRAY_COUNT(a2b->output_curves),
                          a2b->output_curves);

    printf("};\n");

    free(namecopy);
}

int main(int argc, char** argv) {
    const char* filename = NULL;
    bool verbose = false;
    bool svg = false;
    bool test = false;

    for (int i = 1; i < argc; ++i) {
        if (0 == strcmp(argv[i], "-v")) {
            verbose = true;
        } else if (0 == strcmp(argv[i], "-s")) {
            svg = true;
        } else if (0 == strcmp(argv[i], "-t")) {
            test = true;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        printf("usage: %s [-t] [-v] [-s] <ICC filename>\n", argv[0]);
        return 1;
    }

    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fatal("Unable to open input file");
    }

    fseek(fp, 0L, SEEK_END);
    long slen = ftell(fp);
    if (slen <= 0) {
        fatal("ftell failed");
    }
    size_t len = (size_t)slen;
    rewind(fp);

    void* buf = malloc(len);
    size_t bytesRead = fread(buf, 1, len, fp);
    fclose(fp);
    if (bytesRead != len) {
        fatal("Unable to read file");
    }

    skcms_ICCProfile profile;
    if (!skcms_Parse(buf, bytesRead, &profile)) {
        fatal("Unable to parse ICC profile");
    }

    if (test) {
        if (profile.has_A2B) {
            dump_a2b_test_data(filename, &profile.A2B);
        }
        return 0;
    }

    printf("%20s : 0x%08X : %u\n", "Size", profile.size, profile.size);
    dump_sig_field("CMM type", profile.cmm_type);
    printf("%20s : 0x%08X : %u.%u.%u\n", "Version", profile.version,
           profile.version >> 24, (profile.version >> 20) & 0xF, (profile.version >> 16) & 0xF);
    dump_sig_field("Profile class", profile.profile_class);
    dump_sig_field("Data color space", profile.data_color_space);
    dump_sig_field("PCS", profile.pcs);
    printf("%20s :            : %u-%02u-%02u %02u:%02u:%02u\n", "Creation date/time",
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
    printf("%20s : 0x%08X : %u\n", "Rendering intent", profile.rendering_intent,
           profile.rendering_intent);
    printf("%20s :            : %f\n", "Illuminant X", (double)profile.illuminant_X);
    printf("%20s :            : %f\n", "Illuminant Y", (double)profile.illuminant_Y);
    printf("%20s :            : %f\n", "Illuminant Z", (double)profile.illuminant_Z);
    dump_sig_field("Creator", profile.creator);
    printf("%20s : 0x%08X : %u\n", "Tag count", profile.tag_count, profile.tag_count);

    printf("\n");

    printf(" Tag    : Type   : Size   : Data\n");
    printf(" ------ : ------ : ------ : --------\n");
    for (uint32_t i = 0; i < profile.tag_count; ++i) {
        skcms_ICCTag tag;
        skcms_GetTagByIndex(&profile, i, &tag);
        char tagSig[5];
        char typeSig[5];
        signature_to_string(tag.signature, tagSig);
        signature_to_string(tag.type, typeSig);
        printf(" '%s' : '%s' : %6u : %p\n", tagSig, typeSig, tag.size, (const void*)tag.buf);
    }

    printf("\n");

    skcms_TransferFunction tf;
    float max_error;
    if (profile.has_tf) {
        dump_transfer_function("TRC", &profile.tf);
    } else if (skcms_ApproximateTransferFunction(&profile, &tf, &max_error)) {
        printf("%4s : %f, %f, %f, %f, %f, %f, %f  (Max error: %f)\n", "~TRC",
               (double)tf.g, (double)tf.a, (double)tf.b, (double)tf.c,
               (double)tf.d, (double)tf.e, (double)tf.f, (double)max_error);
    }

    if (!profile.has_tf && profile.has_trc) {
        const char* trcNames[3] = { "rTRC", "gTRC", "bTRC" };
        for (int i = 0; i < 3; ++i) {
            dump_curve(trcNames[i], &profile.trc[i], verbose);
        }
    }

    if (svg && profile.has_trc) {
        dump_curves_svg(filename, profile.trc);
    }

    if (profile.has_toXYZD50) {
        skcms_Matrix3x3 toXYZ = profile.toXYZD50;
        printf(" XYZ : | %.7f %.7f %.7f |\n"
               "       | %.7f %.7f %.7f |\n"
               "       | %.7f %.7f %.7f |\n",
               (double)toXYZ.vals[0][0], (double)toXYZ.vals[0][1], (double)toXYZ.vals[0][2],
               (double)toXYZ.vals[1][0], (double)toXYZ.vals[1][1], (double)toXYZ.vals[1][2],
               (double)toXYZ.vals[2][0], (double)toXYZ.vals[2][1], (double)toXYZ.vals[2][2]);
    }
    return 0;
}
