# Bazel toolchain configuration for RBE

---

**DO NOT EDIT THIS DIRECTORY BY HAND.**

All files in this directory (excluding this file) are generated with the `rbe_configs_gen` CLI
tool. Keep reading for details.

---

This directory contains a Bazel toolchain configuration for RBE. It is generated with the
`rbe_configs_gen` CLI tool from the
[bazel-toolchains](https://github.com/bazelbuild/bazel-toolchains) repository.

This directory is referenced from `//.bazelrc`.

## Upgrading to a new Bazel version

Take the following steps to upgrade to a new Bazel version.

### Step 1

Update the `//.bazelversion` file with the new Bazel version. This file is read by
[Bazelisk](https://github.com/bazelbuild/bazelisk) (for those engineers who use `bazelisk`
as a replacement for `bazel`).

### Step 2

Regenerate the `//bazel/rbe` directory with the `rbe_configs_gen` CLI tool (installation
instructions below):

```
# Replace the <PLACEHOLDERS> as needed.
$ rbe_configs_gen \
      --bazel_version=<BAZEL VERSION> \
      --toolchain_container=l.gcr.io/google/rbe-ubuntu16-04:latest \
      --output_src_root=<PATH TO REPOSITORY CHECKOUT> \
      --output_config_path=bazel/rbe \
      --exec_os=linux \
      --target_os=linux
```

If `rbe_configs_gen` fails, try deleting all files under `//bazel/rbe` (except for this file) and
re-run `rbe_configs_gen`.

## How to install the `rbe_configs_gen` CLI tool

Clone the [bazel-toolchains](https://github.com/bazelbuild/bazel-toolchains) repository outside of
the Buildbot repository checkout, build the `rbe_configs_gen` binary, and place it on your $PATH:

```
$ git clone https://github.com/bazelbuild/bazel-toolchains

$ cd bazel-toolchains

# This assumes that $HOME/bin is in your $PATH.
$ go build -o $HOME/bin/rbe_configs_gen ./cmd/rbe_configs_gen/rbe_configs_gen.go
```
