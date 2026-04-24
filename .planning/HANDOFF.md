# SPU-94 M1 Close-Out — Handoff for Fresh Session

**Created:** 2026-04-24
**Session model:** Claude Opus 4.7 (1M context), MAX effort
**Context:** Anthony ran three parallel code reviews + an architectural audit, then asked "just fix the thing." This session started executing the Stage 1 remediation plan. Context is being cleared mid-Step-3 and this document lets a fresh session resume cleanly.

---

## Start here

1. Read this document end-to-end.
2. Read `.planning/ARCHITECTURAL-AUDIT.md` — root causes + target architecture.
3. Read `.planning/REVIEW-c-core.md`, `REVIEW-cli-python.md`, `REVIEW-tests-ci.md` for per-finding detail (skim; the audit doc is the synthesis).
4. Read `.planning/v1.0-MILESTONE-AUDIT.md` for the pre-remediation requirements cross-reference.
5. `git status` + `git log --oneline -6` to confirm the tree state matches what this handoff claims.
6. Resume at Step 3 (see below).

Anthony's standing collaboration preferences (from memory, all still apply):

- Plain language, no SWE jargon. Translate with analogies from *varied* everyday domains (not just recording/studio).
- Small bites. One decision at a time. Don't dump menus.
- No ceremony on bug fixes — apply fixes inline with atomic commits, don't spawn new phases or discuss cycles.
- Announce intent before editing durable planning artifacts (ROADMAP.md, PROJECT.md, DECISIONS.md). Already-committed ADR-Phase-6-I is an example of this pattern done right — the audit doc proposed it, Anthony approved, then the ADR landed.
- Tag `[RECOMMENDED]` on the leaning option when presenting choices.
- `--effort max` is set for this work; `--model opus` too. Use the reasoning budget.

---

## What this session accomplished

### Stage 1 Step 1 — grep-guard + CLI tail-seconds (commit `262930a`)

Two-tier grep-guard:
- **Tier 1 (core):** `src/spu94/`, `include/spu94/` — no float/double/malloc/calloc/realloc/free/unqualified-long.
- **Tier 2 (CLI):** `src/cli/` — no float/double only. malloc/free allowed (CLI legitimately owns its work-buf allocation + dr_wav I/O). Unqualified long allowed (strtol/ftell/printf %ld are stdlib boundaries).

`--tail-seconds` parser rewritten in integer-only arithmetic. No `strtod`, no `double → uint64_t` cast UB, 600-second hard cap, overflow guards on `tail_frames + input.num_frames` and `total_out * sizeof(int16_t)`.

Fixture tests extended 7 → 11 cases covering both tiers.

### Stage 1 Step 2 — reverb input wiring CR-01 (commit `4fcad49`)

The critical bug. Pre-fix, `spu94_process` wrote raw 44.1 kHz input samples into `state->mix_bus_l/r`, and the reverb body read that mailbox at tick time — **bypassing the Phase 4 anti-alias FIR for the reverb's input path.** HF content above 11 kHz aliased into the reverb.

Post-fix: `chain_step_impl` writes `state->mix_bus_l = dec_l; state->mix_bus_r = dec_r;` inside the retained-phase branch on the production path, immediately before `spu94_tick`. The decimator's 22.05 kHz band-limited output is what feeds the reverb now. The test-only reverb-bypass path is unchanged.

Contradicting comments reconciled across three files. ADR-Phase-6-I prepended to `docs/DECISIONS.md` documenting the fix. All 50 golden WAVs regenerated via CLI (`scripts/regenerate_goldens.py` → 50/50 `--check` pass). Modulation report regenerated (105/105 stability_ok=true, still all stable). LEVERS-CATALOG.md vIIR row updated (pre-fix reading was contaminated by HF aliasing; post-fix ~500 Hz is the real character). 72/72 fast ctests green.

`.planning/v1.0-GOLDENS-REGEN.md` documents the regen event.

### Stage 1 — audit artifacts captured (commit `92cca7c`)

