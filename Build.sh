#!/bin/sh
# Usage: ./Build.sh [release] [analyze] [asan] [test]
set -e

mode=debug
analyze=0
asan=0
test=0
for arg in "$@"; do
    case "$arg" in
        release) mode=release ;;
        analyze) analyze=1 ;;
        asan)    asan=1 ;;
        test)    test=1 ;;
    esac
done

root=$(cd "$(dirname "$0")" && pwd)
out="$root/Bin/$mode"
mkdir -p "$out"

cc=${CC:-clang}
cflags="-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter -Wno-unused-function -I$root/Source -D_GNU_SOURCE -g"
if [ "$mode" = release ]; then
    cflags="$cflags -O2 -DNDEBUG"
else
    cflags="$cflags -O0 -DDEBUG"
fi
if [ "$asan" = 1 ]; then
    cflags="$cflags -fsanitize=address,undefined -fno-omit-frame-pointer"
fi

if [ "$analyze" = 1 ]; then
    $cc $cflags --analyze -Xclang -analyzer-output=text "$root/Source/Unity.c" -o /dev/null
fi

$cc $cflags "$root/Source/Unity.c" -o "$out/filey"

if [ "$test" = 1 ]; then
    $cc $cflags "$root/Source/Tests/Tests.c" -o "$out/filey-tests"
    "$out/filey-tests"
fi
