#!/bin/sh
# SPU-94 Phase 26 — Linux plugin installer
# Copies VST3, LV2, and CLAP plugins to standard user scan paths.
# Run from the extracted tarball root where plugin artifacts are siblings
# of this script.
#
# Usage:
#   ./install.sh              Install plugins
#   ./install.sh --uninstall  Remove installed plugins

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

VST3_SRC="$SCRIPT_DIR/SPU-94.vst3"
LV2_SRC="$SCRIPT_DIR/SPU-94.lv2"
CLAP_SRC="$SCRIPT_DIR/SPU-94.clap"

VST3_DEST="$HOME/.vst3/SPU-94.vst3"
LV2_DEST="$HOME/.lv2/SPU-94.lv2"
CLAP_DEST="$HOME/.clap/SPU-94.clap"

uninstall() {
    if [ -e "$VST3_DEST" ]; then
        rm -rf "$VST3_DEST"
        echo "Removed: $VST3_DEST"
    else
        echo "Not found (skipped): $VST3_DEST"
    fi

    if [ -e "$LV2_DEST" ]; then
        rm -rf "$LV2_DEST"
        echo "Removed: $LV2_DEST"
    else
        echo "Not found (skipped): $LV2_DEST"
    fi

    if [ -e "$CLAP_DEST" ]; then
        rm -f "$CLAP_DEST"
        echo "Removed: $CLAP_DEST"
    else
        echo "Not found (skipped): $CLAP_DEST"
    fi

    echo "SPU-94 uninstalled."
}

install() {
    # Verify source artifacts exist
    if [ ! -d "$VST3_SRC" ]; then
        echo "Error: VST3 bundle not found at $VST3_SRC" >&2
        exit 1
    fi
    if [ ! -d "$LV2_SRC" ]; then
        echo "Error: LV2 bundle not found at $LV2_SRC" >&2
        exit 1
    fi
    if [ ! -f "$CLAP_SRC" ]; then
        echo "Error: CLAP binary not found at $CLAP_SRC" >&2
        exit 1
    fi

    # Create target directories
    mkdir -p "$HOME/.vst3"
    mkdir -p "$HOME/.lv2"
    mkdir -p "$HOME/.clap"

    # Copy plugins
    rm -rf "$VST3_DEST"
    cp -r "$VST3_SRC" "$VST3_DEST"
    echo "Installed: $VST3_DEST"

    rm -rf "$LV2_DEST"
    cp -r "$LV2_SRC" "$LV2_DEST"
    echo "Installed: $LV2_DEST"

    rm -f "$CLAP_DEST"
    cp "$CLAP_SRC" "$CLAP_DEST"
    echo "Installed: $CLAP_DEST"

    echo "SPU-94 installed. Restart your DAW to scan for new plugins."
}

case "${1:-}" in
    --uninstall)
        uninstall
        ;;
    "")
        install
        ;;
    *)
        echo "Usage: $0 [--uninstall]" >&2
        exit 1
        ;;
esac
