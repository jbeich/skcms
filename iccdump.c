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

static void dump_curves_svg(const char* name, uint32_t num_curves, const skcms_Curve* curves) {
    char filename[256];
    if (snprintf(filename, sizeof(filename), "%s.svg", name) < 0) {
        return;
    }
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
    const char* rgb_colors[3] = { "red", "green", "blue" };
    const char* cmyk_colors[4] = { "cyan", "magenta", "yellow", "black" };
    const char** colors = (num_curves == 3) ? rgb_colors : cmyk_colors;
    for (uint32_t c = 0; c < num_curves; ++c) {
        uint32_t num_entries = curves[c].table_entries ? curves[c].table_entries : 256;
        double yScale = curves[c].table_8 ? (1.0 / 255) : curves[c].table_16 ? (1.0 / 65535) : 1.0;

        fprintf(fp, "<polyline fill=\"none\" stroke=\"%s\" vector-effect=\"non-scaling-stroke\" "
                    "transform=\"matrix(%f 0 0 %f %f %f)\" points=\"\n",
                colors[c],
                kSVGScaleX / (num_entries - 1.0), -kSVGScaleY * yScale,
                kSVGMarginLeft, kSVGScaleY + kSVGMarginTop);

        for (uint32_t i = 0; i < num_entries; ++i) {
            if (curves[c].table_8) {
                fprintf(fp, "%3u, %3u\n", i, curves[c].table_8[i]);
            } else if (curves[c].table_16) {
                fprintf(fp, "%4u, %5u\n", i, read_big_u16(curves[c].table_16 + 2 * i));
            } else {
                double x = i / (num_entries - 1.0);
                double t = (double)skcms_TransferFunction_eval(&curves[c].parametric, (float)x);
                fprintf(fp, "%f, %f\n", x, t);
            }
        }
        fprintf(fp, "\"/>\n");
    }

    fprintf(fp, "</svg>\n");
    fclose(fp);
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
    if (!buf) {
        fatal("malloc failed");
    }
    size_t bytesRead = fread(buf, 1, len, fp);
    fclose(fp);
    if (bytesRead != len) {
        fatal("Unable to read file");
    }

    skcms_ICCProfile profile;
    if (!skcms_Parse(buf, len, &profile)) {
        fatal("Unable to parse ICC profile");
    }

    dump_profile(&profile, stdout, false);

    if (svg) {
        if (profile.has_trc) {
            dump_curves_svg("TRC_curves", 3, profile.trc);
        }

        if (profile.has_A2B) {
            const skcms_A2B* a2b = &profile.A2B;
            if (a2b->input_channels) {
                dump_curves_svg("A_curves", a2b->input_channels, a2b->input_curves);
            }

            if (a2b->matrix_channels) {
                dump_curves_svg("M_curves", a2b->matrix_channels, a2b->matrix_curves);
            }

            dump_curves_svg("B_curves", a2b->output_channels, a2b->output_curves);
        }
    }

    return 0;
}
