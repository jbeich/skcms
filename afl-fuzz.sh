#!/bin/sh

set -e

afl-clang skcms.c fuzz/fuzz_iccprofile_info.c -o fuzz_iccprofile_info -DIS_FUZZING_WITH_AFL -lm
mkdir -p inputs
find profiles -name '*.icc' -exec cp {} inputs \;
exec afl-fuzz -i inputs -o findings ./fuzz_iccprofile_info @@
