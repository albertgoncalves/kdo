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
    -O3
    "-std=c99"
    -Werror
    -Weverything
    -Wno-c2x-extensions
    -Wno-covered-switch-default
    -Wno-declaration-after-statement
    -Wno-extra-semi-stmt
    -Wno-padded
    -Wno-unsafe-buffer-usage
)
clang-format -i -verbose "$WD/src/"*
mold -run clang "${flags[@]}" -o "$WD/bin/main" "$WD/src/main.c"
"$WD/bin/main"
