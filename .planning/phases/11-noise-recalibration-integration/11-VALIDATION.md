---
phase: 11
slug: noise-recalibration-integration
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-30
---

# Phase 11 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C test framework) + ctest |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `cd build && ctest -L dac --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -L dac --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 11-01-01 | 01 | 1 | DSP-05 | — | N/A | unit | `ctest -R dac_noise --output-on-failure` | ✅ | ⬜ pending |
| 11-02-01 | 02 | 1 | CMP-01 | — | N/A | unit | `ctest -R dac --output-on-failure` | ❌ W0 | ⬜ pending |
| 11-03-01 | 03 | 2 | DSP-07 | — | N/A | unit | `ctest -R latency --output-on-failure` | ❌ W0 | ⬜ pending |
| 11-04-01 | 04 | 2 | INT-01 | — | N/A | integration | `ctest --output-on-failure` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_dac_mode_toggle.c` — stubs for CMP-01 A/B mode toggle
- [ ] `tests/test_latency.c` — stubs for DSP-07 group delay reporting

*Existing dac_noise and integration test infrastructure covers DSP-05 and INT-01.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Gain compensation sounds correct | D-05 | Human listen gate | Play test signal through v1.3 DAC path, compare with v1.2 by ear |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
