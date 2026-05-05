---
phase: 18
slug: i-o-surfaces
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-03
---

# Phase 18 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | pytest (CLI tests), CTest (C unit tests) |
| **Config file** | tests/cli/CMakeLists.txt, tests/unit/tempo/CMakeLists.txt |
| **Quick run command** | `cd build && ctest -L cli -R tempo --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -L cli -R tempo --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 18-01-01 | 01 | 1 | TEMPO-07 | build | `cmake --build . --target spu94_cli` | ✅ | ⬜ pending |
| 18-01-02 | 01 | 1 | TEMPO-07 | integration | `python3 -m pytest tests/cli/test_cli_tempo.py -v` | ❌ W0 | ⬜ pending |
| 18-02-01 | 02 | 1 | TEMPO-08 | build | `cmake --build . --target spu94_standalone` | ✅ | ⬜ pending |
| 18-02-02 | 02 | 1 | TEMPO-08, TEMPO-09 | build | `cmake --build . --target spu94_standalone` | ✅ | ⬜ pending |
| 18-02-03 | 02 | 1 | TEMPO-08, TEMPO-09 | manual | Manual: launch GUI, test tempo controls | N/A | ⬜ pending |
| 18-03-01 | 03 | 2 | TEMPO-08 | build | `cmake --build . --target spu94_standalone` | ✅ | ⬜ pending |
| 18-03-02 | 03 | 2 | TEMPO-08 | manual | Manual: connect MIDI clock source, verify BPM tracking | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/cli/test_cli_tempo.py` — covers TEMPO-07 (--tempo flag integration tests)
- [ ] Update `tests/cli/CMakeLists.txt` — register test_cli_tempo in ctest

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| BPM field in GUI accepts user input and triggers register resnap | TEMPO-08 | GUI interaction requires human | Launch standalone, select INT mode, type BPM, verify register values update |
| Subdivision selectors allow per-register override | TEMPO-09 | GUI interaction requires human | Launch standalone, change individual register dropdown from Global to 1/8, verify that register snaps to 1/8 while others stay on global |
| MIDI clock drives BPM in EXT mode | TEMPO-08 | Requires external MIDI hardware or virtual MIDI | Connect MIDI clock source, select EXT mode, verify BPM display tracks external tempo |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
