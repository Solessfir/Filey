#!/bin/sh
# Usage: ./Build.sh [release] [analyze] [asan] [profile] [test] [install]
set -e

mode=debug
analyze=0
asan=0
test=0
profile=0
install=0
for arg in "$@"; do
    case "$arg" in
        release) mode=release ;;
        analyze) analyze=1 ;;
        asan)    asan=1 ;;
        test)    test=1 ;;
        profile) profile=1 ;;
        install) install=1 ;;
    esac
done

root=$(cd "$(dirname "$0")" && pwd)
out="$root/Bin/$mode"
mkdir -p "$out"

cc=${CC:-clang}
cflags="-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter -Wno-unused-function -I$root/Source -D_GNU_SOURCE -g -pthread"
if [ "$mode" = release ]; then
    cflags="$cflags -O2 -DNDEBUG"
else
    cflags="$cflags -O0 -DDEBUG"
fi
if [ "$asan" = 1 ]; then
    cflags="$cflags -fsanitize=address,undefined -fno-omit-frame-pointer"
fi

if [ "$profile" = 1 ]; then
    cflags="$cflags -DPROFILE"
fi

# EXTRA_CFLAGS passes ad-hoc defines such as -DLOG_EVENTS.
cflags="$cflags ${EXTRA_CFLAGS:-}"
# FILEY_VERSION, such as 1.2.3 for a release, is the version the app shows; 0.1.0 without it.
if [ -n "${FILEY_VERSION:-}" ]; then
    cflags="$cflags -DFILEY_VERSION=\"$FILEY_VERSION\""
fi

app_packages="wayland-client wayland-cursor wayland-egl egl xkbcommon freetype2 fontconfig"
app_cflags=$(pkg-config --cflags $app_packages)
app_libs=$(pkg-config --libs $app_packages)

# Generators run on every build and rewrite their outputs only when the content changes.
$cc $cflags "$root/Source/Tools/Codegen.c" -o "$out/codegen"
"$out/codegen" "$root/Source"

if [ "$analyze" = 1 ]; then
    $cc $cflags $app_cflags --analyze -Xclang -analyzer-output=text "$root/Source/Unity.c" -o /dev/null
fi

$cc $cflags $app_cflags "$root/Source/Unity.c" $app_libs -lm -o "$out/filey"

if [ "$test" = 1 ]; then
    $cc $cflags "$root/Source/Tests/Tests.c" -lm -o "$out/filey-tests"
    "$out/filey-tests"
fi

# Installs this build for the current user: the binary in ~/.local/bin, and the desktop entry and icons that launchers and the compositor find it by.
if [ "$install" = 1 ]; then
    data=${XDG_DATA_HOME:-$HOME/.local/share}
    bin="$HOME/.local/bin"
    resources="$root/Source/Resources"
    install -Dm755 "$out/filey" "$bin/filey"
    mkdir -p "$data/applications"
    sed "s|^Exec=filey|Exec=$bin/filey|" "$resources/filey.desktop" > "$data/applications/filey.desktop"
    install -Dm644 "$resources/Filey.svg" "$data/icons/hicolor/scalable/apps/filey.svg"
    install -Dm644 "$resources/filey-symbolic.svg" "$data/icons/hicolor/symbolic/apps/filey-symbolic.svg"
    for png in "$resources"/Icons/filey-*.png; do
        size=${png##*-}
        size=${size%.png}
        install -Dm644 "$png" "$data/icons/hicolor/${size}x${size}/apps/filey.png"
    done
    if command -v update-desktop-database >/dev/null; then update-desktop-database "$data/applications"; fi
    if command -v gtk-update-icon-cache >/dev/null; then gtk-update-icon-cache -q -t "$data/icons/hicolor" || true; fi
    echo "Installed to $bin/filey and $data"
fi
