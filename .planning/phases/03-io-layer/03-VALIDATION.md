---
phase: 3
slug: io-layer
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-04-27
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest + Unity (C), pytest (Python) |
| **Config file** | `tests/CMakeLists.txt` (C), `pyproject.toml` (Python) |
| **Quick run command** | `cd build && ctest -R "adpcm\|vag" --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure && pytest tests/ -x` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -R "adpcm\|vag" --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure && pytest tests/ -x`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | ADPCM-IO-03, ADPCM-IO-04 | unit (C) | `ctest -R "vag" --output-on-failure` | W0 | pending |
| 03-01-02 | 01 | 1 | ADPCM-IO-01, ADPCM-IO-02 | integration (CLI) | `pytest tests/cli/test_cli_adpcm.py -x` | W0 | pending |
| 03-02-01 | 02 | 1 | ADPCM-IO-06 | manual | Build standalone, click toggle, listen | manual-only | pending |
| 03-03-01 | 03 | 2 | ADPCM-IO-05 | unit (Python) | `pytest tests/python/binding/test_binding_adpcm.py -x` | W0 | pending |

---

## Wave 0 Requirements

- [ ] `tests/unit/vag/test_vag.c` — unit tests for VAG read/write (ADPCM-IO-03, ADPCM-IO-04)
- [ ] `tests/cli/test_cli_adpcm.py` — CLI integration tests (ADPCM-IO-01, ADPCM-IO-02)
- [ ] `tests/python/binding/test_binding_adpcm.py` — Python binding tests (ADPCM-IO-05)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| JUCE ADPCM toggle enables/disables coloration during playback | ADPCM-IO-06 | No headless JUCE test infrastructure | Build standalone, load WAV, play with toggle on/off, verify audible difference |

---

## Validation Sign-Off

- [x] All tasks have automated verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 15s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
