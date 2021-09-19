# Bazel toolchain configurations for RBE

This directory contains Bazel toolchain configurations for RBE grouped by operating system.

It might be necessary to rebuild these configurations when the RBE toolchain container image
for a specific operating system is updated, or when a new Bazel version is released.

## Linux

Directories `linux-bazel-<BAZEL VERSION>` contain Bazel toolchain configurations for Linux RBE
builds. Multiple such directories may exist to ease migrating from one Bazel version to the next.

### Config regeneration instructions

#### Step 1

Clone the [bazel-toolchains](https://github.com/bazelbuild/bazel-toolchains) repository, build the
`rbe_configs_gen` binary, and place it on your $PATH:

```
$ git clone https://github.com/bazelbuild/bazel-toolchains

$ cd bazel-toolchains

# This assumes that $HOME/bin is in your $PATH.
$ go build -o $HOME/bin/rbe_configs_gen ./cmd/rbe_configs_gen/rbe_configs_gen.go
```

#### Step 2

Generate a new `//toolchains/linux-bazel-<BAZEL VERSION>` directory with the `rbe_configs_gen` CLI
tool (installation instructions below):

```
# Replace the <PLACEHOLDERS> as needed.
$ rbe_configs_gen \
      --bazel_version=<BAZEL VERSION> \
      --toolchain_container=l.gcr.io/google/rbe-ubuntu16-04:latest \
      --output_src_root=<PATH TO REPOSITORY CHECKOUT> \
      --output_config_path=toolchains/linux-bazel-<BAZEL VERSION> \
      --exec_os=linux \
      --target_os=linux
```

If `rbe_configs_gen` fails, try deleting all files under `//bazel/rbe` (except for this file) and
re-run `rbe_configs_gen`.

#### Step 3

Add an empty `WORKSPACE` file inside the directory created in the previous step.

#### Step 4

Update the paths in `//WORKSPACE.bazel` as needed. 

## Windows

TODO(lovisolo)

## macOS

TODO(lovisolo)
