---
phase: 26-packaging-beta-uat
plan: 02
subsystem: packaging
tags: [installer, ci, release, linux, macos, windows]
dependency_graph:
  requires: []
  provides: [per-os-installers, release-workflow]
  affects: [beta-distribution]
tech_stack:
  added: [inno-setup, pkgbuild, hdiutil]
  patterns: [sha-pinned-actions, per-os-matrix, tag-triggered-release]
key_files:
  created:
    - packaging/linux/install.sh
    - packaging/macos/create_pkg.sh
    - packaging/macos/create_dmg.sh
    - packaging/windows/spu94.iss
    - .github/workflows/release.yml
  modified: []
decisions:
  - "AU goes to /Library/Audio/Plug-Ins/Components/ (system path, admin required) per PITFALLS B4"
  - "Linux install.sh uses POSIX sh (not bash) for portability"
  - "Inno Setup installer disables dir page -- plugins go to fixed standard paths only"
  - "Release workflow is separate from plugins.yml -- no validators (validation runs on push/PR)"
  - "All beta releases are marked --prerelease in GitHub Releases"
metrics:
  duration: "3m 22s"
  completed: "2026-05-13"
  tasks: 2
  files: 5
---

# Phase 26 Plan 02: Per-OS Installer Packages & Release Workflow Summary

Per-OS packaging scripts for Linux (tarball + install.sh), macOS (.pkg + .dmg), and Windows (Inno Setup .exe), plus a tag-triggered GitHub Release workflow that builds, packages, and publishes all three as pre-release assets on v* tag push.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Create per-OS packaging scripts | 7133c72 | packaging/linux/install.sh, packaging/macos/create_pkg.sh, packaging/macos/create_dmg.sh, packaging/windows/spu94.iss |
| 2 | Create tag-triggered release workflow | 5c8354f | .github/workflows/release.yml |

## What Was Built

### Task 1: Per-OS Packaging Scripts

**Linux (install.sh):** POSIX sh script that copies VST3/LV2/CLAP plugins to `~/.vst3/`, `~/.lv2/`, `~/.clap/` respectively. Supports `--uninstall` flag to remove. Expects to run from extracted tarball root where artifacts are siblings. No sudo required -- all paths are in user home.

**macOS (create_pkg.sh):** Builds a flat .pkg via `pkgbuild` that installs AU to `/Library/Audio/Plug-Ins/Components/`, VST3 to `/Library/Audio/Plug-Ins/VST3/`, and CLAP to `/Library/Audio/Plug-Ins/CLAP/`. Uses system paths (not user paths) per PITFALLS B4 -- AU on macOS is unreliable at the user path. Requires admin password at install time.

**macOS (create_dmg.sh):** Creates a drag-install .dmg with the three plugin formats plus symlinks ("Drag Components Here", "Drag VST3 Here", "Drag CLAP Here") pointing to standard install destinations. Compressed UDZO format.

**Windows (spu94.iss):** Inno Setup 6 script installing VST3 bundle to `{commoncf}\VST3\SPU-94.vst3` and CLAP binary to `{commoncf}\CLAP\SPU-94.clap`. Directory page disabled -- plugins go to fixed standard paths. LZMA2 solid compression. No code signing (deferred per project decision). Clean install/uninstall entries.

### Task 2: Release Workflow

**`.github/workflows/release.yml`:** Triggers on `v*` tag pushes. Separate from `plugins.yml` (which handles validation on push/PR). Two jobs:

1. **build-and-package** (3-OS matrix): Checkout, install deps, configure, build (all steps copied from plugins.yml with identical SHA-pinned actions). Then per-OS packaging: Linux creates `.tar.gz` with install.sh + artifacts; macOS calls both create_pkg.sh and create_dmg.sh; Windows installs Inno Setup via choco and runs iscc. Each OS uploads its installer artifacts.

2. **create-release** (depends on build-and-package): Downloads all installer artifacts, creates a GitHub Release via `gh release create` with `--prerelease` flag and `--generate-notes`. All installer files attached as release assets.

SHA pins match plugins.yml exactly. No validator steps -- validation runs on every push via plugins.yml; tag pushes to main will have already passed.

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

All acceptance criteria verified:
- All four packaging scripts exist with correct content and shebangs
- install.sh contains `/.vst3/`, `/.lv2/`, `/.clap/`, and `--uninstall` handling
- create_pkg.sh contains `pkgbuild` and `/Library/Audio/Plug-Ins/Components/`
- create_dmg.sh contains `hdiutil` and `UDZO`
- spu94.iss contains `[Setup]`, `{commoncf}\VST3`, `{commoncf}\CLAP`, no signing directives
- release.yml is valid YAML, triggers on v* tags only
- All SHA pins match plugins.yml
- No validator steps (only in header comments)
- create-release job has `permissions: contents: write`
- No code signing steps

## Self-Check: PASSED

All 5 created files verified present. Both task commits (7133c72, 5c8354f) verified in git log.
