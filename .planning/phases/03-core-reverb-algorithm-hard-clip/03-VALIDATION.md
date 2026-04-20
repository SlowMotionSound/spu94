---
phase: 3
slug: core-reverb-algorithm-hard-clip
status: approved
nyquist_compliant: true
wave_0_complete: false
created: 2026-04-19
updated: 2026-04-19 (Phase 3 planning)
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C, vendored at `tests/vendor/Unity/`, Phase 1) + Python 3 ctypes (stdlib) |
| **Config file** | `tests/unit/CMakeLists.txt` (Phase 3 adds `tests/unit/reverb/` subdir); `tests/python/CMakeLists.txt` (Phase 3 adds `fuzz_reverb` ctest target) |
| **Quick run command** | `ctest --output-on-failure --test-dir build -R reverb` |
| **Full suite command** | `ctest --output-on-failure --test-dir build` |
| **Estimated runtime** | ~4–5 s total (Phase 3 unit tests ~0.5s; fuzz_reverb ~2.5s at 10^6 steps; fuzz_buffer ~2.5s) |

---

## Sampling Rate

- **After every task commit:** Run `ctest --output-on-failure --test-dir build -R reverb` (Phase 3 unit tests only — sub-second)
- **After every plan wave (Plans 01, 02, 03, 04 merge):** Run `ctest --output-on-failure --test-dir build` (full suite including Phase 1 + Phase 2 regressions + both fuzz harnesses)
- **Before `/gsd-verify-work`:** Full suite must be green; `bash scripts/ci/grep-guard.sh`, `bash scripts/ci/verify-no-heap-symbols.sh`, clang-tidy, cppcheck, UBSan all clean on a fresh build.
- **Max feedback latency:** < 5 seconds for the quick run; < 10 seconds for the full suite.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 3-01-01 | 01 | 1 | CORE-02, CORE-05 | T-03-02, T-03-03 | N/A | structural | `grep -q "#ifndef SPU94_REVERB_INTERNAL_H" src/spu94/spu94_reverb_internal.h && cmake --build build --target spu94_obj` | ❌ W0 | ⬜ pending |
| 3-01-02 | 01 | 1 | CORE-02, CORE-05 | T-03-01, T-03-02 | N/A | integration | `cmake --build build --target spu94_shared && ctest --test-dir build -R fuzz_buffer` (no regression) | ❌ W0 | ⬜ pending |
| 3-01-03 | 01 | 1 | CORE-02 | T-03-01 | hard-clip saturation on mix bus | unit | `ctest --output-on-failure --test-dir build -R "^reverb_(hard_clip|input_scale|output_scale)$"` | ❌ W0 | ⬜ pending |
| 3-02-01 | 02 | 2 | CORE-05, CORE-08 | T-03-05, T-03-06, T-03-07 | int32 widening avoids INT16_MIN-negation UB | unit+build | `cmake --build build && ctest --test-dir build -R "^reverb_"` | ❌ W0 | ⬜ pending |
| 3-02-02 | 02 | 2 | CORE-05, CORE-08 | T-03-05, T-03-06 | cross-side tap pairing correct (dRDIFF→mLDIFF) | unit+build | `cmake --build build && ctest --test-dir build -R "^reverb_"` | ❌ W0 | ⬜ pending |
| 3-02-03 | 02 | 2 | CORE-05, CORE-08, TEST-06 | T-03-06 | vIIR=INT16_MIN anomaly test + non-anomaly control | unit | `ctest --output-on-failure --test-dir build -R "^reverb_(same|diff)_iir$"` | ❌ W0 | ⬜ pending |
| 3-03-01 | 03 | 3 | CORE-05 | T-03-10 | D-07 cascading-sat NOT int32-accumulate | unit+build | `cmake --build build && ctest --test-dir build -R "^reverb_"` | ❌ W0 | ⬜ pending |
| 3-03-02 | 03 | 3 | CORE-05 | T-03-09, T-03-11 | APF subtract Pitfall-1 guarded; feedback-loop edge stable | unit+build | `cmake --build build && ctest --test-dir build -R "^reverb_"` | ❌ W0 | ⬜ pending |
| 3-03-03 | 03 | 3 | CORE-05 | T-03-09, T-03-10, T-03-11 | per-stage bit-exactness against Python reference | unit | `ctest --output-on-failure --test-dir build -R "^reverb_(comb|apf1|apf2)$"` | ❌ W0 | ⬜ pending |
| 3-04-01 | 04 | 4 | TEST-07 | T-03-01..T-03-11 | Q15 saturation/truncation/overflow edges at every stage | unit | `ctest --output-on-failure --test-dir build -R "^reverb_edges$"` | ❌ W0 | ⬜ pending |
| 3-04-02 | 04 | 4 | TEST-07, TEST-06 | T-03-05, T-03-12 | SC-1 composition equivalence + 10^6-step fuzz invariants | unit+property | `ctest --output-on-failure --test-dir build -R "^(reverb_body|fuzz_reverb)$"` | ❌ W0 | ⬜ pending |
| 3-04-03 | 04 | 4 | — | T-03-13 | ROADMAP SC-5 ADR entries landed in DECISIONS.md | doc | `grep -qE "^## ADR-000[789]" docs/DECISIONS.md && grep -qE "^## ADR-001[01]" docs/DECISIONS.md` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Every test file in the map above is new — does not exist at the start of Phase 3. The Wave 0 scaffolding that must exist BEFORE any Phase 3 task runs:

