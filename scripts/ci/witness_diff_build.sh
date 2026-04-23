#!/usr/bin/env bash
# scripts/ci/witness_diff_build.sh — build lv2-psx-reverb fresh at pinned SHA (D-05).
#
# D-08 discipline: lv2 is a behavioral witness binary only; source is NOT
# read as primary material. This script compiles the bundle and confirms the
# exposed URIs/ports via lv2info. The witness-diff Python harness reads ONLY
# the bundle's produced audio output, never the .c/.h sources.
#
# Pin provenance: RESEARCH.md verified 2026-04-22 that commit
# 424e1e8ee7f780106b005011b036386513c61db3 is master tip (no release tags).
# Re-verified at plan 07-03 execute time 2026-04-23 via
#   git ls-remote https://github.com/ipatix/lv2-psx-reverb HEAD
# returned the same SHA — no bump needed.
#
# Supply-chain gate (T-07-03-A): post-checkout we verify
#   git rev-parse HEAD == $LV2_COMMIT
# and fail loudly on mismatch (e.g., ref rewrite between clone and checkout).
#
# Output contract:
#   - Writes $WORK/.LV2_PATH with absolute path to the installed bundle dir.
#   - Prints 'PASS: lv2-psx-reverb built at <SHA>; LV2_PATH emitted to ...'
#     as the final stdout line on success.
#   - Exit 1 on clone/build/verify failure with 'FAIL: ...' on stderr.

set -euo pipefail

LV2_COMMIT="${LV2_COMMIT:-424e1e8ee7f780106b005011b036386513c61db3}"
LV2_REPO="${LV2_REPO:-https://github.com/ipatix/lv2-psx-reverb}"
WORK="${WITNESS_WORK:-$(pwd)/.artifacts/lv2-psx-reverb}"

rm -rf "$WORK"
mkdir -p "$(dirname "$WORK")"
mkdir -p "$WORK"

echo "Cloning $LV2_REPO at $LV2_COMMIT..."
# Clone without recursing submodules first — the .gitmodules entry for
# `waflib` points at a git@ ssh URL (gitlab.com:drobilla/autowaf.git) which
# won't resolve anonymously. We rewrite it to https below, then fetch it.
git clone --quiet "$LV2_REPO" "$WORK/src"
git -C "$WORK/src" checkout --quiet "$LV2_COMMIT"

# Supply-chain gate: verify the commit we're on matches the pin exactly.
ACTUAL=$(git -C "$WORK/src" rev-parse HEAD)
if [ "$ACTUAL" != "$LV2_COMMIT" ]; then
    echo "FAIL: expected commit $LV2_COMMIT, got $ACTUAL" >&2
    exit 1
fi

# The upstream waflib/ submodule is configured via ssh (git@gitlab.com:...),
# which does not work in unauthenticated CI. Rewrite to https then fetch.
# If submodule init fails entirely (gitlab.com reachability, etc.), fall back
# to the waflib already checked into the repo at the pinned commit.
if [ -f "$WORK/src/.gitmodules" ]; then
    git -C "$WORK/src" config submodule.waflib.url \
        "https://gitlab.com/drobilla/autowaf.git" || true
    git -C "$WORK/src" submodule update --init --recursive 2>&1 | tail -5 || \
        echo "NOTE: submodule init failed; using in-tree waflib if present"
fi

echo "Building LV2 bundle via waf..."
# The repo's build.sh hardcodes /tmp/lv2test; we want an isolated prefix
# under $WORK so concurrent CI jobs can't race on /tmp. Invoke waf directly.
#
# waf's shebang is `#!/usr/bin/env python` (no 3). On Debian/Ubuntu since
# bookworm, `python` is not installed by default — only `python3`. Invoke
# waf explicitly via python3 to avoid a /usr/bin/env lookup failure.
PY3="${PYTHON3:-python3}"
if ! command -v "$PY3" >/dev/null 2>&1; then
    echo "FAIL: python3 is required to run waf but was not found on PATH" >&2
    exit 1
fi
pushd "$WORK/src" >/dev/null
mkdir -p "$WORK/prefix"
"$PY3" ./waf configure --prefix="$WORK/prefix" 2>&1 | tail -20
"$PY3" ./waf build 2>&1 | tail -20
"$PY3" ./waf install 2>&1 | tail -10
popd >/dev/null

# Locate the installed .lv2 bundle. waf install places it under
# $prefix/lib/lv2/ on most layouts.
mkdir -p "$WORK/lv2"
BUNDLE=$(find "$WORK/prefix" -maxdepth 5 -name "*.lv2" -type d | head -1)
if [ -z "$BUNDLE" ]; then
    # Fall back to the build-dir bundle (waf puts an unfinalized copy in
    # $WORK/src/build/lv2/psx-reverb.lv2 during `build` stage).
    BUNDLE=$(find "$WORK/src/build" -maxdepth 4 -name "*.lv2" -type d | head -1)
fi
if [ -z "$BUNDLE" ]; then
    echo "FAIL: no .lv2 bundle dir found after waf install" >&2
    exit 1
fi
cp -r "$BUNDLE" "$WORK/lv2/"
INSTALLED_BUNDLE="$WORK/lv2/$(basename "$BUNDLE")"

echo ""
echo "=== lv2info discovery (D-08: binary output only, not source read) ==="
# lv2info takes a plugin URI, not a bundle path. Discover the plugin URI from
# manifest.ttl (a line shaped `<URI> a lv2:Plugin`), then query via lv2info.
# The URI is also emitted to $WORK/.LV2_URI for witness_diff.py's consumption.
LV2INFO_OUT="$WORK/lv2info.txt"
# Extract the plugin URI = the last <https?://...> subject seen on or before
# a line containing `a lv2:Plugin`. Skips Turtle prefix declarations (which
# point at ontology URIs, not the plugin itself) and comment lines.
URI=$(awk '
    /^\s*#/ { next }
    /^[[:space:]]*@prefix/ { next }
    match($0, /<https?:\/\/[^>]+>/) {
        u = substr($0, RSTART+1, RLENGTH-2)
    }
    /a[[:space:]]+lv2:Plugin/ {
        print u; exit
    }
' "$INSTALLED_BUNDLE/manifest.ttl")
if [ -z "$URI" ]; then
    echo "FAIL: could not extract plugin URI from manifest.ttl" >&2
    exit 1
fi
echo "Discovered plugin URI: $URI"

if LV2_PATH="$WORK/lv2" lv2info "$URI" > "$LV2INFO_OUT" 2>&1; then
    head -120 "$LV2INFO_OUT" || true
else
    echo "FAIL: lv2info failed for URI '$URI' (LV2_PATH=$WORK/lv2)" >&2
    cat "$LV2INFO_OUT" >&2 || true
    exit 1
fi

# Emit the discovered LV2_PATH + URI for the caller (witness_diff.py reads them)
echo "$WORK/lv2" > "$WORK/.LV2_PATH"
echo "$URI" > "$WORK/.LV2_URI"

echo ""
echo "PASS: lv2-psx-reverb built at $LV2_COMMIT; LV2_PATH emitted to $WORK/.LV2_PATH"
