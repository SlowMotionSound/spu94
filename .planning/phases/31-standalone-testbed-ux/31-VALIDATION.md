---
phase: 31
slug: standalone-testbed-ux
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-16
---

# Phase 31 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C unit tests) + manual UAT |
| **Config file** | tests/CMakeLists.txt |
| **Quick run command** | `cd build && ctest -R voice --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -R voice --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 31-01-01 | 01 | 1 | TEST-01 | — | N/A | manual (GUI) | N/A — requires GUI interaction | N/A | ⬜ pending |
| 31-01-02 | 01 | 1 | TEST-02 | — | N/A | manual (GUI + audio) | N/A — requires audio output verification | N/A | ⬜ pending |
| 31-01-03 | 01 | 1 | TEST-03 | — | N/A | manual (MIDI controller) | N/A — requires external MIDI hardware | N/A | ⬜ pending |
| 31-01-04 | 01 | 1 | TEST-04 | — | N/A | automated (pluginval) | `pluginval --validate build/spu94_plugin_artefacts/VST3/SPU-94.vst3 --strictness-level 7` | Existing CI gate | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Existing infrastructure covers all phase requirements. No new test framework or stubs needed.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| WAV loads into voice RAM with file name and byte count displayed | TEST-01 | Requires GUI interaction and visual confirmation | Load a WAV via button or drag-drop; verify filename and encoded byte count appear in status label |
| GUI trigger button produces audible output at specified pitch | TEST-02 | Requires audio output verification | Set pitch knob, click Trigger, confirm audio; click Stop, confirm silence |
| MIDI note-on triggers voice at correct pitch, note-off releases | TEST-03 | Requires external MIDI device | Connect MIDI controller, play notes, verify polyphonic output; release notes, verify silence |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
