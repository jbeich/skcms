"""
This module defines the download_android_ndk repository rule.
"""
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_rule_name = "android_ndk"

def download_android_ndk():
    """Downloads the Android NDK under external/android_ndk."""

    # Archive taken from https://github.com/android/ndk/wiki/Unsupported-Downloads#r21e.
    http_archive(
        name = _rule_name,
        urls = ["https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip"],
        sha256 = "ad7ce5467e18d40050dc51b8e7affc3e635c85bd8c59be62de32352328ed467e",
        strip_prefix = "android-ndk-r21e",
        build_file = Label("//toolchain:ndk.BUILD"),
    )

# Path to the Android NDK from the point of view of the cc_toolchain rule.
NDK_PATH = "external/%s" % _rule_name