Five markdown docs committed so the evidence trail is citable:
- `.planning/ARCHITECTURAL-AUDIT.md`
- `.planning/REVIEW-c-core.md`
- `.planning/REVIEW-cli-python.md`
- `.planning/REVIEW-tests-ci.md`
- `.planning/v1.0-MILESTONE-AUDIT.md`

---

## Current state (end of this session)

**Branch:** `master`
**HEAD:** `92cca7c docs(audit): capture M1 close-out audit artifacts`
**Working tree:** dirty — two files have uncommitted Step-3 edits (see below).
**ctest status (pre-Step-3 edits):** 72/72 fast set green. 50/50 goldens match. Grep-guard green.

### Uncommitted Step 3 work

Two files have been edited but NOT committed. They're the foundation of Step 3:

**`include/spu94/spu94.h`** — added:

1. Three new `spu94_result_t` enum values, numerically appended so ABI/D-07 append-only contract is preserved:
   ```c
   SPU94_INVALID_STATE      = 4,
   SPU94_WORK_BUF_TOO_SMALL = 5,
   SPU94_INVALID_ARG        = 6,
   ```
2. `#define SPU94_WORK_BUF_MAX_BYTES 0x80000u` — 512 KB, guaranteed to fit every factory preset (full PS1 SPU RAM).
3. `size_t spu94_preset_min_work_buf_size(spu94_preset_id_t id);` declaration with full contract docstring.
4. Updated `spu94_load_preset` contract docstring — new return codes + migration note explaining the behavior change for NULL-state and out-of-range-id cases.

**`src/spu94/spu94_presets.c`** — added:

1. `#include "spu94_state_internal.h"` (needed to access `state->work_buf_size`).
2. `spu94_preset_min_work_buf_size` implementation — scans the preset's u16-type registers, returns `(max_halfword_value + 1) * 2` bytes. Rationale: the reverb network's tap formulas access at `m*`, `m* − d*`, or `buffer_address + delta`. All bounded above by the max u16 register value.
3. Rewrote `spu94_load_preset` to validate:
   - `state == NULL` → `SPU94_INVALID_STATE` (was `SPU94_OK`)
   - `id` out of range → `SPU94_INVALID_ARG` (was `SPU94_UNKNOWN_REG`)
   - `state->work_buf_size < required` → `SPU94_WORK_BUF_TOO_SMALL` (was `SPU94_OK` + silent degradation)
   - All early-return paths leave state un-mutated.

### Why not committed yet

The tightened `spu94_load_preset` contract will break two tests immediately if committed without companion caller updates:

1. **`tests/python/binding/test_binding_numpy_contract.py:200`** asserts `load_preset(state, 99) == SPU94_UNKNOWN_REG`. Post-fix it returns `SPU94_INVALID_ARG`. One-line test update.

2. **`tests/python/binding/test_binding_preset_table.py:116`** calls `spu94_init(..., work_buf, 8192)` then `load_preset(HALL)`. Hall needs `~11124` bytes. Post-fix this returns `SPU94_WORK_BUF_TOO_SMALL` and the test's subsequent assertions fail. Change `8192` → `SPU94_WORK_BUF_MAX_BYTES` (or any value `>= spu94_preset_min_work_buf_size(HALL)`).

The remaining caller updates (CLI, Python binding, ADR) are additive and won't break anything.

### Resume recipe for Step 3

Do these in order, then build, test, commit as one atomic change:

1. **`src/cli/main.c`** (around line 187) — check `spu94_load_preset`'s return value. On non-OK, emit a `SPU94_ERROR(...)` line and return 2. Currently the return value is discarded with `(void)`. Add branches for `SPU94_INVALID_ARG` and `SPU94_WORK_BUF_TOO_SMALL` (the CLI uses 512 KB so the latter is unreachable in practice, but the branch keeps the error-surface tidy). Don't need to handle `SPU94_INVALID_STATE` — the CLI guarantees non-NULL state at that point.

2. **`python/spu94/_binding.py`** (after line 225 existing `SPU94_UNKNOWN_REG = 2`) — add the three new constants:
   ```python
   SPU94_INVALID_STATE      = 4
   SPU94_WORK_BUF_TOO_SMALL = 5
   SPU94_INVALID_ARG        = 6
   ```
   Also add a Python-visible `SPU94_WORK_BUF_MAX_BYTES = 0x80000`.

