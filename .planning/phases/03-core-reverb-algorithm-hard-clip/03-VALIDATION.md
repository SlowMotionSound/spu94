---
phase: 3
slug: core-reverb-algorithm-hard-clip
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-19
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | TBD (populate from RESEARCH.md Validation Architecture) |
| **Config file** | TBD |
| **Quick run command** | `TBD` |
| **Full suite command** | `TBD` |
| **Estimated runtime** | ~TBD seconds |

---

## Sampling Rate

- **After every task commit:** Run `{quick run command}`
- **After every plan wave:** Run `{full suite command}`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** TBD seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 3-01-01 | 01 | 1 | TBD | — | N/A | unit | `TBD` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

*Planner must fill this table — one row per task across all PLAN.md files, sourcing values from RESEARCH.md Validation Architecture.*

---

## Wave 0 Requirements

- [ ] TBD — populate from RESEARCH.md

*If none: "Existing infrastructure covers all phase requirements."*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| TBD | TBD | TBD | TBD |

*If none: "All phase behaviors have automated verification."*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < TBD
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
