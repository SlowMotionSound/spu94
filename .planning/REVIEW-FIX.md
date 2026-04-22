---
status: partial
date: 2026-04-22
scope: critical_warning
findings_in_scope: 6
fixed: 6
skipped: 16
iteration: 1
---

# Whole-Codebase Code Review — Fix Report

**Source review:** `.planning/REVIEW.md`
**Iteration:** 1
**Scope:** Critical + High findings in the IN list defined by the orchestrator prompt. Out-of-scope classes (README polish, install-path, PyPI, PEP 668) skipped per Anthony's standing guidance (`feedback_readme_not_priority.md`). CR-01 (Hall DSP output pinned at ~-5 dBFS) explicitly routed to a separate `/gsd-debug` session.

**Summary:**
- Findings in scope: 6
- Fixed: 6 (CR-02, HI-01 [a+b], HI-02, HI-03, HI-04, HI-05)
- Skipped: 16 (out-of-scope category or below default scope)
- Status: `partial` — all IN-scope findings landed; skipped items are deliberate deferrals, not fix failures.

## Fixed Issues

### CR-02: Block-size-invariance / in-place tests compared silence to silence

**Files modified:** `tests/unit/process/test_process_block_size.c`, `tests/unit/process/test_process_in_place.c`
**Commit:** `06f5d7c`
**Applied fix:** Added `spu94_set_vLOUT(s, 0x7FFF); spu94_set_vROUT(s, 0x7FFF);` after the Hall preset load in both `fresh_state` helpers. Matches the `test_process_reverb_audible.c` pattern. Both tests still pass — the invariance and bit-identity properties are real, they just weren't being exercised against non-zero output.

---

### HI-01a: test_reverb_body.c discarded Path B's wet output with `(void)` casts

**Files modified:** `tests/unit/reverb/test_reverb_body.c`
**Commit:** `2574c56`
**Applied fix:** Replaced `(void)LeftOutput; (void)RightOutput;` at lines 149-150 with
```
TEST_ASSERT_EQUAL_INT16_MESSAGE(A->reverb_out_l, (int16_t)LeftOutput, ...);
TEST_ASSERT_EQUAL_INT16_MESSAGE(A->reverb_out_r, (int16_t)RightOutput, ...);
```
This pins ADR-Phase-6-G's mailbox-publishing contract at the unit level: Path A's call to `spu94_reverb_body` must leave `reverb_out_l/r` equal to Path B's stage-by-stage scaled wet output.

---

### HI-01b: test_process_flush.c comment described pre-ADR-Phase-6-G broken state

**Files modified:** `tests/unit/process/test_process_flush.c`
**Commit:** `32d5d4f`
**Applied fix:** Rewrote the plan-adjustment comment block (lines 53-71) to describe the current wiring: reverb_body publishes to `state->reverb_out_l/r`, the FIR interpolator consumes the mailbox, `spu94_flush` drains interpolator delay lines with silent input, and the wet path is gated by vLOUT/vROUT. Test behavior unchanged; only the explanation refreshed.

---

### HI-02: CLI test suite never inspected output WAV content

**Files modified:** `tests/cli/test_cli_preset_hall_roundtrip.py`
**Commit:** `cefb994`
**Applied fix:** Extended `test_every_preset_roundtrips` (the parametrized 10-preset check) to read the output WAV's sample bytes and assert: Off produces pure silence, every other preset produces at least one non-zero sample. Raw byte-pair scan, no numpy dependency. All 16 tests in the file pass. This is the CLI-level analogue of the `test_process_reverb_audible.c` pattern and closes the specific class of bug the user spent a day debugging.

---

### HI-03: `self_test` never checked that Hall produces any non-zero output

**Files modified:** `python/spu94/api.py`
**Commit:** `286aee8`
**Applied fix:** Added a non-silent-input audibility arm to `self_test`: after `load_preset("hall")` + `set_reg_i16("vLOUT", 0x7FFF)` + `set_reg_i16("vROUT", 0x7FFF)` + tick, feed a deterministic 2048-sample ramp through `process` and raise `RuntimeError` if every output sample is zero. Verified locally: `python -c "import spu94; spu94.self_test()"` passes against the current build. Buffer size 2048 was chosen empirically — 1024 is the floor on a fresh state (FIR round-trip delay + IIR ramp-up), 2048 gives comfortable headroom. The legacy 1s silent-input + 1s flush path is retained as secondary coverage of the block-loop / ndpointer contract. Now gates every cibuildwheel-produced wheel against the reverb-not-wired class of bug.

---

### HI-04: `fuzz_process.py` non-zero-output invariant depended on random-choice race

