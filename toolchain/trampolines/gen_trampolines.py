#!/usr/bin/env python3

"""
Helper script to generate trampoline scripts for NDK tools.
"""

import argparse
import os
import sys

BAZEL_NDK_PATH = "external/android_ndk"

# Paths relative to the Android NDK root directory.
tools = [
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-ar",
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-ld",
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-nm",
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-objcopy",
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-objdump",
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-strip",
  "toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-dwp",
  "toolchains/llvm/prebuilt/linux-x86_64/bin/clang",
]

parser = argparse.ArgumentParser(description="Generates trampoline scripts for NDK tools.")
parser.add_argument(
  "--ndk-dir",
  dest="ndk_dir",
  required=True,
  help="Path to a local NDK. Used only to verify that the tool paths assumed by this program are valid.")
parser.add_argument(
  "--out-dir",
  dest="out_dir",
  required=True,
  help="Where to save the trampoline scripts.")
args = parser.parse_args()

for tool in tools:
  # Verify that the tool exists in the NDK.
  ndk_path = os.path.join(args.ndk_dir, tool)
  if not os.path.exists(ndk_path):
    print("File %s not found." % ndk_path)
    sys.exit(1)
  
  # Generate script.
  script_basename = "%s.sh" % os.path.basename(tool)
  script_path = os.path.join(args.out_dir, script_basename)
  with open(script_path, "w") as f:
    f.write("\n".join([
      "#!/bin/sh",
      "%s $@" % os.path.join(BAZEL_NDK_PATH, tool),
      ""
    ]))
  os.chmod(script_path, 0o750)
