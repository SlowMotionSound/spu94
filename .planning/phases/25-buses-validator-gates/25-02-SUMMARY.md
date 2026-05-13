---
phase: 25-buses-validator-gates
plan: 02
subsystem: ci-validation
tags: [pluginval, auval, lv2lint, vst3-validator, ci-gates, strictness-7]
dependency_graph:
  requires: [25-01-bus-layout]
  provides: [pluginval-strictness-7-gate, auval-gate, lv2lint-gate, vst3-sdk-validator-gate]
  affects: [.github/workflows/plugins.yml]
tech_stack:
  added: [lv2lint, sord_validate, steinberg-vst3-validator]
  patterns: [ci-hard-gate, build-from-source-in-ci, sha-pinned-actions, validator-cache]
key_files:
  created: []
  modified:
    - .github/workflows/plugins.yml
decisions:
  - "VST3 SDK validator cache only effective on Linux/macOS; Windows rebuilds each CI run (acceptable tradeoff)"
  - "xvfb merged into main Linux deps apt-get install (was a separate step)"
metrics:
  duration: 4m 17s
  completed: 2026-05-13
  tasks: 2
  files: 1
---

# Phase 25 Plan 02: Validator CI Gates Summary

Promoted pluginval to strictness-7 hard gate on all formats/OSes, added auval (macOS), lv2lint + sord_validate (Linux), and Steinberg VST3 SDK validator (all 3 OSes) as hard CI gates with zero continue-on-error

## Commits

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 0 | Pre-flight pluginval strictness-7 (deferred to CI) | (none) | (diagnostic only) |
| 1 | Promote pluginval + add auval/lv2lint/VST3 validator gates | b6b96fe | .github/workflows/plugins.yml |

## What Was Done

### Task 0: Pre-flight pluginval strictness-7

Deferred to CI. pluginval is not installed on the local development machine and the VST3 plugin binary only exists in the main repo build directory (not accessible from the worktree). Per execution context instructions, this local pre-flight is a best-effort check, not a hard gate -- the CI pipeline installs pluginval from the Tracktion release page and runs it against freshly-built binaries. The Phase 24 parameter thread safety concern (RESEARCH.md Open Question 2) will be empirically verified when the CI pipeline runs pluginval at strictness-7 for the first time.

### Task 1: Validator CI gates in plugins.yml

**A) Promoted pluginval to strictness-7 (PLUG-37, PLUG-41)**
- Replaced three "Smoke (Linux/macOS/Windows plugin formats)" steps with "Validate (Linux/macOS/Windows, pluginval strictness-7)" steps
- Changed `--strictness-level 1` to `--strictness-level 7` with `--timeout-ms 120000` on all invocations
- Linux pluginval invocations wrapped with `xvfb-run -a` for GUI test support
- Per-format coverage retained: Linux=VST3+LV2+CLAP, macOS=VST3+AU+CLAP, Windows=VST3+CLAP

**B) Added auval gate (PLUG-38)**
- New step "Validate AU (auval)" with `if: matrix.name == 'macos'`
- Command: `auval -v aufx Sp94 Spu9` (matches PLUGIN_CODE=Sp94, PLUGIN_MANUFACTURER_CODE=Spu9)
- No continue-on-error (PLUG-42)

**C) Added lv2lint + sord_validate gate (PLUG-39)**
- New step "Build lv2lint from source (Linux)" -- clones from gitlab.com/drobilla/lv2lint, builds with meson/ninja
- New step "Validate LV2 (lv2lint + sord_validate)" -- runs `sord_validate` on TTL files, then `lv2lint -I ... -M nopack` against the LV2 bundle URI
- Added `meson liblilv-dev sordi` to the "Install Linux deps" apt-get install
- No continue-on-error (PLUG-42)

**D) Added Steinberg VST3 SDK validator (PLUG-40)**
- New step "Cache VST3 SDK validator" -- SHA-pinned actions/cache, key `${{ runner.os }}-vst3-validator-v1`
- New step "Build VST3 SDK validator (Linux/macOS)" -- clones steinbergmedia/vst3sdk, builds `validator` CMake target with Ninja, skips if cached
- New step "Build VST3 SDK validator (Windows)" -- same but with vcvars64 + cmd shell
- New step "Validate VST3 (Steinberg SDK validator, Linux/macOS)" -- finds validator binary, runs against SPU-94.vst3
- New step "Validate VST3 (Steinberg SDK validator, Windows)" -- PowerShell variant with Get-ChildItem + exit code check
- No continue-on-error (PLUG-42)

**E) Removed advisory pluginval-early-warning job**
- Entire `pluginval-early-warning` job block (was lines 252-285) removed -- strictness-7 is now a hard gate in the main build job

**F) Zero continue-on-error (PLUG-42)**
- Verified: zero occurrences of `continue-on-error` in the entire workflow file
- Any validator warning or error fails the CI build

**G) Standalone smoke steps unchanged**
- "Smoke (Linux/macOS standalone)" and "Smoke (Windows standalone)" steps preserved exactly

**Infrastructure cleanup:**
- Merged standalone xvfb install step into main Linux deps (xvfb is needed by both pluginval and standalone smoke)
- Added `xvfb` to the "Install Linux deps" apt-get install list

## Verification Results

- YAML syntax valid (python3 yaml.safe_load)
- 8 occurrences of `strictness-level 7` (3 formats on Linux + 3 on macOS + 2 on Windows)
- 0 occurrences of `strictness-level 1`
- 4 occurrences of `auval` (comment header, step name, PLUG ref, command)
- 7 occurrences of `lv2lint` (comment, steps, command, install)
- 0 occurrences of `continue-on-error`
- `pluginval-early-warning` only appears in comment header (documenting removal)
- 2 occurrences of `steinbergmedia/vst3sdk` (Linux/macOS and Windows build steps)
- 2 occurrences of `target validator` (Linux/macOS and Windows build steps)
- All third-party actions SHA-pinned (checkout, cache, upload-artifact)
- `auval -v aufx Sp94 Spu9` present in macOS-only step
- `sord_validate` and `lv2lint` present in Linux-only steps
- `meson liblilv-dev sordi` added to Linux deps apt-get install

## Deviations from Plan

### Deferred Items

**1. [Deferred] Task 0 pre-flight pluginval strictness-7**
- **Reason:** pluginval not installed locally; VST3 binary not available in worktree
- **Impact:** Phase 24 parameter thread safety (Open Question 2) will be verified when CI runs for the first time
- **Mitigation:** The CI hard gate itself serves as the verification mechanism

### Auto-fixed Issues

**1. [Rule 3 - Blocking] xvfb install step ordering**
- **Found during:** Task 1
- **Issue:** The original workflow had "Install xvfb" as a separate step placed *after* the standalone smoke that used it (step ordering bug). With pluginval now also needing xvfb on Linux, the separate step was both misplaced and insufficient.
- **Fix:** Merged xvfb into the main "Install Linux deps" apt-get install, which runs before all validation and smoke steps
- **Files modified:** .github/workflows/plugins.yml

## Known Stubs

None.

## Threat Flags

None. All changes are CI configuration only. No new network endpoints, auth paths, or schema changes introduced to the plugin source code.

## Self-Check: PASSED

- .github/workflows/plugins.yml exists
- 25-02-SUMMARY.md exists
- Commit b6b96fe present in git log
- YAML syntax valid
- All acceptance criteria verified (strictness-7 counts, zero strictness-1, auval, lv2lint, sord_validate, no continue-on-error, advisory job removed, VST3 SDK validator, SHA-pinned actions)
