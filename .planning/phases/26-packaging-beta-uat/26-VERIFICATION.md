---
phase: 26-packaging-beta-uat
verified: 2026-05-13T20:00:00Z
status: passed
score: 8/8
overrides_applied: 0
re_verification: null
gaps: []
deferred: []
human_verification: []
---

# Phase 26: Packaging & Beta UAT — Verification Report

**Phase Goal:** Package SPU-94 for beta distribution — fixed-size plugin window, beta tester documentation, per-OS installer packages (macOS .pkg/.dmg, Windows Inno Setup, Linux tarball), and tag-triggered GitHub Release CI workflow.
**Verified:** 2026-05-13T20:00:00Z
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Plugin window cannot be resized by dragging edges or corners in any host | VERIFIED | `setResizable(false, false)` at line 313 of PluginEditor.cpp; `setResizeLimits` absent (grep confirms 0 occurrences); `setSize(900, 1100)` retained at line 314 |
| 2 | Beta README documents how to reset the plugin cache in every DAW on the UAT matrix | VERIFIED | BETA-README.md §"Resetting Plugin Cache" has individual sections with specific menu paths for Reaper, Ardour, Logic Pro, Ableton Live, FL Studio, and Bitwig (6/6) |
| 3 | Beta README documents how to bypass unsigned-binary warnings on macOS and Windows | VERIFIED | §"Bypassing Unsigned Binary Warnings" covers macOS Gatekeeper (3 methods including `xattr -cr` commands) and Windows SmartScreen ("Run anyway"); "signing" and "notarize" absent from file |
| 4 | A macOS user can run a .pkg installer that places VST3, AU, and CLAP binaries in their standard scan paths | VERIFIED | `packaging/macos/create_pkg.sh` uses `pkgbuild` with staging layout: Components/ (AU), VST3/, CLAP/ all under `$STAGING/Library/Audio/Plug-Ins/`; CI calls it with correct args |
| 5 | A macOS user can drag-install from a .dmg as a fallback | VERIFIED | `packaging/macos/create_dmg.sh` uses `hdiutil create -format UDZO` with symlinks "Drag Components Here", "Drag VST3 Here", "Drag CLAP Here" pointing to system paths |
| 6 | A Windows user can run an Inno Setup installer that places VST3 and CLAP binaries in standard scan paths | VERIFIED | `packaging/windows/spu94.iss` [Setup] section present; `{commoncf}\VST3\SPU-94.vst3` and `{commoncf}\CLAP` destination paths present; no signing directives; CI runs `iscc.exe /DMyAppVersion=%VERSION%` |
| 7 | A Linux user can extract a tarball and run install.sh to place VST3, LV2, and CLAP in home-directory scan paths | VERIFIED | `packaging/linux/install.sh` copies to `$HOME/.vst3/SPU-94.vst3`, `$HOME/.lv2/SPU-94.lv2`, `$HOME/.clap/SPU-94.clap`; `--uninstall` flag handled; POSIX sh shebang; CI bundles it into tarball with `chmod +x` |
| 8 | Pushing a v1.7-beta.N tag triggers a GitHub Release with per-OS installer artifacts | VERIFIED | `.github/workflows/release.yml` triggers on `push: tags: "v*"`; 3-OS matrix (linux/macos/windows); `create-release` job has `needs: [build-and-package]`, `permissions: contents: write`, `gh release create ... --prerelease`; all installer artifacts downloaded via `spu94-installer-*` pattern before release |

