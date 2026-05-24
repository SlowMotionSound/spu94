---
phase: 36
slug: noise-generator-non
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-22
---

# Phase 36 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake/CTest + custom C test harness |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `cd build && ctest --output-on-failure -R noise` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest --output-on-failure -R noise`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 36-01-01 | 01 | 1 | NON-01 | — | N/A | unit | `ctest -R noise_lfsr` | ❌ W0 | ⬜ pending |
| 36-01-02 | 01 | 1 | NON-02 | — | N/A | unit | `ctest -R noise_timer` | ❌ W0 | ⬜ pending |
| 36-01-03 | 01 | 1 | NON-03 | — | N/A | unit | `ctest -R noise_freq` | ❌ W0 | ⬜ pending |
| 36-01-04 | 01 | 1 | NON-04 | — | N/A | unit | `ctest -R noise_bitmask` | ❌ W0 | ⬜ pending |
| 36-01-05 | 01 | 1 | NON-05 | — | N/A | unit | `ctest -R noise_shared` | ❌ W0 | ⬜ pending |
| 36-01-06 | 01 | 1 | NON-06 | — | N/A | unit | `ctest -R noise_adpcm` | ❌ W0 | ⬜ pending |
| 36-01-07 | 01 | 1 | NON-07 | — | N/A | unit | `ctest -R noise_adsr` | ❌ W0 | ⬜ pending |
| 36-01-08 | 01 | 1 | NON-08 | — | N/A | unit | `ctest -R noise_tick` | ❌ W0 | ⬜ pending |
| 36-02-01 | 02 | 2 | NON-09 | — | N/A | doc | manual review | — | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_noise.c` — stubs for NON-01..08
- [ ] Test helpers for LFSR verification and noise timer assertions

*Existing test infrastructure (CMake/CTest) covers framework needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| ADR content review | NON-09 | Documentation quality | Review ADR-0058 for completeness of LFSR polynomial, seed, and ADPCM-fetch decision |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
