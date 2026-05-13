#!/bin/bash
# SPU-94 Phase 26 — macOS .pkg installer creation script
# Creates a flat .pkg that installs VST3, AU, and CLAP plugins to standard
# macOS system scan paths. Requires admin privileges at install time (the AU
# component must live at /Library/Audio/Plug-Ins/Components/ per Apple's
# documented path — user-path AU is unreliable in Logic).
#
# Usage:
#   ./create_pkg.sh <artifacts-dir> [version]
#
# Arguments:
#   artifacts-dir  Path to the JUCE build artifacts directory
#                  (e.g. build/src/plugin/spu94_plugin_artefacts/Release)
#   version        Version string for the .pkg (default: 0.0.0)

set -euo pipefail

ARTIFACTS_DIR="${1:?Usage: $0 <artifacts-dir> [version]}"
VERSION="${2:-0.0.0}"

# Validate source artifacts
AU_SRC="$ARTIFACTS_DIR/AU/SPU-94.component"
VST3_SRC="$ARTIFACTS_DIR/VST3/SPU-94.vst3"
CLAP_SRC="$ARTIFACTS_DIR/CLAP/SPU-94.clap"

for src in "$AU_SRC" "$VST3_SRC"; do
    if [ ! -d "$src" ]; then
        echo "Error: bundle not found at $src" >&2
        exit 1
    fi
done
if [ ! -f "$CLAP_SRC" ]; then
    echo "Error: CLAP binary not found at $CLAP_SRC" >&2
    exit 1
fi

# Create staging directory with standard macOS plugin layout
STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

mkdir -p "$STAGING/Library/Audio/Plug-Ins/Components"
mkdir -p "$STAGING/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGING/Library/Audio/Plug-Ins/CLAP"

cp -R "$AU_SRC"   "$STAGING/Library/Audio/Plug-Ins/Components/SPU-94.component"
cp -R "$VST3_SRC" "$STAGING/Library/Audio/Plug-Ins/VST3/SPU-94.vst3"
cp    "$CLAP_SRC" "$STAGING/Library/Audio/Plug-Ins/CLAP/SPU-94.clap"

# Build the .pkg
pkgbuild \
    --root "$STAGING" \
    --identifier "com.spu94project.spu94.pkg" \
    --version "$VERSION" \
    --install-location "/" \
    "SPU-94.pkg"

echo "Created SPU-94.pkg (version $VERSION)"
