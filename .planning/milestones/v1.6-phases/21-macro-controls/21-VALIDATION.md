---
phase: 21
slug: macro-controls
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-04
---

# Phase 21 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | ctest + Unity (C unit test framework) |
| **Config file** | CMakeLists.txt (test targets already configured) |
| **Quick run command** | `cd build_test && ctest --output-on-failure -R macro` |
| **Full suite command** | `cd build_test && cmake --build . && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build_test && ctest --output-on-failure -R macro`
- **After every plan wave:** Run `cd build_test && cmake --build . && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | WALL-01..06 | — | N/A | unit | `ctest -R macro_walls` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | ECHO-SPD-01 | — | N/A | unit | `ctest -R macro_echo` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | TAP-01..03 | — | N/A | unit | `ctest -R macro_taps` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | DIFF-AMT-01, DIFF-TEX-01, DIFF-POS-01..02 | — | N/A | unit | `ctest -R macro_diffusion` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | ROOM-01 | — | N/A | unit | `ctest -R macro_room` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUF-01..02 | — | N/A | unit | `ctest -R macro_buffer` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | SS-01..03 | — | N/A | unit | `ctest -R spread_sweep` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CTRL-01..03 | — | N/A | unit | `ctest -R macro_controls` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | SAFE-03, SAFE-04 | — | vIIR floor enforced | unit | `ctest -R macro_safety` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | PRESET-01 | — | N/A | unit | `ctest -R macro_preset` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_spu94_macro_controls.c` — stubs for all macro control group tests
- [ ] `tests/test_spu94_spread_sweep.c` — stubs for Spread+Sweep model tests
- [ ] `tests/test_spu94_bipolar.c` — stubs for bipolar apply path tests

*Existing test infrastructure (CMake, Unity framework) covers framework needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Preset load derives all macros | PRESET-01 | Requires factory preset values + full macro surface | Load each factory preset, verify no register values change, verify all knob positions non-zero |

*All other phase behaviors have automated verification.*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
