load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")
load(
    "@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "feature",
    "flag_group",
    "flag_set",
    "tool_path",
    "with_feature_set",
)

# NDK_PATH = "/usr/local/google/home/lovisolo/android-ndk-r21e" # DO NOT SUBMIT
NDK_PATH = "external/android_ndk"

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


    # clang_trampoline_file = ctx.actions.declare_file(ctx.attr.name + ".clang_trampoline")
    # ctx.actions.write(clang_trampoline_file, """#!/bin/bash
    # external/android_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang $@
    # """, is_executable=True)

    # print("Hello world!")
    # print(clang_trampoline_file.root.path)
    # print("Bye world!")


    tool_paths = [
        tool_path(
            name = "ar",
            # path = "/usr/bin/ar",
            path = "aarch64-linux-android-ar.sh",
            # path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-ar",
        ),
        tool_path(
            name = "cpp",
            path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-cpp",
        ),
        # tool_path(
        #     name = "dwp",
        #     path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-dwp",
        # ),
        tool_path(
            name = "gcc",
            path = "clang.sh",
            # path = clang_trampoline_file.basename,
            # path = ctx.attr._clang.files_to_run.executable.path,
            # path = "external/android_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang",
            # path = NDK_PATH + "/toolchains/llvm/prebuilt/linux-x86_64/bin/clang",
            # path = "@android_ndk//:toolchains/llvm/prebuilt/linux-x86_64/bin/clang",
        ),
        tool_path(
            name = "gcov",
            path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-gcov",
        ),
        # tool_path(
        #     name = "gcov_tool",
        #     path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-gcov-tool",
        # ),
        tool_path(
            name = "ld",
            path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-ld",
        ),
        tool_path(
            name = "nm",
            path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-nm",
        ),
        # tool_path(
        #     name = "objcopy",
        #     path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-objcopy",
        # ),
        tool_path(
            name = "objdump",
            path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-objdump",
        ),
        tool_path(
            name = "strip",
            path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-strip",
        ),
        # tool_path(
        #     name = "llvm_profdata",
        #     path = NDK_PATH + "/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-llvm-profdata",
        # ),
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "ndk-arm64-v8a-toolchain",
        host_system_name = "local",
        target_system_name = "aarch64-linux-android", # Or "arm-linux-androideabi" for 32-bit ARM.
        target_cpu = "arm64-v8a",
        target_libc = "local",
        compiler = "clang9.0.8",
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

ndk_arm64_v8a_toolchain_config = rule(
    implementation = _impl,
    attrs = {},
    provides = [CcToolchainConfigInfo],
)
