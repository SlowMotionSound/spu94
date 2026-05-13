#!/bin/bash
# SPU-94 Phase 26 — macOS .dmg drag-install image creation script
# Creates a .dmg containing all plugin formats with symlinks to their
# standard install destinations. This is the drag-install fallback for
# users who prefer not to run the .pkg installer.
#
# Usage:
#   ./create_dmg.sh <artifacts-dir>
#
# Arguments:
#   artifacts-dir  Path to the JUCE build artifacts directory
#                  (e.g. build/src/plugin/spu94_plugin_artefacts/Release)

set -euo pipefail

ARTIFACTS_DIR="${1:?Usage: $0 <artifacts-dir>}"

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

# Create staging directory
STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

DMG_CONTENT="$STAGING/SPU-94 Beta"
mkdir -p "$DMG_CONTENT"

# Copy plugin artifacts
cp -R "$AU_SRC"   "$DMG_CONTENT/SPU-94.component"
cp -R "$VST3_SRC" "$DMG_CONTENT/SPU-94.vst3"
cp    "$CLAP_SRC" "$DMG_CONTENT/SPU-94.clap"

# Create symlinks to standard install destinations
ln -s "/Library/Audio/Plug-Ins/Components/" "$DMG_CONTENT/Drag Components Here"
ln -s "/Library/Audio/Plug-Ins/VST3/"       "$DMG_CONTENT/Drag VST3 Here"
ln -s "/Library/Audio/Plug-Ins/CLAP/"       "$DMG_CONTENT/Drag CLAP Here"

# Create the .dmg
hdiutil create \
    -volname "SPU-94 Beta" \
    -srcfolder "$DMG_CONTENT" \
    -ov \
    -format UDZO \
    "SPU-94.dmg"

echo "Created SPU-94.dmg"
