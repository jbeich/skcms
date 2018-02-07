#!/bin/sh

set -e

afl-clang skcms.c fuzz/fuzz_iccprofile_info.c fuzz/fuzz_main.c -lm -o fuzz_iccprofile_info
mkdir -p inputs
find profiles -name '*.icc' -exec cp {} inputs \;
exec afl-fuzz -i inputs -o findings ./fuzz_iccprofile_info @@
