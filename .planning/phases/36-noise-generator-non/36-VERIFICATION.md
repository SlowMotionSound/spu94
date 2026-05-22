---
phase: 36-noise-generator-non
verified: 2026-05-22T22:00:00Z
status: passed
score: 7/7 must-haves verified
overrides_applied: 0
re_verification: false
---

# Phase 36: Noise Generator NON Verification Report

**Phase Goal:** Voices can output LFSR pseudo-random noise instead of ADPCM, enabling percussion and texture
**Verified:** 2026-05-22
**Status:** PASSED
**Re-verification:** No -- initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | NON-enabled voice outputs the global LFSR noise level instead of Gaussian interpolation output | VERIFIED | `spu94_voice.c:211-214`: `if (non_enabled) { gauss_out = noise_level; }`. `test_non_voice_outputs_noise` PASS. |
| 2 | Two NON-enabled voices produce identical output samples on every tick (one generator, not per-voice) | VERIFIED | `mixer_tick:566` passes `m->noise_gen.level` (same value) to every voice call. `test_non_two_voices_same_output` PASS: `outx_0 == outx_1`. |
| 3 | ADSR envelope shapes noise output (noise * adsr_level produces percussive noise with fast decay) | VERIFIED | NON branch sets `gauss_out = noise_level` then STEP 2.5 unconditionally applies `q15_mul_truncate(gauss_out, adsr_level)`. `test_non_adsr_shapes_noise` PASS. |
| 4 | ADPCM decode still runs for NON voices (loop flags fire, ENDX status updates) | VERIFIED | STEP 1 (decode) runs unconditionally in `voice_tick`; NON branch is only in STEP 2. `test_non_adpcm_still_runs` PASS: ENDX fires on loop-end block. |
| 5 | Noise frequency is controlled by SPUCNT shift/step; per-voice pitch has no effect on noise | VERIFIED | `set_noise_freq` sets `noise_gen.shift` and `noise_gen.step`. NON branch replaces Gauss with `noise_level` regardless of pitch counter. `test_non_pitch_no_effect` PASS. |
| 6 | Noise generator ticks once globally before the voice loop, not per-voice | VERIFIED | `mixer_tick:524-525`: `spu94_noise_gen_tick(&m->noise_gen)` called once before the 24-voice `for` loop. |
| 7 | ADR-0058 documents the noise LFSR polynomial (taps 15,12,11,10 XOR 1), initial seed (1), and ADPCM-fetch-during-NON decision | VERIFIED | `docs/DECISIONS.md:33-139`: ADR-0058 accepted 2026-05-22, covers all three decision areas. Sources cite nocash psx-spx and DuckStation. |

