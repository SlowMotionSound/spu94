---
phase: 20
slug: macro-engine-safety-core
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-03
---

# Phase 20 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | C unit tests (custom test harness, same as all prior phases) |
| **Config file** | `Makefile` (existing build system) |
| **Quick run command** | `make test` |
| **Full suite command** | `make test` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `make test`
- **After every plan wave:** Run `make test`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 20-01-01 | 01 | 1 | MACRO-01 | — | N/A | unit | `make test` | ❌ W0 | ⬜ pending |
| 20-01-02 | 01 | 1 | MACRO-02 | — | N/A | unit | `make test` | ❌ W0 | ⬜ pending |
| 20-01-03 | 01 | 1 | MACRO-03 | — | N/A | unit | `make test` | ❌ W0 | ⬜ pending |
| 20-01-04 | 01 | 1 | MACRO-04 | — | N/A | unit | `make test` | ❌ W0 | ⬜ pending |
| 20-01-05 | 01 | 1 | MACRO-05 | — | N/A | unit | `make test` | ❌ W0 | ⬜ pending |
| 20-02-01 | 02 | 1 | SAFE-01 | — | vIIR x vWALL product clamped below stability ceiling | unit | `make test` | ❌ W0 | ⬜ pending |
| 20-02-02 | 02 | 1 | SAFE-02 | — | m-prefix writes clamped to work_buf_size boundary | unit | `make test` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_macro_engine.c` — stubs for MACRO-01..05
- [ ] `tests/test_safety.c` — stubs for SAFE-01..02

*Existing test infrastructure (Makefile, test harness) covers framework needs.*

---

## Manual-Only Verifications

*All phase behaviors have automated verification.*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
