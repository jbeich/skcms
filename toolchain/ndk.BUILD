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
