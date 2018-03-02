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
#include "test_only.h"
#include "src/Macros.h"
#include "src/TransferFunction.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SKCMS_NORETURN
static void fatal(const char* msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
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

float xFit_1931(float wave) {
    float t1 = (wave - 442.0f)*((wave < 442.0f) ? 0.0624f : 0.0374f);
    float t2 = (wave - 599.8f)*((wave < 599.8f) ? 0.0264f : 0.0323f);
    float t3 = (wave - 501.1f)*((wave < 501.1f) ? 0.0490f : 0.0382f);
    return 0.362f*expf(-0.5f*t1*t1) + 1.056f*expf(-0.5f*t2*t2) - 0.065f*expf(-0.5f*t3*t3);
}

float yFit_1931(float wave) {
    float t1 = (wave - 568.8f)*((wave < 568.8f) ? 0.0213f : 0.0247f);
    float t2 = (wave - 530.9f)*((wave < 530.9f) ? 0.0613f : 0.0322f);
    return 0.821f*expf(-0.5f*t1*t1) + 0.286f*expf(-0.5f*t2*t2);
}

float zFit_1931(float wave) {
    float t1 = (wave - 437.0f)*((wave < 437.0f) ? 0.0845f : 0.0278f);
    float t2 = (wave - 459.0f)*((wave < 459.0f) ? 0.0385f : 0.0725f);
    return 1.217f*expf(-0.5f*t1*t1) + 0.681f*expf(-0.5f*t2*t2);
}

static const double kSVGMarginLeft   = 100.0;
static const double kSVGMarginRight  = 10.0;
static const double kSVGMarginTop    = 10.0;
static const double kSVGMarginBottom = 50.0;

static const double kSVGScaleX = 800.0;
static const double kSVGScaleY = 800.0;

static const char* kSVG_RGB_Colors[3] = { "red", "green", "blue" };
static const char* kSVG_CMYK_Colors[4] = { "cyan", "magenta", "yellow", "black" };

static double svg_map_x(double x) {
    return x * kSVGScaleX + kSVGMarginLeft;
}

static double svg_map_y(double y) {
    return (1.0 - y) * kSVGScaleY + kSVGMarginTop;
}

static FILE* svg_open(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        fatal("Unable to open output file");
    }

    fprintf(fp, "<svg width=\"%f\" height=\"%f\" xmlns=\"http://www.w3.org/2000/svg\">\n",
            kSVGMarginLeft + kSVGScaleX + kSVGMarginRight,
            kSVGMarginTop + kSVGScaleY + kSVGMarginBottom);
    return fp;
}

static void svg_close(FILE* fp) {
    fprintf(fp, "</svg>\n");
    fclose(fp);
}

static void svg_axes(FILE* fp) {
    fprintf(fp, "<polyline fill=\"none\" stroke=\"black\" points=\"%f,%f %f,%f %f,%f\"/>\n",
            svg_map_x(0), svg_map_y(1), svg_map_x(0), svg_map_y(0), svg_map_x(1), svg_map_y(0));
}

static void svg_curve(FILE* fp, const skcms_Curve* curve, const char* color) {
    uint32_t num_entries = curve->table_entries ? curve->table_entries : 256;
    double yScale = curve->table_8 ? (1.0 / 255) : curve->table_16 ? (1.0 / 65535) : 1.0;

    fprintf(fp, "<polyline fill=\"none\" stroke=\"%s\" vector-effect=\"non-scaling-stroke\" "
            "transform=\"matrix(%f 0 0 %f %f %f)\" points=\"\n",
            color,
            kSVGScaleX / (num_entries - 1.0), -kSVGScaleY * yScale,
            kSVGMarginLeft, kSVGScaleY + kSVGMarginTop);

    for (uint32_t i = 0; i < num_entries; ++i) {
        if (curve->table_8) {
            fprintf(fp, "%3u, %3u\n", i, curve->table_8[i]);
        } else if (curve->table_16) {
            fprintf(fp, "%4u, %5u\n", i, read_big_u16(curve->table_16 + 2 * i));
        } else {
            double x = i / (num_entries - 1.0);
            double t = (double)skcms_TransferFunction_eval(&curve->parametric, (float)x);
            fprintf(fp, "%3u, %f\n", i, t);
        }
    }
    fprintf(fp, "\"/>\n");
}

static void svg_curves(FILE* fp, uint32_t num_curves, const skcms_Curve* curves,
                       const char** colors) {
    for (uint32_t c = 0; c < num_curves; ++c) {
        svg_curve(fp, curves + c, colors[c]);
    }
}

static void dump_curves_svg(const char* filename, uint32_t num_curves, const skcms_Curve* curves) {
    FILE* fp = svg_open(filename);
    svg_axes(fp);
    fprintf(fp, "<text x=\"20\" y=\"20\" font-size=\"18\">%s</text>\n", filename);
    svg_curves(fp, num_curves, curves, (num_curves == 3) ? kSVG_RGB_Colors : kSVG_CMYK_Colors);
    svg_close(fp);
}

int main(int argc, char** argv) {
    const char* filename = NULL;
    bool svg = false;

    for (int i = 1; i < argc; ++i) {
        if (0 == strcmp(argv[i], "-s")) {
            svg = true;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        printf("usage: %s [-s] <ICC filename>\n", argv[0]);
        return 1;
    }

    void* buf = NULL;
    size_t len = 0;
    if (!load_file(filename, &buf, &len)) {
        fatal("Unable to load input file");
    }

    skcms_ICCProfile profile;
    if (!skcms_Parse(buf, len, &profile)) {
        fatal("Unable to parse ICC profile");
    }

    dump_profile(&profile, stdout, false);

    if (svg) {
        if (profile.has_trc) {
            FILE* fp = svg_open("TRC_curves.svg");
            svg_axes(fp);
            svg_curves(fp, 3, profile.trc, kSVG_RGB_Colors);
            skcms_Curve approx;
            float max_error;
            if (skcms_ApproximateTransferFunction(&profile, &approx.parametric, &max_error)) {
                approx.table_8 = approx.table_16 = NULL;
                approx.table_entries = 0;
                svg_curve(fp, &approx, "magenta");
            }
            svg_close(fp);
        }

        if (profile.has_A2B) {
            const skcms_A2B* a2b = &profile.A2B;
            if (a2b->input_channels) {
                dump_curves_svg("A_curves.svg", a2b->input_channels, a2b->input_curves);
            }

            if (a2b->matrix_channels) {
                dump_curves_svg("M_curves.svg", a2b->matrix_channels, a2b->matrix_curves);
            }

            dump_curves_svg("B_curves.svg", a2b->output_channels, a2b->output_curves);
        }
    }

    return 0;
}
