#!/usr/bin/env bash
# scripts/ci/install-phase7-deps.sh — Phase 7 (TEST-01, BUILD-08) host toolchain.
#
# Idempotent installer for the Phase 7 verification harness toolchain:
#   - lilv-utils     — provides `lv2apply`, the LV2 host used by the
#                      witness-diff harness to render lv2-psx-reverb output.
#   - lv2-dev        — headers needed to build lv2-psx-reverb fresh at a
#                      pinned commit during the witness-diff CI step.
#   - python3-scipy  — Butterworth band-split, cross-correlation alignment,
#                      chirp generation for the standard input set.
#   - python3-pytest-benchmark
#                    — timing harness for `spu94_process` (report-only gate).
#   - strace         — hot-path allocation gate (filters brk/mmap/munmap/mremap
#                      in the spu94_process call tree).
#   - coreutils      — `sha256sum` for golden-file sidecars (usually present,
#                      kept explicit for container reproducibility parity).
#   - git            — witness-diff clones lv2-psx-reverb at a pinned SHA.
#
# D-13 target: the host dev workstation is the first (and, in Phase 7, only)
# environment this script provisions. The pinned Debian Docker image handles
# its own apt-based provisioning separately (D-14).
#
# Fallback (package-name drift on a future distro): if any `apt-get install`
# target is renamed, fall back to pip for scipy + pytest-benchmark:
#
#     python3 -m pip install --user "scipy>=1.11" "pytest-benchmark>=5.2"
#
# Document the fallback reason in STATE.md when invoked, because an ADR bump
# to the D-14 pinned image digest may be the right next step.
#
# Exit code 0 on success (prints `PASS: Phase 7 host toolchain installed`);
# exit code 1 on any verify-step failure with `FAIL: <tool> not available
# after install` on stderr.

set -euo pipefail

echo "--> apt-get update"
sudo apt-get update

echo "--> apt-get install Phase 7 deps"
sudo apt-get install -y \
    lilv-utils \
    lv2-dev \
    python3-scipy \
    python3-pytest-benchmark \
    strace \
    coreutils \
    git

echo "--> Verifying tool availability"

if ! command -v lv2apply >/dev/null 2>&1; then
    echo "FAIL: lv2apply not available after install" >&2
    exit 1
fi
echo "  lv2apply: $(command -v lv2apply)"

if ! python3 -c "import scipy.signal; import scipy; print('  scipy: ' + scipy.__version__)"; then
    echo "FAIL: python3-scipy not available after install" >&2
    exit 1
fi

if ! python3 -c "import pytest_benchmark; print('  pytest_benchmark: ' + pytest_benchmark.__version__)"; then
    echo "FAIL: python3-pytest-benchmark not available after install" >&2
    exit 1
fi

if ! command -v strace >/dev/null 2>&1; then
    echo "FAIL: strace not available after install" >&2
    exit 1
fi
echo "  strace: $(command -v strace)"

if ! command -v sha256sum >/dev/null 2>&1; then
    echo "FAIL: sha256sum not available after install" >&2
    exit 1
fi
echo "  sha256sum: $(command -v sha256sum)"

echo "PASS: Phase 7 host toolchain installed"
