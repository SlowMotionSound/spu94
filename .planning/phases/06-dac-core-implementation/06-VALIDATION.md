---
phase: 6
slug: dac-core-implementation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-28
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C unit tests) + pytest (Python bit-identity) + ctest (runner) |
| **Config file** | CMakeLists.txt (test targets auto-registered via add_test) |
| **Quick run command** | `cd build && ctest -R dac --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -R dac --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 06-01-01 | 01 | 1 | DAC-FILT-02 | — | N/A | unit | `ctest -R dac_fir` | ❌ W0 | ⬜ pending |
| 06-01-02 | 01 | 1 | DAC-FILT-02 | — | N/A | integration | `python3 -m pytest tests/python/test_dac_fir_bit_identity.py` | ❌ W0 | ⬜ pending |
| 06-02-01 | 02 | 1 | DAC-NOISE-01 | — | N/A | unit | `ctest -R dac_noise` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/dac_fir/` — Unity test directory for FIR filter tests
- [ ] `tests/unit/dac_noise/` — Unity test directory for noise model tests
- [ ] `tests/python/test_dac_fir_bit_identity.py` — Python bit-identity test stub

*Existing Unity + ctest infrastructure covers framework needs. Only test files are new.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Noise sounds plausible | DAC-NOISE-01 | Perceptual quality | Listen to noise-only output; should sound like shaped hiss, not tonal |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
