cc_library(
    name = "skcms",
    srcs = [
        "skcms.cc",
        "skcms_internal.h",
        "src/Transform_inl.h",
    ],
    hdrs = ["skcms.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "test_only",
    testonly = True,
    srcs = ["test_only.c"],
    hdrs = ["test_only.h"],
    deps = [":skcms"],
)

cc_test(
    name = "tests",
    srcs = ["tests.c"],
    data = glob(["profiles/**"]),
    deps = [
        ":skcms",
        ":test_only",
    ],
)

cc_binary(
    name = "iccdump",
    testonly = True,
    srcs = ["iccdump.c"],
    linkopts = ["-ldl"],
    deps = [
        ":skcms",
        ":test_only",
    ],
)

cc_binary(
    name = "bench",
    srcs = ["bench.c"],
    deps = [":skcms"],
)