**Files modified:** `tests/python/fuzz_process.py`
**Commit:** `629222b`
**Applied fix:** Added explicit `lib.spu94_set_reg_i16(state, vLOUT, 0x7FFF)` + `... vROUT, 0x7FFF)` writes inside `op_load_preset` when the loaded preset is non-Off (id != 0). Pinned the "non-Off preset produces non-zero output within 256 process calls" invariant to the product contract rather than to the statistical chance that a future random op picks the vLOUT register. Verified with a 20,000-step fuzz run under the golden seed (`--seed 0x05F05EED`): all invariants hold, op counts stay balanced.

---

### HI-05: Flat JSON config accepted duplicate register keys silently

**Files modified:** `src/cli/json_config.c`
**Commit:** `f2d295d`
**Applied fix:** Added a stack-allocated `bool seen[SPU94_REG__COUNT]` in the flat-shape branch of `spu94_cli_json_apply`. Each pair's register is looked up via `find_reg_by_name` before `apply_one`; on a duplicate, the CLI returns
```
spu94: error: duplicate register 'vLOUT' in flat config '<path>'
```
and exits non-zero. Required adding `<stdbool.h>` to the include list. Smoke-tested by constructing a 35-pair flat config with a duplicated `vLOUT` — the CLI rejects it with the expected message. All existing CLI pytests (35 tests) still pass.

---

## Skipped Issues

### CR-01: Hall preset output pinned at ~-5 dBFS regardless of input level

**File:** `src/spu94/spu94_reverb.c` (vIIR/vWALL feedback path, system-level)
**Reason:** `skipped-debug-session` — the orchestrator owns a dedicated `/gsd-debug` session file at `.planning/debug/reverb-not-in-audio-path.md`. Not fixable in a review-fix pass; requires instrumented DSP investigation against lv2-psx-reverb / DuckStation behavioral witnesses.
**Original issue:** Output pinned at ~-5 dBFS across 0 / -12 / -24 dBFS input levels, followed by a cliff drop in the tail. Characteristic of a self-oscillating feedback loop or a coefficient-induced limit-cycle attractor.

---

### CR-03: README promises `pip install spu94` but the PyPI package doesn't exist

**File:** `README.md`, `pyproject.toml`
**Reason:** `skipped-deferred` — user has explicitly deferred all README / install-path / PyPI concerns as end-of-project polish. See `feedback_readme_not_priority.md`. Also requires outside-the-repo action (PyPI credentials, GitHub repo creation).
**Original issue:** `pip install spu94` fails (package not on PyPI); README `git clone` URL 404s.

---

### HI-06: pyproject.toml project URLs point at non-existent GitHub repo

**File:** `pyproject.toml:44-46`
**Reason:** `skipped-deferred` — same class as CR-03. Install-path / project-URL metadata is on the explicit "don't surface, don't fix now" list per `feedback_readme_not_priority.md`.
**Original issue:** Homepage / Source / Issues URLs all resolve to 404.

---

### MED-01: README has no venv / PEP 668 warning

**File:** `README.md`
**Reason:** `skipped-deferred` — README / install-path concern; excluded per `feedback_readme_not_priority.md`.

---

### MED-02, MED-03, MED-04, MED-05, MED-06, MED-07, LO-01, LO-02, LO-03, LO-04, INFO-01, INFO-02

**Reason:** `skipped-below-scope` — below the default Critical + High fix threshold for this pass; not requested in the orchestrator's IN list. Candidates for a future fix pass if the user widens the scope (`fix_scope: all`) or if one of them proves to be the root of a later bug.

- MED-02: `spu94_reverb_output_scale` misleading int32 signature comment (`src/spu94/spu94_reverb_internal.h`).
- MED-03: `spu94_cli_preset_canonical_name` unsynchronized static cache race (`src/cli/preset_names.c`).
- MED-04: `wav_io.c` `interleaved_count * sizeof(int16_t)` overflow risk on 32-bit size_t.
- MED-05: `main.c:218-219` `total_out * sizeof(int16_t)` overflow risk.
- MED-06: `spu94_presets.c` discards `spu94_set_reg_i16/u16` return values.
- MED-07: mix_bus downsampling path has no antialiasing filter — flagged pending research verification, not a clear bug.
- LO-01: `test_reverb_body.c` byte-compare doesn't cover `reverb_out_l/r` field equality. (Partially subsumed by HI-01a's new assertions, which pin the same contract via a different assertion path.)
- LO-02: `__init__.py` drift check is upper-bound-only.
- LO-03: ROADMAP.md Phase 7/8 sections copy-pasted from Phase 3.
- LO-04: `TEST_PASS()` smoke-only assertions in `test_process_basic.c`.
- INFO-01: Stale Phase 4 comment in `spu94_io_chain.c`.
- INFO-02: Post-destroy accessor behavior (documented, not a bug).

---

_Fixed: 2026-04-22_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
