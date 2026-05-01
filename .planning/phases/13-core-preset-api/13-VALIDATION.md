---
phase: 13
slug: core-preset-api
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-01
---

# Phase 13 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (vendored) |
| **Config file** | `tests/unit/preset/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build -L preset -j4` |
| **Full suite command** | `ctest --test-dir build -j$(nproc)` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -L preset -j4`
- **After every plan wave:** Run `ctest --test-dir build -j$(nproc)`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 13-01-01 | 01 | 1 | PRE-01 | — | N/A | unit | `ctest --test-dir build -R preset_save_all_fields -j1` | ❌ W0 | ⬜ pending |
| 13-01-02 | 01 | 1 | PRE-02 | — | N/A | unit | `ctest --test-dir build -R preset_load_all_fields -j1` | ❌ W0 | ⬜ pending |
| 13-01-03 | 01 | 1 | PRE-03 | — | N/A | unit | `ctest --test-dir build -R preset_version_header -j1` | ❌ W0 | ⬜ pending |
| 13-01-04 | 01 | 1 | PRE-04 | — | N/A | unit | `ctest --test-dir build -R preset_roundtrip -j1` | ❌ W0 | ⬜ pending |
| 13-01-05 | 01 | 1 | PRE-05 | — | N/A | unit | `ctest --test-dir build -R preset_format_check -j1` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/preset/test_preset_roundtrip.c` — covers PRE-04 (round-trip fidelity), also exercises PRE-01 through PRE-03
- [ ] `tests/unit/preset/test_preset_parse.c` — covers PRE-02 edge cases (missing keys/D-08, unknown keys/D-09, comments, blank lines, malformed values)
- [ ] `tests/unit/preset/CMakeLists.txt` — add new test targets (existing file needs 2 new `add_executable` + `add_test` blocks)

*Existing infrastructure covers test framework — Unity is already vendored.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Human-readable format | PRE-05 | Readability is subjective | Open saved .spu94 file in text editor, verify section headers and hex values are clear |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