- [x] Unity framework vendored + built (Phase 1 — already available)
- [x] spu94_static / spu94_shared CMake targets (Phase 1 — already available)
- [x] tests/unit/ CMake structure with add_subdirectory pattern (Phase 2 — already in place)
- [x] tests/python/ CMake structure with `$<TARGET_FILE:spu94_shared>` env var pattern (Phase 2 Plan 05 — already in place)
- [ ] NEW (Plan 01 Task 3): `tests/unit/reverb/` directory + CMakeLists.txt scaffold
- [ ] NEW (Plan 01 Task 1): `src/spu94/spu94_reverb_internal.h` declared (scaffolds the internal symbol namespace tests include)
- [ ] NEW (Plan 01 Task 3): `tests/python/derive_reverb_reference.py` — the reference model that every C test table derives its expected values from (Pitfall 9 provenance)

*Everything else is a downstream dependency. Plans 02/03/04 assume the Plan 01 scaffolding is present.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| ADR paraphrase discipline (DOCS-03) | — | requires reading ADR prose vs nocash prose; grep can catch literal quotes but not semantic paraphrase | Human reviewer reads docs/DECISIONS.md ADR-0007..ADR-0011; verifies each is in SPU-94's own wording and cites psx-spx only in Sources sections |
| Test-value provenance (Pitfall 9) | TEST-06, TEST-07 | grep catches literal emulator names; cannot catch silent translation of emulator output into reference values | Human reviewer inspects the `ref_*` functions in tests/python/derive_reverb_reference.py and the C test tables; confirms every value is derivable from nocash pseudocode via the Python script, not pasted from emulator output |
| Musical character judgment of D-07 cascading variant | — | taste-based; the revert lever exists precisely because this is not programmatically verifiable | Deferred to M4 plugin-era user testing (out of Phase 3 scope) |

*All Phase 3 behaviors have automated verification via ctest. The three items above are discipline checks, not functional checks.*

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies — every task in every plan has a concrete ctest command or grep invariant in its `<verify><automated>` block
- [x] Sampling continuity: no 3 consecutive tasks without automated verify — every task has an automated verify
- [x] Wave 0 covers all MISSING references — Plan 01 Task 1 creates the internal header; Plan 01 Task 3 creates the test subdir + Python reference; subsequent plans depend on these
- [x] No watch-mode flags — all ctest invocations are one-shot
- [x] Feedback latency < 10 seconds (quick run < 1s; full suite ~5s)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved for execution
