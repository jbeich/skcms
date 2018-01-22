#!/bin/sh

set -e

afl-clang skcms.c iccdump.c -o iccdump
mkdir -p inputs
find profiles -name '*.icc' -exec cp {} inputs \;
exec afl-fuzz -i inputs -o findings ./iccdump @@
