---
phase: 23
slug: gui-overlay-display
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-05
---

# Phase 23 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C test framework, vendored) |
| **Config file** | tests/unit/CMakeLists.txt |
| **Quick run command** | `cd build_test && ctest -R "test_macro\|test_snap\|test_safety" -j$(nproc)` |
| **Full suite command** | `cd build_test && ctest -j$(nproc)` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build_test && ctest -R "test_macro|test_snap|test_safety" -j$(nproc)`
- **After every plan wave:** Run `cd build_test && ctest -j$(nproc)`
- **Before `/gsd-verify-work`:** Full suite must be green + manual visual verification
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 23-01-01 | 01 | 1 | GUI-01 | — | N/A | manual | Build and launch, verify macro panel shows | N/A (visual) | ⬜ pending |
| 23-01-02 | 01 | 1 | GUI-02 | — | N/A | manual | Click toggle, verify register panel shows | N/A (visual) | ⬜ pending |
| 23-01-03 | 01 | 1 | SAFE-05 | — | N/A | manual | Verify vLIN/vRIN/vLOUT/vROUT not visible | N/A (visual) | ⬜ pending |
| 23-02-01 | 02 | 1 | GUI-03 | — | N/A | unit | `ctest -R test_safety -j$(nproc)` | ✅ | ⬜ pending |
| 23-02-02 | 02 | 1 | GUI-04 | — | N/A | manual | Verify Echo Physics section layout | N/A (visual) | ⬜ pending |
| 23-02-03 | 02 | 1 | GUI-05 | — | N/A | manual | Verify Diffusion section layout | N/A (visual) | ⬜ pending |
| 23-03-01 | 03 | 2 | UNIT-01 | — | N/A | manual | Verify unit labels on macro knobs | N/A (visual) | ⬜ pending |
| 23-03-02 | 03 | 2 | UNIT-02 | — | N/A | manual | Verify dual readout on raw sliders | N/A (visual) | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Existing infrastructure covers all phase requirements. No new test files needed -- this phase is pure GUI construction; correctness is verified through existing C core tests (safety/macro/snap) + visual inspection.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Macro panel is default view | GUI-01 | JUCE GUI visual check | Launch app, verify macro panel shows instead of raw registers |
| Advanced toggle swaps view | GUI-02 | JUCE GUI visual check | Click Advanced toggle, verify register panel appears; click again, verify macro panel returns |
| Echo Physics section layout | GUI-04 | JUCE GUI visual check | Verify Spread/Sweep/Rotate knobs + Sync/Free toggle + 4 echo speed dropdowns visible |
| Diffusion section layout | GUI-05 | JUCE GUI visual check | Verify Amount + Texture knobs + Sync/Free toggle + 2 dAPF dropdowns visible |
| vLIN/vRIN/vLOUT/vROUT hidden | SAFE-05 | JUCE GUI visual check | Verify these 4 registers not visible in macro OR Advanced view |
| Human unit labels | UNIT-01 | JUCE GUI visual check | Verify ms, m, % labels below macro knobs |
| Dual readout in Advanced | UNIT-02 | JUCE GUI visual check | Verify hex + human unit on raw register sliders |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
