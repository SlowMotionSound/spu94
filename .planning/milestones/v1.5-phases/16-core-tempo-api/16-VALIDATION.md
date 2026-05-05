---
phase: 16
slug: core-tempo-api
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-02
---

# Phase 16 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (vendored, C) |
| **Config file** | tests/unit/CMakeLists.txt |
| **Quick run command** | `cd build && ctest -R tempo -j$(nproc)` |
| **Full suite command** | `cd build && ctest -j$(nproc)` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -R tempo -j$(nproc)`
- **After every plan wave:** Run `cd build && ctest -j$(nproc)`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| TBD | 01 | 1 | TEMPO-01 | unit | `ctest -R test_tempo_basic` | Wave 0 | pending |
| TBD | 01 | 1 | TEMPO-02 | unit | `ctest -R test_tempo_snap` | Wave 0 | pending |
| TBD | 01 | 1 | TEMPO-03 | unit | `ctest -R test_tempo_snap` | Wave 0 | pending |
| TBD | 01 | 1 | TEMPO-04 | unit | `ctest -R test_tempo_snap` | Wave 0 | pending |

*Status: pending · green · red · flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/tempo/CMakeLists.txt` — build config for tempo tests
- [ ] `tests/unit/tempo/test_tempo_basic.c` — TEMPO-01 coverage (set/get BPM)
- [ ] `tests/unit/tempo/test_tempo_snap.c` — TEMPO-02, TEMPO-03, TEMPO-04 (subdivision snapping)
- [ ] `tests/unit/tempo/test_tempo_comb.c` — virtual comb computation correctness
- [ ] `tests/unit/tempo/test_tempo_binding.c` — binding state transitions (D-04 through D-07)
