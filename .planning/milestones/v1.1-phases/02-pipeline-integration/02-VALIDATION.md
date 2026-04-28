---
phase: 2
slug: pipeline-integration
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-26
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | ctest (CMake) |
| **Config file** | CMakeLists.txt |
| **Quick run command** | `cd build && ctest --output-on-failure -j$(nproc)` |
| **Full suite command** | `cd build && cmake --build . && ctest --output-on-failure -j$(nproc)` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest --output-on-failure -j$(nproc)`
- **After every plan wave:** Run `cd build && cmake --build . && ctest --output-on-failure -j$(nproc)`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 01 | 1 | ADPCM-INT-01 | — | N/A | integration | `ctest -R adpcm_pipeline` | ❌ W0 | ⬜ pending |
| 02-01-02 | 01 | 1 | ADPCM-INT-02 | — | N/A | unit | `ctest -R adpcm_buffer` | ❌ W0 | ⬜ pending |
| 02-01-03 | 01 | 1 | ADPCM-INT-04 | — | N/A | unit | `ctest -R adpcm_reset` | ❌ W0 | ⬜ pending |
| 02-01-04 | 01 | 1 | ADPCM-INT-05 | — | N/A | regression | `ctest --output-on-failure` | ✅ | ⬜ pending |
| 02-02-01 | 02 | 2 | ADPCM-INT-03 | — | N/A | unit | `ctest -R adpcm_latency` | ❌ W0 | ⬜ pending |
| 02-02-02 | 02 | 2 | ADPCM-INT-06 | — | N/A | integration | `ctest -R rt_safety` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- Existing infrastructure covers all phase requirements. New test files will be added by plan tasks.

---

## Manual-Only Verifications

All phase behaviors have automated verification.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
