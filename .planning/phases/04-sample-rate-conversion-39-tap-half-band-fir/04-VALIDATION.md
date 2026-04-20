---
phase: 04
slug: sample-rate-conversion-39-tap-half-band-fir
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-20
---

# Phase 04 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | {Unity (C unit tests) + pytest (Python fuzz/reference)} |
| **Config file** | {tests/CMakeLists.txt + tests/python/} |
| **Quick run command** | `{cmake --build build --target fir_unit_tests && ctest --test-dir build -L fir -j}` |
| **Full suite command** | `{cmake --build build && ctest --test-dir build -j && python tests/python/fuzz_fir.py}` |
| **Estimated runtime** | ~{TBD by planner} seconds |

---

## Sampling Rate

- **After every task commit:** Run `{quick run command}`
- **After every plan wave:** Run `{full suite command}`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** {N} seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| {to be filled by planner — tasks from 04-0N-PLAN.md} | | | CORE-06 / CORE-07 | | | unit / property / fuzz | | | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/fir/` directory scaffold — Unity test TUs for decimator, interpolator, accumulator-bound, bit-identity
- [ ] `tests/python/derive_fir_reference.py` — hand-derives expected impulse response and round-trip references from the 39-tap coefficient table
- [ ] `tests/python/fuzz_fir.py` — 10⁶-step fuzz harness (follows `fuzz_buffer.py` / `fuzz_reverb.py` template)

*Planner to refine with specific stubs per plan.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Mednafen / DuckStation FIR-implementation classification | CORE-06 / CORE-07 witness axis | Requires running third-party emulator binaries on test ROM and FFT-inspecting audio output | Protocol specified in 04-RESEARCH.md § Witness Analysis; results recorded in the witness-classification ADR |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < {N}s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
