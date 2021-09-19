# SkCMS Bazel build

This file provides instructions on how to build and test SkCMS using
[Bazel](https://bazel.build).

## Linux

### Building and testing locally

Open a terminal and `cd` into your SkCMS repository checkout, then run:

```
$ bazel build //...

$ bazel test //...
```

### Building and testing on RBE

Same as above, but add `--config=linux-rbe` to your `bazel` invocation, e.g.:

```
$ bazel build //... --config=linux-rbe

$ bazel test //... --config=linux-rbe
```

Note that you need to obtain RBE credentials for this to work (instructions below).

## macOS

TODO(lovisolo)

## Windows

SkCMS can be compiled with either
[Microsoft Build Tools for Visual Studio 2019](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019)
or [Clang](https://clang.llvm.org/).

Before continuing, install Bazel by following the instructions
[here](https://docs.bazel.build/versions/4.2.1/install-windows.html). Make sure
to include `bazel` binary in your `PATH`.

Note that Bazel requires symlink support to function properly. Enable symlink
support by enabling
[Developer Mode](https://docs.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development),
or by running Bazel as an administrator
([reference](https://docs.bazel.build/versions/main/windows.html#enable-symlink-support)).

### Building and testing locally

The below instructions are based on the
[Build on Windows](https://docs.bazel.build/versions/main/windows.html#build-on-windows)
section of the Bazel documentation.

#### With Build Tools for Visual Studio 2019

Download and install Build Tools for Visual Studio 2019 using this
[link](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019).
Select "Desktop development with C++" on the installation wizard, and leave all
other items unchanged.

Open `cmd.exe` and `cd` into your SkCMS repository checkout. Set the `BAZEL_VC`
environment variable to point to your Build Tools for Visual Studio 2019
installation:

```
> set BAZEL_VC=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC
```

Then run:

```
> bazel build //...

> bazel test //... --enable_runfiles
```

Reference
[here](https://docs.bazel.build/versions/main/windows.html#build-c-with-msvc).

TODO(lovisolo): Consider adding `--enable_runfiles` to `//.bazelrc`.

#### With Clang

In order to build with Clang, you have to install **both** LLVM and Build Tools
for Visual Studio 2019 (rationale
[here](https://docs.bazel.build/versions/main/windows.html#build-c-with-clang)).
Please install the latter by following the above instructions before proceeding.

Download and install LLVM from this
[link](https://github.com/llvm/llvm-project/releases/tag/llvmorg-12.0.1).

Open `cmd.exe` and `cd` into your SkCMS repository checkout, then run:

```
> bazel build //... --compiler=clang-cl

> bazel test //... --compiler=clanmg-cl --enable_runfiles
```

If the above commands fail because Bazel cannot find your LLVM installation, set
the `BAZEL_LLVM` environment variable to point to your LLVM installation:

```
> set BAZEL_LLVM=C:\Program Files\LLVM
```

Reference
[here](https://docs.bazel.build/versions/main/windows.html#build-c-with-clang).

TODO(lovisolo): Investigate adding a platform target to the top-level
`BUILD.bazel` file as per the instructions
[here](https://docs.bazel.build/versions/main/windows.html#build-c-with-clang).

### Building and testing on RBE

TODO(lovisolo)

## RBE Credentials

Note that running remote builds requires a service account key with the correct permissions.

Instructions:

Step 1: Create service account under the skia-public GCP project, if you don't have one already:

```
$ gcloud iam service-accounts create somegoogler-rbe \
      --description "somegoogler's RBE service account" \
      --project skia-public
```

Step 2: Grant your service account the
[Remote Build Execution Artifact Creator](https://cloud.google.com/remote-build-execution/docs/access-control#granting_the_ability_to_run_builds_remotely)
role under the skia-rbe GCP project, which is where Skia's
[RBE instance](https://pantheon.corp.google.com/apis/api/remotebuildexecution.googleapis.com/overview?project=skia-rbe)
lives:

```
$ gcloud projects add-iam-policy-binding skia-rbe \
      --role roles/remotebuildexecution.artifactCreator \
      --member serviceAccount:somegoogler-rbe@skia-public.iam.gserviceaccount.com
```

Step 3: Create a JSON service account key:

```
$ gcloud iam service-accounts keys create path/to/somegoogler-rbe.json \
      --project skia-public \
      --iam-account somegoogler-rbe@skia-public.iam.gserviceaccount.com
```

Step 4: Create a .bazelrc file in your home directory with the following contents:

```
build:remote --google_credentials=path/to/somegoogler-rbe.json
```

Note that service account keys expire after 3 months, so you might have to repeat this step if
you run into permission issues.
