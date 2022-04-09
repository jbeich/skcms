"""
This module defines the download_android_ndk repository rule.
"""

# Taken from https://github.com/android/ndk/wiki/Unsupported-Downloads#r21e.
_android_ndk_url = "https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip"
_android_ndk_sha256 = "ad7ce5467e18d40050dc51b8e7affc3e635c85bd8c59be62de32352328ed467e"
_android_ndk_prefix = "android-ndk-r21e"

def _download_android_ndk_impl(ctx):
    # Download the NDK (~1 GB).
    # https://bazel.build/rules/lib/repository_ctx#download_and_extract
    ctx.download_and_extract(
        url = [_android_ndk_url],
        output = "",
        stripPrefix = _android_ndk_prefix,
        sha256 = _android_ndk_sha256,
    )

    # Make various subsets of the NDK available via filegroups, which we use from cc_toolchain
    # rules.
    # https://bazel.build/rules/lib/repository_ctx#file
    ctx.file(
        "BUILD.bazel",
        content = """
filegroup(
    name = "arm64-v8a_all_files",
    srcs = glob(["toolchains/llvm/**"]) + glob([
        "platforms/android-29/arch-arm64/**/*",
        "sources/cxx-stl/llvm-libc++/include/**/*",
        "sources/cxx-stl/llvm-libc++abi/include/**/*",
        "sources/android/support/include/**/*",
        "sysroot/**/*",
        "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/**/*",
    ]) + [
      ":arm64-v8a_dynamic_runtime_libraries",
      ":arm64-v8a_static_runtime_libraries",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "arm64-v8a_dynamic_runtime_libraries",
    srcs = glob(["sources/cxx-stl/llvm-libc++/libs/arm64-v8a/*.so"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "arm64-v8a_static_runtime_libraries",
    srcs = glob(["sources/cxx-stl/llvm-libc++/libs/arm64-v8a/*.a"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "armeabi-v7a_all_files",
    srcs = glob(["toolchains/llvm/**"]) + glob([
        "platforms/android-29/arch-arm/**/*",
        "sources/cxx-stl/llvm-libc++/include/**/*",
        "sources/cxx-stl/llvm-libc++abi/include/**/*",
        "sources/android/support/include/**/*",
        "sysroot/**/*",
        "toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/**/*",
    ]) + [
      ":armeabi-v7a_dynamic_runtime_libraries",
      ":armeabi-v7a_static_runtime_libraries",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "armeabi-v7a_dynamic_runtime_libraries",
    srcs = glob(["sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/*.so"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "armeabi-v7a_static_runtime_libraries",
    srcs = glob(["sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/*.a"]),
    visibility = ["//visibility:public"],
)
""",
        executable = False,
    )


_download_android_ndk = repository_rule(
    implementation = _download_android_ndk_impl,
    attrs = {},
    doc = "Downloads the Android NDK.",
)

_rule_name = "android_ndk"

def download_android_ndk():
    """Downloads the Android NDK under external/android_ndk."""
    _download_android_ndk(name = _rule_name)

# Path to the Android NDK from the point of view of the cc_toolchain rule.
NDK_PATH = "external/%s" % _rule_name
