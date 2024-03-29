#!/usr/bin/env bash

set -euo pipefail

flags=(
    -D_DEFAULT_SOURCE
    -D_POSIX_C_SOURCE
    "-ferror-limit=1"
    "-fsanitize=address"
    "-fsanitize=bounds"
    "-fsanitize=float-divide-by-zero"
    "-fsanitize=implicit-conversion"
    "-fsanitize=integer"
    "-fsanitize=nullability"
    "-fsanitize=undefined"
    -fshort-enums
    -g
    -lGL
    -lglfw
    "-march=native"
    -O1
    "-std=c99"
    -Werror
    -Weverything
    -Wno-c2x-extensions
    -Wno-declaration-after-statement
    -Wno-extra-semi-stmt
    -Wno-padded
)
clang-format -i -verbose "$WD/src/"*
mold -run clang "${flags[@]}" -o "$WD/bin/main" "$WD/src/main.c"
"$WD/bin/main" "$WD/assets/config"