**Score:** 7/7 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94_noise.h` | `spu94_noise_gen_t` type and init/tick API | VERIFIED | Struct with `int16_t level`, `int32_t timer`, `uint8_t shift/step`. Init and tick declared. Warning about seed=0 absorbing state present. |
| `src/spu94/spu94_noise.c` | LFSR implementation with taps 15,12,11,10 XOR 1 | VERIFIED | 5-step nocash algorithm. `spu94_noise_gen_tick` implemented. Parity from pre-shift level. Double-reload without re-shift. ~30 LOC. |
| `include/spu94/spu94_voice.h` | `non_flags` + `noise_gen` fields on mixer; `set_non`/`set_noise_freq` API; updated `voice_tick` signature | VERIFIED | `non_flags` at line 132, `noise_gen` at line 133. `voice_tick` signature has `noise_level` + `non_enabled` params. Both API functions declared. |
| `src/spu94/spu94_voice.c` | NON branch in `voice_tick`; noise tick in `mixer_tick`; `set_non`/`set_noise_freq` implementations | VERIFIED | NON branch at lines 211-214. `spu94_noise_gen_tick` at line 525. Both setter implementations present with bounds validation. |
| `tests/unit/voice/test_noise_gen.c` | 6 LFSR unit tests | VERIFIED | All 6 functions present and all 6 PASS: seed, deterministic sequence, timer decrement, double-reload, frequency varies, parity from pre-shift. |
| `tests/unit/voice/test_voice_tick.c` | 5 NON integration tests | VERIFIED | All 5 `test_non_*` functions present at lines 1863-2074 and all PASS. |
| `docs/DECISIONS.md` | ADR-0058 entry | VERIFIED | ADR-0058 prepended before ADR-0057. Status Accepted. Requirement NON-01, NON-06, NON-09. All three context/decision areas present. |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `spu94_voice.c (mixer_tick)` | `spu94_noise_gen_tick` | called once before voice loop | VERIFIED | Line 525: `spu94_noise_gen_tick(&m->noise_gen)` appears at line 525, voice loop starts at line 530. Order confirmed. |
| `spu94_voice.c (voice_tick)` | `noise_level` parameter | `if (non_enabled) gauss_out = noise_level` | VERIFIED | Lines 211-214: exact pattern present. `gauss_out` then flows into ADSR multiply and volume multiply. |
| `spu94_voice.c (mixer_tick)` | `voice_tick` call | passes `m->noise_gen.level` and per-voice `non_enabled` | VERIFIED | Lines 560, 566-567: `non_enabled` computed from `non_flags` bitmask; `m->noise_gen.level` passed as `noise_level`. |
| `src/spu94/CMakeLists.txt` | `spu94_noise.c` | registered as build source | VERIFIED | Line 29: `spu94_noise.c` listed in the library target. |
| `tests/unit/voice/CMakeLists.txt` | `test_noise_gen` | registered as `noise_unit` test | VERIFIED | Lines 5-7: executable registered, linked against `spu94_static`, added as `noise_unit`. |

---

### Data-Flow Trace (Level 4)

The artifacts are a C module producing audio samples from a noise register, not a UI component reading from a DB. Level 4 data-flow trace is N/A for this type (pure computation: LFSR state -> noise_level -> gauss_out -> adsr multiply -> volume multiply -> output). The signal flow is verified by the integration tests which confirm non-zero output propagates through the full pipeline.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 6 noise_gen unit tests pass | `./build_test/tests/unit/voice/test_noise_gen` | 6 Tests 0 Failures 0 Ignored | PASS |
| All 5 NON integration tests pass | `./build_test/tests/unit/voice/test_voice_tick` (last 5) | 51 Tests 0 Failures 0 Ignored | PASS |
| Both test suites pass via ctest | `ctest -R "noise_unit\|voice_tick_unit"` | 2/2 tests passed in 0.02s | PASS |
| Zero build warnings | cmake with -Wall -Wextra | Clean build output | PASS |

---

### Probe Execution

No `scripts/*/tests/probe-*.sh` probes declared or found for this phase. The PLAN uses an inline `<automated>` verify block (cmake + ctest). That verify block was executed manually above and passed.

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| NON-01 | 36-01 | LFSR polynomial, seed=1, left-shift, deterministic sequence | SATISFIED | `spu94_noise.c:29-48`. Tests: `test_lfsr_seed_one`, `test_lfsr_deterministic_sequence`, `test_parity_from_pre_shift_level` -- all PASS |
| NON-02 | 36-01 | Timer mechanism: decrement by step, LFSR on underflow, double-reload | SATISFIED | `spu94_noise.c:25-47`. Tests: `test_timer_decrement_and_reload`, `test_double_reload` -- both PASS |
| NON-03 | 36-01 | Noise frequency from SPUCNT shift/step; per-voice pitch has no effect | SATISFIED | `set_noise_freq` sets generator params. Tests: `test_frequency_varies_with_shift`, `test_non_pitch_no_effect` -- both PASS |
| NON-04 | 36-01 | NON 24-bit bitmask selects voices that output noise | SATISFIED | `non_flags` in mixer struct. `set_non` validated. `non_enabled` computed per-voice in mixer loop. `test_non_voice_outputs_noise` PASS |
| NON-05 | 36-01 | All NON-enabled voices read the same NoiseLevel value per tick | SATISFIED | Single `m->noise_gen.level` passed to all `voice_tick` calls. `test_non_two_voices_same_output` PASS: `outx_0 == outx_1` |
| NON-06 | 36-01 | ADPCM decode still runs for NON voices | SATISFIED | STEP 1 unconditional; NON branch only at STEP 2. `test_non_adpcm_still_runs` PASS: ENDX fires |
| NON-07 | 36-01 | ADSR still applies to noise output | SATISFIED | STEP 2.5 `q15_mul_truncate(gauss_out, adsr_level)` runs after NON substitution. `test_non_adsr_shapes_noise` PASS: late_outx > early_outx |
| NON-08 | 36-01 | Noise generator ticks once globally before voice loop | SATISFIED | `mixer_tick:525` -- architectural placement confirmed |
| NON-09 | 36-02 | ADR documenting noise initial state, ADPCM-fetch-during-NON, LFSR polynomial source | SATISFIED | `docs/DECISIONS.md:33-139` -- ADR-0058 Accepted. All three areas documented. Sources cite nocash and DuckStation. |

**All 9 NON requirements satisfied.**

REQUIREMENTS.md traceability table still shows NON-01 through NON-09 as "Pending" -- the table was not updated by this phase. This is a bookkeeping gap, not a code gap: the implementation evidence is present and the tests pass.

---

### Anti-Patterns Found

Scanned all 7 phase-modified files for debt markers (TBD, FIXME, XXX, TODO, HACK, PLACEHOLDER, stub indicators, hardcoded empty returns).

| File | Pattern | Severity | Impact |
|------|---------|----------|--------|
| `docs/DECISIONS.md:25` | "placeholder refs" | Info | Pre-existing text in the ADR format header describing BIB-NNN placeholder convention from Phase 7. Not introduced by this phase, not a code smell. |
| All implementation files | None found | -- | Clean |

No blockers. No unreferenced TBD/FIXME/XXX markers in any file touched by this phase.

---

### Human Verification Required

None. All behaviors are deterministically testable:

- LFSR sequence is mathematical and verified by unit tests
- NON routing is tested by integration tests with controlled inputs
- ADPCM-during-NON is tested by observing ENDX flag state
- Global single-tick behavior is architectural and verified by code inspection

---

### Gaps Summary

No gaps. All 9 requirements are covered by substantive, wired, tested implementations. Both commits (f4ad508 RED, b5143c3 GREEN) exist in git history. The ADR-0058 commit (a4782cc) also exists. All 57 tests pass with zero failures and zero regressions from prior phases.

The only cosmetic gap is that REQUIREMENTS.md traceability table checkboxes for NON-01 through NON-09 remain unchecked ([ ] instead of [x]). This is a documentation bookkeeping item outside the phase's declared `files_modified` scope and does not affect goal achievement.

---

_Verified: 2026-05-22_
_Verifier: Claude (gsd-verifier)_
