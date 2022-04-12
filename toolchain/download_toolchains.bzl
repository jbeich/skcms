"""
This module defines the download_android_ndk repository rule.
"""
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_download_android_ndk_rule_name = "android_ndk"

def _download_android_ndk(name):
    """Downloads the Android NDK under external/android_ndk.

    Args:
      name: Name of the external repository. This MUST equal "android_ndk".
    """

    # The toolchain assumes that the NDK is found at /external/android_ndk, so we must enforce this
    # name.
    if name != _download_android_ndk_rule_name:
        fail("The name of this rule MUST be \"%s\"" % _download_android_ndk_rule_name)

    # Archive taken from https://github.com/android/ndk/wiki/Unsupported-Downloads#r21e.
    http_archive(
        name = _download_android_ndk_rule_name,
        urls = [
            "https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip",
            "https://storage.googleapis.com/skia-world-readable/bazel/ad7ce5467e18d40050dc51b8e7affc3e635c85bd8c59be62de32352328ed467e.zip",
        ],
        sha256 = "ad7ce5467e18d40050dc51b8e7affc3e635c85bd8c59be62de32352328ed467e",
        strip_prefix = "android-ndk-r21e",
        build_file = Label("//toolchain:ndk.BUILD"),
    )

def download_toolchains(android_ndk_repository_name):
    """Downloads the toolchains needed to build this repository.

    Args:
      android_ndk_repository_name: Name of the external repository with the android NDK. This MUST
        equal "android_ndk".
    """
    _download_android_ndk(android_ndk_repository_name)

# Path to the Android NDK from the point of view of the cc_toolchain rule.
NDK_PATH = "external/%s" % _download_android_ndk_rule_name
