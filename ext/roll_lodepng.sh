#!/bin/bash

set -e
set -x

here=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
lodepng=$(mktemp -d)

git clone --depth 1 https://github.com/lvandeve/lodepng $lodepng
git -C $lodepng checkout-index -a -f --prefix $here/lodepng/
git add lodepng