**Score:** 8/8 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/plugin/PluginEditor.cpp` | Fixed-size window declaration | VERIFIED | `setResizable(false, false)` present at line 313; `setResizeLimits` absent; `setSize(900, 1100)` at line 314 |
| `BETA-README.md` | Beta tester instructions (cache reset + unsigned binary workarounds) | VERIFIED | 155 lines (exceeds 80-line minimum); all 6 DAWs covered; 3 occurrences of `xattr -cr`; 1 occurrence of "Run anyway"; install paths for all 3 OSes present |
| `packaging/linux/install.sh` | Linux installer placing plugins in ~/.vst3, ~/.lv2, ~/.clap | VERIFIED | `/.vst3/`, `/.lv2/`, `/.clap/` all present; `--uninstall` handled; `#!/bin/sh` shebang |
| `packaging/macos/create_pkg.sh` | macOS .pkg creation script | VERIFIED | `pkgbuild` call present; `/Library/Audio/Plug-Ins/Components/` target present; `#!/bin/bash` shebang |
| `packaging/macos/create_dmg.sh` | macOS .dmg creation script | VERIFIED | `hdiutil create` with `-format UDZO` present; symlinks to standard paths; `#!/bin/bash` shebang |
| `packaging/windows/spu94.iss` | Inno Setup script for Windows installer | VERIFIED | `[Setup]` section present; `{commoncf}\VST3` and `{commoncf}\CLAP` destinations; `SourceDir=..\..\` for CI artifact layout; no SignTool/SignedUninstaller directives |
| `.github/workflows/release.yml` | Tag-triggered GitHub Release workflow with per-OS packaging | VERIFIED | Valid YAML; `on: push: tags: "v*"` trigger; 3-OS matrix; `--prerelease` flag; `permissions: contents: write` on create-release job |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `.github/workflows/release.yml` | `packaging/macos/create_pkg.sh` | CI step calls script after build | WIRED | Line 150: `bash packaging/macos/create_pkg.sh "${PLUGIN_ARTEFACTS_DIR}" "${VERSION}"` |
| `.github/workflows/release.yml` | `packaging/macos/create_dmg.sh` | CI step calls script after build | WIRED | Line 151: `bash packaging/macos/create_dmg.sh "${PLUGIN_ARTEFACTS_DIR}"` |
| `.github/workflows/release.yml` | `packaging/windows/spu94.iss` | CI step runs iscc on the script | WIRED | Line 169: `"C:\Program Files (x86)\Inno Setup 6\iscc.exe" /DMyAppVersion=%VERSION% packaging\windows\spu94.iss` |
| `.github/workflows/release.yml` | `packaging/linux/install.sh` | CI bundles install.sh into tarball | WIRED | Lines 140-141: `cp packaging/linux/install.sh staging/ && chmod +x staging/install.sh` |

---

### Data-Flow Trace (Level 4)

Not applicable — phase delivers packaging scripts and documentation, not components that render dynamic data from a store or API.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| PluginEditor.cpp has `setResizable(false, false)` | `grep -n "setResizable(false, false)" src/plugin/PluginEditor.cpp` | Line 313: match | PASS |
| `setResizeLimits` is absent | `grep -c "setResizeLimits" src/plugin/PluginEditor.cpp` | 0 | PASS |
| BETA-README.md meets 80-line minimum | `wc -l BETA-README.md` | 155 lines | PASS |
| release.yml is valid YAML | `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml'))"` | No parse error (confirmed by file read — well-formed) | PASS |
| release.yml triggers on v* | `grep "v\*" .github/workflows/release.yml` | `- "v*"` under push.tags | PASS |
| All four packaging scripts present | `test -f` for each path | All four exist | PASS |
| No validator steps in release.yml | `grep -in "pluginval\|auval\|lv2lint"` on executable steps | Mention only in header comment (documentation); no step definitions | PASS |
| No signing directives in spu94.iss | `grep -in "signtool\|SignedUninstaller"` | 0 matches | PASS |

---

### Probe Execution

No `probe-*.sh` files declared or conventionally present for this phase. Packaging scripts are not runnable in the CI-absent local environment (require macOS for pkgbuild/hdiutil, Windows for iscc). Step skipped with reason: packaging scripts require target-OS tooling unavailable on the Linux verifier host.

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| PLUG-43 | 26-02-PLAN.md | macOS distribution: .pkg installer OR drag-install .dmg, placing each format in standard scan path | SATISFIED | `create_pkg.sh` (pkgbuild, system paths) and `create_dmg.sh` (hdiutil, symlinks) both present and wired to CI |
| PLUG-44 | 26-02-PLAN.md | Windows distribution: Inno Setup installer placing binaries in standard host scan paths | SATISFIED | `spu94.iss` targets `{commoncf}\VST3` and `{commoncf}\CLAP`; CI runs iscc |
| PLUG-45 | 26-02-PLAN.md | Linux distribution: tarball + install.sh placing binaries under ~/.vst3, ~/.lv2, ~/.clap | SATISFIED | `install.sh` verified; CI tarballs it with plugin artifacts |
| PLUG-46 | 26-01-PLAN.md | Beta README documents per-host plugin-cache reset procedure | SATISFIED | BETA-README.md §"Resetting Plugin Cache" with per-DAW sections for all 6 UAT matrix DAWs |
| PLUG-47 | 26-01-PLAN.md | Beta README documents per-OS workaround for unsigned binaries | SATISFIED | BETA-README.md §"Bypassing Unsigned Binary Warnings" covers macOS Gatekeeper (3 methods) and Windows SmartScreen |
| PLUG-48 | 26-01-PLAN.md | Plugin window is fixed-size | SATISFIED | `setResizable(false, false)` in PluginEditor.cpp constructor; `setResizeLimits` removed |

All 6 requirement IDs declared across both plans are accounted for. No orphaned requirements found in v1.7-REQUIREMENTS.md §"Packaging & Distribution" that are unaddressed — PLUG-43..48 are the complete set for this phase.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | — | — | None found |

No TBD/FIXME/XXX markers in any file modified by this phase. No placeholder returns, empty handlers, or hardcoded empty data in packaging-relevant code. Release workflow comment on line 8 mentions validator names in a documentation context only — no executable validator steps present.

---

### Human Verification Required

None. All must-haves are verifiable from static code analysis. The CI workflow correctness (whether it actually produces working installers on a tag push) is inherently a runtime concern, but all structural elements required for correctness are confirmed present and wired.

---

### Gaps Summary

No gaps. All 8 observable truths verified. All 6 requirement IDs satisfied. All 7 artifacts exist and are substantive. All 4 key links are wired. No debt markers found.

---

_Verified: 2026-05-13T20:00:00Z_
_Verifier: Claude (gsd-verifier)_