3. **`python/spu94/api.py`** — update `load_preset` to raise a clear exception on `SPU94_WORK_BUF_TOO_SMALL` with a message naming the required size (call `spu94_preset_min_work_buf_size` through the binding). Update the imports at the top to surface the new constants. Update `load_preset`'s docstring.

4. **`tests/python/binding/test_binding_numpy_contract.py:200`** — change `SPU94_UNKNOWN_REG` to `SPU94_INVALID_ARG`.

5. **`tests/python/binding/test_binding_preset_table.py:116`** — change `spu94_init(state_buf, 16384, work_buf, 8192)` to use `SPU94_WORK_BUF_MAX_BYTES` (or just a literal 65536 — anything ≥ Hall's min). Update the fixture's `work_buf` allocation accordingly.

6. **`tests/python/binding/test_binding_surface.py`** — consider adding asserts pinning the three new enum values (`SPU94_INVALID_STATE == 4`, etc.) for ABI regression coverage.

7. **`docs/DECISIONS.md`** — prepend ADR-0022 at line 33 (follow the pattern ADR-Phase-6-I used in commit `4fcad49`). Title: "Work-buf sizing contract + load_preset validation." Cover:
   - The enum extension (append-only, D-07-compliant).
   - `SPU94_WORK_BUF_MAX_BYTES` constant.
   - `spu94_preset_min_work_buf_size` function + its conservative-upper-bound derivation.
   - `spu94_load_preset` validation tightening — explicitly document the behavior change for NULL-state and out-of-range-id cases; note that callers branching on `rc != SPU94_OK` continue to work.
   - Relate to: Root Cause #1 in `ARCHITECTURAL-AUDIT.md` (the silent-drop pattern); `REVIEW-c-core.md` HI-01 (the finding that triggered this).

8. **Build:** `cmake --build build -j4` — expect clean.

9. **Test:** `ctest --test-dir build -E "fuzz_process|fuzz_reverb|fuzz_fir|fuzz_buffer|rt_bench_latency|rt_no_syscalls|bench_process" --output-on-failure` — expect 72/72 green.

10. **Commit** as one atomic change. Suggested message shape:
    ```
    feat(api): work-buf size contract + load_preset validation (ADR-0022)

    Closes Root Cause #1 from .planning/ARCHITECTURAL-AUDIT.md:
    the C-core "silent drop" contract — where silent clamps/no-ops/OK-returns
    at the hot path hid buffer-size mismatches from every outer layer.

    Changes:
    - ABI (append-only, D-07-stable): SPU94_INVALID_STATE=4,
      SPU94_WORK_BUF_TOO_SMALL=5, SPU94_INVALID_ARG=6.
    - New constant SPU94_WORK_BUF_MAX_BYTES = 0x80000 (= 512 KiB; fits every
      factory preset).
    - New accessor spu94_preset_min_work_buf_size(id) — O(35) scan, returns
      the conservative upper bound on the reverb network's deepest tap.
    - spu94_load_preset tightened: NULL state -> INVALID_STATE, out-of-range
      id -> INVALID_ARG, undersized work_buf -> WORK_BUF_TOO_SMALL. All
      non-OK returns leave state un-mutated.
    - CLI main.c: checks load_preset return, emits clean error on non-OK.
    - Python _binding.py: exports the new constants.
    - Python api.py: raises a clear exception on WORK_BUF_TOO_SMALL.
    - Tests updated for the new error surface.
    - ADR-0022 documents the contract tightening and its rationale.

    Closes review findings: HI-01 (REVIEW-c-core.md), CR-01/CR-02 for C core
    (REVIEW-tests-ci.md), partially CR-02 (CLI-Python) — the Python default
    still needs raising to MAX_BYTES; that's Step 5.

    Stage 1 Step 3 of .planning/ARCHITECTURAL-AUDIT.md Part 6.

    Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
    ```

---

## Remaining steps (Stage 1 + Stage 2)

Per `.planning/ARCHITECTURAL-AUDIT.md` Part 6. Do them in order; each is a separate atomic commit.

| # | Step | Notes |
|---|------|-------|
| 4 | Add observable error counters | `spu94_get_error_counters()` + `oob_tap_count` field on state. Wire `reverb_buf_read`/`reverb_buf_write` to increment on OOB. Keep hot path silent, make the event observable after the fact. |
| 5 | Raise Python default to max | `SPU94()` and `api.init()` default → `SPU94_WORK_BUF_MAX_BYTES`. Unify fuzz harness constants through `tests/python/_constants.py`. |
| 6 | Tighten self_test + modulation harness | Assert `counters.oob_tap_count == 0` after each preset run. Non-silence RMS floor. Commit known-good reference signatures. |
| 7 | Fix NULL-state-on-mutation across set_reg_* | Setters return `SPU94_INVALID_STATE` on NULL state, not `SPU94_UNKNOWN_REG`. |
| 8 | Fix CLI and Python critical findings | Most notable: `wav_io.c` bits-per-sample gate (refuse non-16-bit WAVs with clear error — CLI-Python review C-01), `api.destroy()` nulls the handle `.value` (H-03), other high-severity items from the reviews. |
| 11 | Push to GitHub + verify CI green | **First real CI run.** Confirm all 5 jobs green (build-and-test gcc+clang, grep-guard, clang-tidy+cppcheck, ubsan, reproducibility). Commit the CI run URL into `MILESTONES.md` as a citable fact. |
| 12 | Land witness-diff tolerance gate | ADR + `witness_diff_thresholds.json`. Even a loose gate ("catches complete divergence") is infinitely better than the current "prints numbers, exits 0." |
| 13 | Land external-anchor test | Hand-computed expected output for a single known impulse through the simplest preset (Off or Room). 8-sample bit-identical assertion. Catches "bits reproducible but to garbage." |
| 14 | Write Phase 6 VERIFICATION.md | From existing UAT + SUMMARY evidence. Closes the 3-source audit gap for PYBIND-01..06, CLI-01..04, DOCS-04. |
| 15 | Re-run audit + close M1 | `/gsd-audit-milestone v1.0` → expect `passed`. Then `/gsd-complete-milestone v1.0`. Git tag `v1.0`. |

Steps 9 and 10 (golden regen, modulation regen) were already completed during Step 2 because the wiring fix forced them.

## What stays untouched (preserve during remediation)

- Q15 math primitives + their err-tap semantics.
- FIR overflow-width proof.
- 35-entry write-policy table + IMMEDIATE/TICK_LATCHED dispatch.
- State lifecycle (`init`/`reset`/`destroy`) heap-free discipline.
- Docker reproducibility pin (SHA256-digest).
- Strace-based hot-path allocation gate + its WILL_FAIL negative meta-test.
- Preset three-source audit (`verify_preset_sources.py`).
- Benchmark harness + committed baselines.
- `fuzz_buffer.py`'s independent-Python-model vs C comparison (genuinely thorough).

See `.planning/ARCHITECTURAL-AUDIT.md` Part 7 for full "don't touch" list with rationale.

## Decided-and-pinned context

- **Phase 8 (MCU cross-compile) is PARKED.** Moves to between M4 and M5. Do NOT attempt to close Phase 8 during M1 close-out. BUILD-03 stays as a deliberate deferred gap in `MILESTONES.md`.
- **Milestone order:** M1 reverb → M2 ADPCM → M3 DAC modeling → M4 plugin → Phase 8 MCU → M5 hardware validation. Anthony explicitly chose sequential, NOT combined M2+M3+M4.
- **The post-fix reverb runs anti-aliased.** All pre-2026-04-24 measurements (goldens, witness diffs, modulation reports) reflected aliased input and are historically incorrect. The current committed goldens (commit `4fcad49`) reflect the corrected behavior.

## If you hit a conflict between this document and ARCHITECTURAL-AUDIT.md

The audit document is the architectural reference; this handoff is the operational state. They should agree on the target shape. If the code in the tree diverges from what either document describes, trust the code, update the document that's wrong, and keep going.

---

*End of handoff.*
