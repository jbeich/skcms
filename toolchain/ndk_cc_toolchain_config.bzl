"""This module defines the ndk_cc_toolchain_config rule."""

load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")
load(
    "@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "feature",
    "flag_group",
    "flag_set",
    "tool_path",
    "with_feature_set",
)
load("download_android_ndk.bzl", "NDK_PATH")

# Supported CPUs.
_ARMEABI_V7A = "armeabi-v7a"
_ARM64_V8A = "arm64-v8a"

_all_compile_actions = [
    ACTION_NAMES.c_compile,
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.linkstamp_compile,
    ACTION_NAMES.assemble,
    ACTION_NAMES.preprocess_assemble,
    ACTION_NAMES.cpp_header_parsing,
    ACTION_NAMES.cpp_module_compile,
    ACTION_NAMES.cpp_module_codegen,
    ACTION_NAMES.clif_match,
    ACTION_NAMES.lto_backend,
]

_all_link_actions = [
    ACTION_NAMES.cpp_link_executable,
    ACTION_NAMES.cpp_link_dynamic_library,
    ACTION_NAMES.cpp_link_nodeps_dynamic_library,
]

def _impl(ctx):
    default_compile_flags = [
        "-gcc-toolchain",
        NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64",
        "-target",
        "aarch64-none-linux-android",
        "-fpic",
        "-isystem",
        NDK_PATH + "/sysroot/usr/include/aarch64-linux-android",
        "-D__ANDROID_API__=29",
        "-no-canonical-prefixes",
        "-Wno-invalid-command-line-argument",
        "-Wno-unused-command-line-argument",
        "-funwind-tables",
        "-fstack-protector-strong",
        "-fno-addrsig",
        "-Werror=return-type",
        "-Werror=int-to-pointer-cast",
        "-Werror=pointer-to-int-cast",
        "-Werror=implicit-function-declaration",
    ]
    unfiltered_compile_flags = [
        "-isystem",
        NDK_PATH + "/sources/cxx-stl/llvm-libc++/include",
        "-isystem",
        NDK_PATH + "/sources/cxx-stl/llvm-libc++abi/include",
        "-isystem",
        NDK_PATH + "/sources/android/support/include",
        "-isystem",
        NDK_PATH + "/sysroot/usr/include",
    ]
    default_link_flags = [
        "-gcc-toolchain",
        NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64",
        "-target",
        "aarch64-none-linux-android",
        "-L",
        NDK_PATH + "/sources/cxx-stl/llvm-libc++/libs/arm64-v8a",
        "-no-canonical-prefixes",
        "-Wl,-z,relro",
        "-Wl,--gc-sections",
    ]
    default_fastbuild_flags = [""]
    default_dbg_flags = ["-O0", "-g", "-UNDEBUG"]
    default_opt_flags = ["-O2", "-g", "-DNDEBUG"]

    opt_feature = feature(name = "opt")
    fastbuild_feature = feature(name = "fastbuild")
    dbg_feature = feature(name = "dbg")
    supports_dynamic_linker_feature = feature(name = "supports_dynamic_linker", enabled = True)
    supports_pic_feature = feature(name = "supports_pic", enabled = True)
    static_link_cpp_runtimes_feature = feature(name = "static_link_cpp_runtimes", enabled = True)

    default_compile_flags_feature = feature(
        name = "default_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = _all_compile_actions,
                flag_groups = [flag_group(flags = default_compile_flags)],
            ),
            flag_set(
                actions = _all_compile_actions,
                flag_groups = [flag_group(flags = default_fastbuild_flags)],
                with_features = [with_feature_set(features = ["fastbuild"])],
            ),
            flag_set(
                actions = _all_compile_actions,
                flag_groups = [flag_group(flags = default_dbg_flags)],
                with_features = [with_feature_set(features = ["dbg"])],
            ),
            flag_set(
                actions = _all_compile_actions,
                flag_groups = [flag_group(flags = default_opt_flags)],
                with_features = [with_feature_set(features = ["opt"])],
            ),
        ],
    )

    default_link_flags_feature = feature(
        name = "default_link_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = _all_link_actions,
                flag_groups = [flag_group(flags = default_link_flags)],
            ),
        ],
    )

    user_compile_flags_feature = feature(
        name = "user_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = _all_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = ["%{user_compile_flags}"],
                        iterate_over = "user_compile_flags",
                        expand_if_available = "user_compile_flags",
                    ),
                ],
            ),
        ],
    )

    sysroot_feature = feature(
        name = "sysroot",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = _all_compile_actions + _all_link_actions,
                flag_groups = [
                    flag_group(
                        flags = ["--sysroot=%{sysroot}"],
                        expand_if_available = "sysroot",
                    ),
                ],
            ),
        ],
    )

    unfiltered_compile_flags_feature = feature(
        name = "unfiltered_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = _all_compile_actions,
                flag_groups = [flag_group(flags = unfiltered_compile_flags)],
            ),
        ],
    )

    features = [
        default_compile_flags_feature,
        default_link_flags_feature,
        supports_dynamic_linker_feature,
        supports_pic_feature,
        static_link_cpp_runtimes_feature,
        fastbuild_feature,
        dbg_feature,
        opt_feature,
        user_compile_flags_feature,
        sysroot_feature,
        unfiltered_compile_flags_feature,
    ]
 
    # The cc_common.create_cc_toolchain_config_info function expects tool paths to point to files
    # under the directory in which it is invoked. This means we cannot directly reference tools
    # under external/android_ndk. The solution is to use "trampoline" scripts that pass through
    # any command-line arguments to the NDK binaries under external/android_sdk.
    tool_paths = [
        tool_path(
            name = "ar",
            path = "trampolines/aarch64-linux-android-ar.sh",
        ),
        tool_path(
            name = "cpp",
            path = "trampolines/clang.sh",
        ),
        tool_path(
            name = "dwp",
            path = "trampolines/aarch64-linux-android-dwp.sh",
        ),
        tool_path(
            name = "gcc",
            path = "trampolines/clang.sh",
        ),
        tool_path(
            name = "gcov",
            path = "/bin/false",
        ),
        tool_path(
            name = "ld",
            path = "trampolines/aarch64-linux-android-ld.sh",
        ),
        tool_path(
            name = "nm",
            path = "trampolines/aarch64-linux-android-nm.sh",
        ),
        tool_path(
            name = "objcopy",
            path = "trampolines/aarch64-linux-android-objcopy.sh",
        ),
        tool_path(
            name = "objdump",
            path = "trampolines/aarch64-linux-android-objdump.sh",
        ),
        tool_path(
            name = "strip",
            path = "trampolines/aarch64-linux-android-strip.sh",
        ),
    ]

    # https://bazel.build/rules/lib/cc_common#create_cc_toolchain_config_info
    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "ndk-arm64-v8a-toolchain",
        host_system_name = "local",
        target_system_name = "aarch64-linux-android", # Or "arm-linux-androideabi" for 32-bit ARM.
        target_cpu = "arm64-v8a",
        target_libc = "local",
        compiler = "clang9.0.9",
        abi_version = "arm64-v8a", 
        abi_libc_version = "local",
        features = features,
        tool_paths = tool_paths,
        cxx_builtin_include_directories = [
            NDK_PATH + "/toolchains/llvm/prebuilt/linux-x86_64/lib64/clang/9.0.9/include",
            "%sysroot%/usr/include",
            NDK_PATH + "/sysroot/usr/include",
        ],
        builtin_sysroot = NDK_PATH + "/platforms/android-29/arch-arm64",
    )

ndk_cc_toolchain_config = rule(
    implementation = _impl,
    attrs = {
        "cpu": attr.string(
            mandatory = True,
            values = [_ARMEABI_V7A, _ARM64_V8A],
            doc = "Target CPU.",
        )
    },
    provides = [CcToolchainConfigInfo],
)
