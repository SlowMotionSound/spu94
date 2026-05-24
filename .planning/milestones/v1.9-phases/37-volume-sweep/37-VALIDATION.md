---
phase: 37
slug: volume-sweep
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-22
---

# Phase 37 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake/CTest + custom C test harness |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `cd build && ctest --output-on-failure -R sweep` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest --output-on-failure -R sweep`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 37-01-01 | 01 | 1 | SWEEP-04 | — | N/A | unit | `ctest -R adsr` | existing | ⬜ pending |
| 37-01-02 | 01 | 1 | SWEEP-01..08 | — | N/A | unit | `ctest -R sweep` | ❌ W0 | ⬜ pending |
| 37-02-01 | 02 | 2 | SWEEP-09 | — | N/A | unit | `ctest -R sweep_neg` | ❌ W0 | ⬜ pending |
| 37-02-02 | 02 | 2 | SWEEP-10 | — | N/A | doc | manual review | — | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/voice/test_sweep.c` — stubs for SWEEP-01..09
- [ ] Test helpers for counter-accumulate verification and sweep level assertions

*Existing test infrastructure (CMake/CTest) covers framework needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| ADR content review | SWEEP-10 | Documentation quality | Review ADR for completeness of negative-phase uncertainty, spec source |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
