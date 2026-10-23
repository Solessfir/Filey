#!/usr/bin/env bash
# Installs the latest Filey release for the current user, or removes it: the binary in ~/.local/bin, and the desktop entry and icons
# that launchers find it by. Settings in ~/.config/filey and the cache in ~/.cache/filey are left alone.
#   curl -sSL https://raw.githubusercontent.com/Solessfir/Filey/main/install.sh | bash -s install
#   curl -sSL https://raw.githubusercontent.com/Solessfir/Filey/main/install.sh | bash -s uninstall
set -euo pipefail

REPO="Solessfir/Filey"
BIN_DIR="$HOME/.local/bin"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS_DIR="$DATA_DIR/applications"
ICONS_DIR="$DATA_DIR/icons/hicolor"

refresh() {
    if command -v update-desktop-database >/dev/null; then update-desktop-database "$APPS_DIR" || true; fi
    if command -v gtk-update-icon-cache >/dev/null; then gtk-update-icon-cache -q -t "$ICONS_DIR" || true; fi
}

install_filey() {
    echo "Finding the latest release..."
    url=$(curl -sSL "https://api.github.com/repos/$REPO/releases/latest" | grep -oE '"browser_download_url": *"[^"]*linux-x64\.tar\.gz"' | cut -d '"' -f 4 | head -n 1)
    if [ -z "$url" ]; then
        echo "Error: no Linux build in the latest release of $REPO" >&2
        exit 1
    fi
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    echo "Downloading ${url##*/}..."
    curl -sSL -o "$work/filey.tar.gz" "$url"
    tar -xzf "$work/filey.tar.gz" -C "$work"
    content=$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -n 1)

    install -Dm755 "$content/filey" "$BIN_DIR/filey"
    mkdir -p "$APPS_DIR"
    sed "s|^Exec=filey|Exec=$BIN_DIR/filey|" "$content/filey.desktop" > "$APPS_DIR/filey.desktop"
    install -Dm644 "$content/Filey.svg" "$ICONS_DIR/scalable/apps/filey.svg"
    install -Dm644 "$content/filey-symbolic.svg" "$ICONS_DIR/symbolic/apps/filey-symbolic.svg"
    for png in "$content"/icons/filey-*.png; do
        size=${png##*-}
        size=${size%.png}
        install -Dm644 "$png" "$ICONS_DIR/${size}x${size}/apps/filey.png"
    done
    refresh
    echo "Installed Filey to $BIN_DIR/filey"
    case ":$PATH:" in *":$BIN_DIR:"*) ;; *) echo "Note: $BIN_DIR is not on your PATH; launchers still find Filey." ;; esac
}

uninstall_filey() {
    rm -f "$BIN_DIR/filey" "$APPS_DIR/filey.desktop" "$ICONS_DIR/scalable/apps/filey.svg" "$ICONS_DIR/symbolic/apps/filey-symbolic.svg"
    rm -f "$ICONS_DIR"/*/apps/filey.png
    refresh
    echo "Removed Filey. Its settings in ~/.config/filey and cache in ~/.cache/filey are kept; delete them to remove everything."
}

case "${1:-}" in
    install) install_filey ;;
    uninstall) uninstall_filey ;;
    *) echo "Usage: install.sh install|uninstall" >&2; exit 1 ;;
esac
