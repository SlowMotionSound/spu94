# SPU-94 Architectural Audit — M1 Close-Out Blocker Report

**Produced:** 2026-04-24
**Author:** Claude (Opus 4.7, MAX effort), synthesising across three parallel code reviews (`REVIEW-c-core.md`, `REVIEW-cli-python.md`, `REVIEW-tests-ci.md`) plus direct inspection of the load-bearing files.
**Status:** M1 **cannot** close in the current state. This document names the root causes, proposes a clean target architecture, and gives an ordered remediation plan.

---

## Executive Summary

The `work_buf_size = 8192` bug Anthony spotted is not a one-off. It is the surface expression of a deeper pattern — a "silent drop" contract — that runs through every layer of the library.

Three independent code reviews (C core, CLI + Python, tests + CI) converged on the same root causes. In total: **9 critical findings, 17 high findings, 17 medium, 9 low.**

Those findings collapse into **four architectural root causes**, one confirmed bug, and a small set of elegance passes that would simplify the library meaningfully. What follows is the complete picture, with enough detail that a fresh session can execute the remediation without re-discovering what this one found.

The good news: the foundation is solid. Q15 math is clean, the FIR overflow proof is sound, the register-write-policy table is a nice design, the Docker reproducibility pin is real, the fuzz harnesses are genuinely thorough, the state lifecycle is freestanding-correct. The problems are almost entirely at the *contracts* between layers — not inside any one layer.

---

## Part 1 — Methodology

Files read directly during this audit (to verify or disambiguate review findings):

- `src/spu94/spu94_process.c` — the top-level block-based entry point
- `src/spu94/spu94_io_chain.c` — FIR chain step, tick wiring
- `src/spu94/spu94_reverb.c` (reverb body, ~lines 590–656) — mix-bus read site
- `include/spu94/spu94.h` — full public header
- `python/spu94/reverb.py` (lines 1–100) — `SPU94` class init path
- `docs/DECISIONS.md` (ADR index + ADR-Phase-6-G body)
- Live `bash scripts/ci/grep-guard.sh` run (to confirm the CLI-malloc finding)

Files read indirectly (via the three REVIEW reports which were produced by dedicated code-reviewer agents with their own deep reads):

- All of `src/spu94/*.c`, `include/spu94/*.h`
- All of `src/cli/*.c`, `src/cli/*.h`
- All of `python/spu94/*.py`
- All of `tests/**` and `scripts/ci/**` (primary false-confidence surface)
- `.github/workflows/ci.yml`

Where review findings contradicted, I verified against the code directly and call out the verdict in the relevant section.

---

## Part 2 — Root Causes (Architectural, Not Bug List)

### Root Cause #1 — The "silent drop" contract

**The pattern.** Every layer of the library handles out-of-bounds or out-of-spec input by **silently producing the smallest reasonable value and continuing** — zero on OOB reads, no-op on OOB writes, `SPU94_OK` on NULL state, success on undersized buffers. No layer raises, logs, or increments a visible counter.

**Where it manifests:**

1. **C core hot path.** `reverb_buf_read` / `reverb_buf_write` (`src/spu94/spu94_reverb.c:48-73`) silently return 0 / discard when `byte_off + 1 >= work_buf_size`. No error-count field, no sticky flag, no observable event. The reverb body's "stability" tests pass because `0` is stable.

2. **Init contract.** `spu94_init` (`src/spu94/spu94_state.c:63-96`) validates `state_buf_size` and alignment rigorously but accepts **any** `work_buf_size` including 1. `test_state_lifecycle.c:79-96` pins `work[64]` as legal. This codifies the permissiveness.

3. **Preset load.** `spu94_load_preset` (`src/spu94/spu94_presets.c:463-490`) performs no check that the caller's `work_buf` is large enough for the preset's delay-line addresses. Hall needs ~11 KB; any smaller buffer proceeds without warning.

4. **NULL-state on mutation.** `spu94_load_preset(NULL, id)` returns `SPU94_OK`. This is documented in `spu94.h:293` as "lifecycle-null-safe convention," but it breaks the `if (r) handle_error(r)` idiom — NULL-state on a *mutation* is a caller bug, not a lifecycle idempotence case.

5. **Python default.** `python/spu94/reverb.py:56` and `python/spu94/api.py:52` default `work_buf_size=8192`. Hall needs 11,124. 5 of 10 presets exceed 8 KB.

6. **Self-test.** `api.self_test()` (`python/spu94/api.py:414`) uses the broken 8192 default and asserts `output.any()` — "is there a non-zero sample anywhere?" Even with the buffer short-circuit firing on every tap, the mix-bus scaling and output-scale paths produce non-zero samples, so this passes. The function that was designed to catch exactly this class of bug has the bug inside it.

7. **Modulation harness.** Runs Hall with the 8192 default. Stability check asserts `-32768 <= x <= 32767 and isfinite(x)`. Zero satisfies both. The 211 cases "pass" on degraded audio.

**The root problem.** Every layer trusts the layer below to have validated. None validate. The hot-path silent clamp is defensible (rt-safety: can't raise in the tick), **but only if some earlier layer has gated against it ever firing.** No earlier layer does.

**The fix shape.** Push validation to the load/init boundary (cheap — runs once), and make the hot-path clamp *observable* by incrementing a counter that outer layers can read. The hot-path stays silent (rt-safe); outer layers learn about it after the fact. Details in Part 4.2 / 4.3.

### Root Cause #2 — The broken reverb input wiring (CONFIRMED)

**The claim (from three sources):**

- **ROADMAP Phase 4 goal:** "SPU-94 is bit-faithful at the I/O boundary — the 44.1 kHz host rate is converted to/from the internal 22.05 kHz reverb rate via nocash's documented 39-tap half-band FIR."
- **Comment in `spu94_io_chain.c:62-63`:** "The dry decimator output (dec_l, dec_r) feeds the reverb INPUT via state->mix_bus_l/r."

**The reality (from the code):**

- **`spu94_process.c:40-41`:** `state->mix_bus_l = l; state->mix_bus_r = r;` — where `l` and `r` are the **raw 44.1 kHz input samples** (from `L_in[i]`), **not** the decimator output.
- **`spu94_reverb.c:606-615`:** reads `state->mix_bus_l` as the reverb input.
- **Self-contradicting comment in `spu94_reverb.c:606-608`:** "spu94_process writes state->mix_bus_l/r with the current 44.1 kHz input sample." This is self-consistent with the code but contradicts the io_chain comment.
- **The decimator's output `dec_l` / `dec_r`** is computed inside `chain_step_impl` on the retained phase (`spu94_io_chain.c:51`) and is **never written to `mix_bus_l/r`** on the production (`reverb_active=1`) path. It is only written on the test-bypass (`reverb_active=0`) path, and even then, only as the direct interpolator input.

**What this means in audio terms.** Picture a recording console where one of the aux sends has a fader labelled "post-filter" on the faceplate, but the wiring inside bypasses the filter entirely. The knob does nothing it says it does. In our case: the 39-tap anti-alias half-band FIR is literally built, sits in the DSP graph, but **is not connected to the reverb's input.** The reverb samples the raw 44.1 kHz input stream at the retained-phase instants (effectively nearest-neighbor downsampling — the worst kind, aliases every HF component above 11 kHz straight into the reverb).

**Audibility.** Whether anyone has heard this is an empirical question. Material with significant energy above 11 kHz (cymbals, sibilance, synth brightness, noise fixtures used in tests) will audibly alias. Material that's naturally bandlimited (vocals, bass, midrange instruments) will sound "fine" — meaning this bug could ship without Anthony ever hearing it on his typical material. That's the dangerous part.

**ADR-Phase-6-G does NOT cover this.** ADR-Phase-6-G is titled "Wet-only 44.1 kHz output — chain_step_impl feeds reverb wet into interpolator, not decimator output." It addresses the **output** path: once the reverb produces a 22.05 kHz wet sample, where does that go? (Answer: to the interpolator, not blended with dry.) It is silent on the **input** path. So there is no ADR saying "the reverb input is intentionally raw 44.1 kHz." It's just how the code ended up — most likely an oversight during the Phase 5 "mix-bus mailbox" plumbing (ADR-Phase-5-B), which wired `mix_bus_l/r` in `spu94_process` without considering that the decimator's output was the intended source.

**Evidence this was an oversight, not a design choice:**

1. Phase 4's entire goal is quoted above — "closing the fidelity gap" — and that gap is precisely "reverb sees aliased input." A design that keeps the gap open would be worth an ADR; none exists.
2. The `spu94_io_chain.c:62-63` comment describing the *intent* is correct; the code just doesn't implement it.
3. The `spu94_reverb.c:606-608` comment is describing what the code *does*, and it's written matter-of-factly — no rationale, no trade-off.
4. The test-bypass path in `chain_step_impl` DOES route `dec_l`/`dec_r` directly. Whoever wrote the bypass path understood the decimator's output was the "right" signal to use.

**Verdict: confirmed bug.** See Part 3 for the fix.

### Root Cause #3 — The toothless verification infrastructure

**The bit-faithful claim is the project's core value proposition.** Phase 7 was built explicitly to *earn* that claim through verifiable evidence. Three independent artifacts exist:

1. **Golden files** (50 WAV × SHA256 pairs) — byte-reproducible outputs across environments.
2. **Witness diff vs lv2-psx-reverb** — an independent implementation's output as comparison witness.
3. **Modulation harness** — exercises all 35 registers at audio rate, catches instability.

**The hole.** Each of these artifacts passes CI on its *existence*, not on its *content matching a reference.*

- **Witness diff** (`scripts/ci/witness_diff.py:788`) prints numbers and exits 0. D-06 is explicit: "this harness does NOT gate pass/fail on divergence magnitude." Our `low_band_diff_dbfs` could be 0 dBFS (total divergence) and CI reports green.
- **Golden files** are byte-compared against committed SHA256s. But the committed SHAs are themselves anchored only to the CLI that generated them. If CR-01 is fixed, the goldens will need regeneration — which means the current goldens are frozen snapshots of *the buggy behavior.* There is no externally-anchored reference (e.g., a hand-computed expected value for a known impulse, or a hash of mednafen's output on the same input) saying "these bits are correct, not merely reproducible."
- **Modulation harness** uses the broken work_buf default. Stability passes on degraded output.
- **Determinism test** (`test_witness_determinism.py`) asserts the witness-diff harness produces identical numbers on two consecutive runs. This is satisfied by "always produces garbage in the same way."
- **Self-test** uses the broken default + `.any()` assertion.

**Analogy.** The bit-faithful claim is load-bearing the way a bridge's main truss is load-bearing. Golden files prove the bridge is the same bridge it was yesterday. Witness diff is supposed to prove the bridge matches the blueprint. Modulation harness is supposed to prove the bridge doesn't wobble when you run across it. Today none of them would catch a crack in the truss — they'd just confirm the crack is stable, identical to yesterday's crack, and still holds zero weight.

**The fix shape.** Land at least one *external anchor* (hand-computed impulse response for a known input, or hashed reference output pinned outside our control). Gate the witness-diff on a loose threshold (e.g., `low_band_diff_dbfs < -20` for preset × input pairs where lv2 is a valid witness per ADR-Phase-4-I). Tighten the modulation stability check (non-silence floor + RMS comparison to known-working reference). The goldens can only be trusted *after* CR-01 is resolved and they're regenerated.

### Root Cause #4 — CI has never actually run

**Verified live during this audit:** running `bash scripts/ci/grep-guard.sh` on the current tree **fails** with exit 1. Reason: the CLI legitimately uses `malloc`/`free` for its 512 KB work buffer (`src/cli/main.c:155`), and the grep-guard scans `src/**` including `src/cli/**` but doesn't know the CLI is exempt.

**Phase 1 VERIFICATION.md status:** `human_needed`. The items flagged as needing human verification were exactly:

- `clang-tidy` CI job green on GitHub
- `cppcheck` CI job green on GitHub
- `ubsan` CI job green on GitHub
- `grep-guard` CI job green on GitHub

None of these have run on GitHub Actions. The CI workflow file exists; the pipeline was never actually pushed and observed green. When it *is* pushed for the first time (which Phase 8 was supposed to trigger), it will fail on the grep-guard job immediately.

**This is fixable** — either carve the CLI out of the guard's scope, or add a narrower allowlist that forbids `float`/`double` in the CLI but permits `malloc`/`free` — but it means the "CI is passing" premise that's been assumed throughout M1 is false. There is no CI baseline to regress against. Every "Pending → human verified" item in Phase 1 that's gated on CI has not actually been verified.

**Implication.** Before M1 can close with any integrity, there must be a **green CI run on GitHub Actions** against the post-fix codebase. Until then, BUILD-04, BUILD-05, BUILD-07 are structurally unverified claims.

---

## Part 3 — The Reverb Input Path: Full Resolution

Since this is the one "is it a bug or a design choice?" ambiguity, I want to spell out the fix completely so a fresh session can land it without re-debating the intent.

### The design intent (reconstructed from ROADMAP + comments + ADRs)

**Reverb rate:** 22.05 kHz tick, per PS1 SPU hardware.
**Host rate:** 44.1 kHz, per normal pro-audio convention.
**I/O boundary:** a 39-tap linear-phase half-band FIR, nocash-documented coefficients, at *both* directions (decimator inbound, interpolator outbound). Phase 4's deliverable.
**Reverb input:** should be the **decimator's retained-phase output** — one 22.05 kHz band-limited sample per pair of 44.1 kHz input samples.

### The actual wiring

```
spu94_process(state, L_in, R_in, L_out, R_out, num_samples):
    for i in range(num_samples):
        l = L_in[i]
        state->mix_bus_l = l         # ← raw 44.1 kHz; should be dec_l
        spu94_fir_chain_step(state, l, r, &lo, &ro)

spu94_fir_chain_step → chain_step_impl (reverb_active=1):
    spu94_fir_decimate(state, l_in, r_in, &dec_l, &dec_r, &dec_valid)
    if dec_valid:                    # retained phase — 22.05 kHz sample ready
        # ← this is where mix_bus_l/r SHOULD be written with dec_l/dec_r
        state->reverb_out_l = 0
        state->reverb_out_r = 0
        spu94_tick(state)            # ← reverb body fires, reads mix_bus_l
        spu94_fir_interpolate(...)
```

### The fix

Move the `mix_bus_l/r` write from `spu94_process` into `chain_step_impl`, after the decimator, before the tick, on the retained phase:

```c
/* src/spu94/spu94_process.c -- remove these two lines: */
-       state->mix_bus_l = l;
-       state->mix_bus_r = r;

/* src/spu94/spu94_io_chain.c chain_step_impl -- add these inside if (dec_valid): */
+       state->mix_bus_l = dec_l;
+       state->mix_bus_r = dec_r;
```

And update the comments in both files to agree. The `spu94_io_chain.c:62-63` comment is already correct; the `spu94_reverb.c:606-608` comment needs revising to say "mix_bus_l/r carries the current 22.05 kHz reverb-rate input sample (decimator output), written by chain_step_impl on the retained phase before spu94_tick fires."

### Downstream consequences of the fix

1. **Every golden file will change bits.** The 50 WAV SHA256s will all drift. This is expected — the goldens were snapshots of the buggy behavior. After the fix, regenerate them via the CLI (which already uses 512 KB work buffer, so it's unaffected by the silent-drop class of bug) and commit the new SHAs.

2. **Witness-diff numbers will change.** Probably *improve* vs lv2-psx-reverb on HF content, since both libraries will now be running anti-aliased input instead of aliased. This is a good opportunity to land the long-deferred tolerance gate (D-06).

3. **Modulation harness numbers will change.** Again, these reflect actual DSP behavior rather than the aliased approximation. Re-run and commit.

4. **The `test_process_reverb_audible` (`tests/unit/process/test_process_reverb_audible.c:111-152`) regression gate landed by ADR-Phase-6-G may need its exact expected-values updated.** It asserts "non-silent output after 2 seconds of Hall"; that will still pass, but any specific-sample expected values inside it may shift by a few LSB.

5. **No ADR changes required.** ADR-Phase-6-G is about the output path; it doesn't need amendment. A new ADR (call it ADR-Phase-6-I or ADR-0021) should be written to document the *reason* this fix was needed: "Decimator output wiring corrected — reverb input now receives the FIR-decimated 22.05 kHz sample per Phase 4 intent, closing the path that was silently running on raw 44.1 kHz."

---

## Part 4 — Target Architecture (the Clean Shape)

### 4.1 Result code discipline

Current state: `spu94_result_t` has 4 codes (`OK`, `CLAMPED`, `UNKNOWN_REG`, `TYPE_MISMATCH`). NULL state on mutation returns `OK`. Undersized work buffer produces silent degradation, no code.

**Target:** extend the enum, and make NULL-state-on-mutation a caller bug, not a silent success.

```c
typedef enum {
    SPU94_OK                 = 0,
    SPU94_CLAMPED            = 1,  /* value saturated to fit register */
    SPU94_UNKNOWN_REG        = 2,  /* register id out of range */
    SPU94_TYPE_MISMATCH      = 3,  /* signed/unsigned accessor mismatch */
    SPU94_INVALID_STATE      = 4,  /* NULL state on a mutation call */
    SPU94_WORK_BUF_TOO_SMALL = 5,  /* work buf < preset's minimum */
    SPU94_INVALID_ARG        = 6   /* e.g., preset id out of range */
} spu94_result_t;
```

Append-only is preserved (D-07 invariant). Callers that ignore the return value continue to work. Callers that branch on `if (r != SPU94_OK)` now actually know about failures.

**NULL-state convention, revised:**
- **Observation calls** (get_reg, get_buffer_address, snapshot, state_size) — keep returning zero/`OK` on NULL. Observation is idempotent.
- **Lifecycle calls** (reset, destroy) — keep null-safe. Lifecycle idempotence is a real value.
- **Mutation calls** (set_reg_*, load_preset, tick) — return `SPU94_INVALID_STATE` on NULL. Mutation on no state is a caller bug, not a silent success.

### 4.2 Size contract at the boundary

Current: no way to ask "how much work buffer does this preset need?" Callers guess (CLI guesses 512 KB; Python guesses 8 KB and guesses wrong).

**Target:** publish the sizing contract.

```c
/* In spu94.h */

/* Return the minimum work_buf_size (bytes) required to safely run this
 * preset. Scans the preset's m*/d* delay-address registers, returns the
 * largest byte offset + 2. Deterministic, O(35) at load time. */
size_t spu94_preset_min_work_buf_size(spu94_preset_id_t id);

/* The maximum required work_buf_size across all 10 factory presets.
 * Callers that don't know in advance which preset they'll load can size
 * once against this and forget. Static-asserted against the preset table. */
#define SPU94_WORK_BUF_MAX_BYTES 0x40000u /* 256 KB — exact value to be
                                             computed from the preset table.
                                             Likely ~22 KB for Space Echo;
                                             round up to a power of two or
                                             match the PS1's 0x80000 SPU RAM. */
```

**Enforcement:**
- `spu94_load_preset` returns `SPU94_WORK_BUF_TOO_SMALL` if `state->work_buf_size < min_size`. State is NOT mutated on that return path — the caller can retry with a larger buffer.
- `spu94_init` does NOT enforce a minimum. Callers may init with any size including 0. A state that never sees `load_preset` or `tick` has no buffer requirement; enforcing at init would be over-eager.

**Python default:** set `SPU94()` and `api.init()` default to `SPU94_WORK_BUF_MAX_BYTES`. 256 KB is free on every host Python runs on. Callers who want smaller can pass smaller and will get an actionable error from `load_preset` if they undershoot.

**CLI:** replace the hardcoded 512 KB in `src/cli/main.c:153` with `SPU94_WORK_BUF_MAX_BYTES`.

**Fuzz harnesses:** replace the three independent 8192 / 256K constants (`fuzz_reverb.py:79`, `fuzz_buffer.py:63`, `fuzz_process.py`) with a single shared import from `tests/python/_constants.py` that uses the binding's published constant.

### 4.3 Observable error counters

Current: `state->overflow_magnitude` exists (Phase 3), `state->err_q15` / `err_input_scale` / `err_output_scale` exist (ADR-0004). None are read-accessible from outside.

**Target:** one public accessor returning a snapshot struct.

```c
/* In spu94.h */

typedef struct {
    int64_t overflow_magnitude;       /* cumulative hard-clip excursion */
    int64_t err_q15;                  /* cumulative Q15 multiply truncation */
    int64_t err_input_scale;          /* cumulative input-scale truncation */
    int64_t err_output_scale;         /* cumulative output-scale truncation */
    uint64_t oob_tap_count;           /* NEW: times reverb_buf_read/write clamped */
    /* append-only; field order stable */
} spu94_error_counters_t;

/* Read-only snapshot of the error counters. NULL state zeros the output. */
void spu94_get_error_counters(const spu94_state *state,
                              spu94_error_counters_t *out);

/* Reset all counters to zero. Does NOT affect audio state. */
void spu94_reset_error_counters(spu94_state *state);
```

**The OOB counter closes the silent-drop loop.** Internal tests (and self_test, and modulation harness) can now assert `counters.oob_tap_count == 0` after a block to prove the work buffer was adequate. Users can read it for diagnostics. The hot path stays silent (safe under rt-safety).

```c
/* src/spu94/spu94_reverb.c — reverb_buf_read */
static inline int16_t reverb_buf_read(spu94_state *s, uint32_t hw_off) {
    uint32_t byte_off = (hw_off << 1) & 0x7FFFEu;
    if ((size_t)byte_off + 1u >= s->work_buf_size) {
        s->err_oob_tap_count++;   /* NEW: observable */
        return 0;
    }
    /* ... normal path unchanged */
}
```

### 4.4 Reverb input routing

Already spelled out in Part 3. Two-line move. Update two comments. New ADR documenting the correction.

### 4.5 Witness-diff gating

Current: `witness_diff.py` prints numbers and exits 0. D-06 explicitly deferred the tolerance policy.

**Target:** land a loose gate. The gate doesn't have to be tight — "catches complete divergence" is infinitely better than "doesn't gate at all."

Per-preset × per-input thresholds, written into a committed JSON alongside `benchmark_baselines.json`:

```json
{
  "hall": {
    "impulse":     { "low_band_diff_dbfs_max": -20.0, "enabled": true  },
    "sine_1k":     { "low_band_diff_dbfs_max": -20.0, "enabled": true  },
    "frequency_sweep": { "enabled": false, "reason": "ADR-Phase-4-I excludes freq-response axis" }
  },
  ...
}
```

Any exceedance fails CI. Below the threshold (generous for noise-floor realism), passes. The threshold can tighten over time as we gain confidence.

**This needs its own ADR:** tolerance policy per preset, rationale for the chosen values, revision triggers.

### 4.6 Grep-guard scope

Current: scans `src/** include/**`. Fails on `src/cli/main.c:155` (malloc). CI never verified.

**Target:** two-tier guard.

```bash
# Tier 1: core library strict forbid (float|double|malloc|calloc|realloc|free|long)
TIER1_SCOPE="src/spu94 include/spu94"

# Tier 2: CLI narrower forbid (float|double|long) — malloc/free allowed
TIER2_SCOPE="src/cli"
TIER2_FORBID="\\b(float|double|long)\\b"

# Document the rationale in the script header:
# Core library: freestanding-C constraint, absolutely no heap, for MCU portability.
# CLI: allowed to use malloc (it's a userland tool that binds dr_wav), but not
# floating-point (bit-faithful determinism concern).
```

Then: push to GitHub, verify CI green, **commit the green CI run URL into MILESTONES.md** so "CI is green" is a citable fact rather than an assumption.

---

## Part 5 — Elegance Passes (Simplifications Worth Considering)

These are independent of the correctness fixes. They make the library smaller and easier to reason about. I recommend doing them in M1.x (a cleanup pass after the correctness fixes land) rather than rolling them into the close-out — they don't block M1 close, but they reduce the surface that has to survive into M2.

### 5.1 The 105-function register facade — is it actually used?

Phase 2 Plan 03 created `include/spu94/spu94_register_facade.h`: 35 setters + 35 active getters + 35 pending getters = 105 hand-written static-inline wrappers in the public header. Each wraps one engine-layer call with a pre-baked `SPU94_REG_*` argument.

**Observed usage:**
- The Python binding uses the **engine layer** (`spu94_set_reg_i16(state, SPU94_REG_vIIR, value)`), not the facade.
- The CLI's `json_config.c` uses the engine layer.
- The preset loader uses the engine layer internally.
- Tests use the engine layer.

Who uses the 105 facade functions? I haven't found a production consumer. It's a Phase 2 artifact that never found its audience.

**Simplification:** move the facade to an optional secondary header (`<spu94/spu94_register_facade.h>` explicitly opt-in, not auto-included by `<spu94/spu94.h>`). Or delete it entirely. Either halves the public header surface area (currently 105 inline functions, several hundred lines).

**Why this matters for elegance:** the public header is the library's front door. 105 functions nobody calls is a "front door that looks busier than it is" problem. A caller trying to understand "what does SPU-94 actually expose?" has to wade through the facade before reaching `spu94_process` and `spu94_load_preset`, which are the two functions 99% of callers will ever use.

### 5.2 Python binding — three layers, two might be enough

Current structure:

```
_binding.py   — ctypes.CDLL + argtypes/restype declarations, Register/Preset IntEnums
api.py        — "raw panel" functions: init(), destroy(), process(), load_preset(), ...
reverb.py     — SPU94 class that wraps api.py methods as instance methods
cli.py        — entry point shim
```

`api.py` adds almost nothing over `_binding.py` except Python-native signatures (default args, kwargs) and the numpy-contiguous-int16 enforcement. `reverb.py` adds almost nothing over `api.py` except `self._state` ownership.

**Simplification option A** (minimal): merge `api.py` into `_binding.py`. One module that exposes the raw panel with Python-native signatures. Keep `reverb.py` as the class wrapper.

**Simplification option B** (more aggressive): keep only `SPU94` + the enums as the primary surface. Move the raw panel to `spu94.raw.*` (explicit namespace). Most users want the class.

**Either way:** document the surface as two-axis — "raw panel" for power users and C-side-thinking developers; "SPU94 class" for most Python consumers. ADR-Phase-6-A already half-commits to this, but the implementation spreads across three modules when two would do.

### 5.3 Test-only public symbol

`spu94_fir_chain_step_reverb_bypass` is a public symbol (exported from `libspu94.so`, declared in a public header) that is only used by four FIR test files. It's documented as "test-only" but there's nothing stopping a plugin author from calling it and getting confused.

**Simplification:** move to `include/spu94/spu94_test_api.h`, which is NOT included by `spu94.h`. Tests include it explicitly. Production code has no way to see it. One line of CMake to install the header only in dev mode.

Or: guard with `#ifdef SPU94_EXPOSE_TEST_API` in the umbrella header, define that macro in the test build only, strip the symbol from the shipped `.so` via linker version script.

### 5.4 The `SPU94_PRESET__COUNT` / `SPU94_REG__COUNT` sentinel pattern

Double-underscore sentinels (`SPU94_REG__COUNT`, `SPU94_PRESET__COUNT`) are a C-community convention for "not a real value, iteration bound only." The facade uses them correctly. Some call sites use the explicit numeric range (`35`, `10`). Consistency pass: every iteration uses the sentinel; no magic numbers.

### 5.5 The internal-header proliferation

`src/spu94/` contains several internal headers:

- `spu94_state_internal.h` — the `struct spu94_state` definition
- `spu94_reverb_internal.h` — reverb sub-stages
- `spu94_fir_internal.h` — FIR sub-stages

Each is fine on its own. But `spu94_state_internal.h` is included by almost every TU in `src/spu94/`, and it's the big one (~150 lines). If we ever want to reduce include-time, the state definition could be behind a compiler flag for "tests that need struct access" versus "production code that treats the state as opaque even within the library." Currently production code accesses `state->foo` freely, which is correct for a library this size, but a cleaner pattern at M4 scale would use inline accessors even internally.

Not urgent. Flagging for M2+.

---

## Part 6 — Ordered Migration Plan

The shape that gets us to a clean M1 close:

### Stage 1 — Correctness fixes (BLOCKS M1 CLOSE)

Each commit atomic, each lands in its own branch, merged into `master` after review.

1. **Fix the grep-guard scope.** Two-tier (core strict, CLI narrower). Verify `bash scripts/ci/grep-guard.sh` returns 0 locally. Commit.

2. **Fix the reverb input wiring (CR-01).** Move `mix_bus_l/r` write from `spu94_process` into `chain_step_impl` using `dec_l/dec_r`. Update the two contradictory comments to agree. Write ADR-0021 documenting the correction. Commit.

3. **Add `spu94_preset_min_work_buf_size` + `SPU94_WORK_BUF_MAX_BYTES` + `SPU94_WORK_BUF_TOO_SMALL` result code.** Wire `spu94_load_preset` to validate. Update `spu94.h` and `spu94_presets.c`. Commit.

4. **Add `spu94_get_error_counters` + `oob_tap_count` field.** Wire `reverb_buf_read`/`write` to increment on OOB. Commit.

5. **Raise Python default to `SPU94_WORK_BUF_MAX_BYTES`.** Update `api.py`, `reverb.py`, all three fuzz harnesses to a shared constant. Commit.

6. **Tighten `self_test()` and `modulation_harness.py`.** Assert `counters.oob_tap_count == 0` after each preset run. Non-silence floor check. RMS comparison vs a known-good reference signature (precomputed and committed). Commit.

7. **Fix the NULL-state-on-mutation pattern.** `spu94_load_preset(NULL, ...)` returns `SPU94_INVALID_STATE`, not `SPU94_OK`. Update header contract comment. Update tests. Commit.

8. **Address remaining critical findings from the three review reports** — CLI's 24-bit WAV silent-downconvert (`wav_io.c`), `main.c` UB on `--tail-seconds` cast, `api.destroy()` not nulling the handle, etc. Each a separate small commit.

9. **Regenerate the 50 golden files.** Commit the new WAVs + SHAs. Write a short note in `.planning/v1.0-GOLDENS-REGEN.md` citing ADR-0021 as the reason.

10. **Regenerate modulation harness results.** Commit the new `modulation_report.json`.

11. **Push to GitHub Actions. Observe CI green. Commit the CI run URL into MILESTONES.md.**

### Stage 2 — Verification tightening (BLOCKS M1 CLOSE)

12. **Land the witness-diff tolerance gate.** Write ADR-Phase-7-I (or equivalent). Commit `witness_diff_thresholds.json`. Wire `witness_diff.py` to fail CI on exceedance. Verify CI still green.

13. **Land external-anchor tests.** Hand-computed expected output for a single known impulse through Hall (or Off, or the simplest preset where ground truth is tractable). A single `TEST_ASSERT_EQUAL` on 8 expected samples is enough to catch "bits are reproducible but to garbage."

14. **Write Phase 6 VERIFICATION.md** from the existing UAT + SUMMARY evidence. Closes the 11 Phase 6 requirements' 3-source audit gap.

15. **Re-run the milestone audit.** Status should flip to `passed`.

### Stage 3 — Elegance pass (M1.x, optional but strongly recommended before M2)

16. Delete or optional-namespace the 105-function facade.
17. Merge `api.py` into `_binding.py`, or move `api` to `spu94.raw`.
18. Move `spu94_fir_chain_step_reverb_bypass` to a test-only header.
19. Consistent sentinel usage everywhere.

### Stage 4 — Close the milestone

20. `/gsd-complete-milestone v1.0` with all gaps resolved. Git tag. Push.

21. Kick off M2 (ADPCM) with a clean foundation.

### Phase 8 (MCU cross-compile) stays parked

As decided: Phase 8 moves to between M4 and M5. Do NOT attempt to close it as part of M1. The BUILD-03 gap is deliberate; MILESTONES.md records it explicitly.

---

## Part 7 — What NOT to Change (the Good Stuff)

The reviewers agreed on these. Don't touch them during remediation — they're load-bearing correctness you'd have to rebuild from scratch.

- **The Q15 math primitives** (`include/spu94/spu94_q15.h`). `q15_mul_truncate`, `sat_s16`, `q15_add_sat`, the `err_out` tap, the ADR-0001 ASR static_assert. Clean, tested, edge-case correct.

- **The FIR overflow-width proof.** The analytic derivation in `spu94_fir.c`, the 2.791 dB / 0.464 bits headroom disclosure in ADR-0014, the 10^6-step `fuzz_fir.py` runtime regression. This is a rare "we proved it, then we tested the proof" artifact.

- **The 35-entry write-policy table** (`spu94_write_policy.c`). Clean split between IMMEDIATE and TICK_LATCHED, swappable seam (D-05), well-tested. Don't touch.

- **The register facade *pattern*** (even if we move it out of the primary header). The pattern is good — it makes the ergonomic API match the enum. Just relocate, don't redesign.

- **The state lifecycle.** `init`/`reset`/`destroy` are freestanding-C correct, the hand-rolled zero-loop is deliberate, the `spu94_state` sizing is well-guarded. The only issue is the "NULL-state on mutation returns OK" convention — that's a policy fix, not a lifecycle rewrite.

- **The Docker reproducibility pin.** SHA256-digest pinning is genuinely excellent (rare in OSS). Keep the Dockerfile exactly as-is; just document the amd64 assumption.

- **The strace-based hot-path allocation gate + its WILL_FAIL negative test.** The meta-test (Phase 7 Plan 05) proves the gate catches what it claims. Don't touch — just tighten the exit-code discipline per WR-04.

- **The preset-sourcing three-source audit** (`verify_preset_sources.py`, BIB-011 vs BIB-012 cross-check). Keep.

- **The benchmark harness with committed baselines** (`benchmark_baselines.json`). Good pattern. Keep.

- **`fuzz_buffer.py`'s independent-Python-model vs C comparison.** Better than pure property-test because it actually *simulates* the expected behavior, not just checks invariants. Keep. Extend this pattern elsewhere if possible.

---

## Part 8 — Verification Strategy

After Stage 1 lands, the following must all be true before proceeding to Stage 2:

- [ ] `bash scripts/ci/grep-guard.sh` exits 0.
- [ ] `ctest --test-dir build --output-on-failure` passes 100% (all 66+ ctest targets).
- [ ] `python -m spu94.api` `self_test()` passes AND `counters.oob_tap_count == 0`.
- [ ] Every golden file regenerates byte-identically across host + Docker (reproducibility).
- [ ] Every preset loads with the new `SPU94_WORK_BUF_MAX_BYTES` default and produces audible (non-silent) reverb on a noise input without any `oob_tap_count` increment.
- [ ] `spu94_load_preset(NULL, 0)` returns `SPU94_INVALID_STATE`, not `SPU94_OK`.
- [ ] `spu94_load_preset(state, 5)` with a 4 KB work buffer returns `SPU94_WORK_BUF_TOO_SMALL` and does NOT mutate state.
- [ ] Manual listening test: render a drum loop with cymbals through Hall, compare pre-fix vs post-fix. Pre-fix should have audible aliasing on the cymbal tails; post-fix should be cleaner. (This is the empirical confirmation that CR-01's fix matters, not just on paper.)
- [ ] CI runs green on GitHub Actions for the first time.

After Stage 2:

- [ ] Witness-diff CI job fails when a preset × input exceeds its threshold (tested by deliberately introducing a small deviation in a test branch).
- [ ] Hand-computed expected-output test passes for the known impulse.
- [ ] `/gsd-audit-milestone v1.0` returns status `passed`.

---

## Part 9 — Handoff to the Next Session

A fresh Claude session can start from this document. The ordered plan in Part 6 is the execution backbone. The standing memories (in `~/.claude/projects/-home-ubuntu-studio-Desktop-PSX-Reverb/memory/MEMORY.md`) will provide project context and preferred collaboration style.

**Key entry points for the next session:**

1. Read this file (`.planning/ARCHITECTURAL-AUDIT.md`) in full.
2. Read the three review reports: `.planning/REVIEW-c-core.md`, `.planning/REVIEW-cli-python.md`, `.planning/REVIEW-tests-ci.md` for individual-finding detail.
3. Read `.planning/v1.0-MILESTONE-AUDIT.md` for the broader milestone readiness context (produced earlier in this session).
4. Execute Stage 1 steps in order. Each step is a direct inline fix with an atomic commit — no new phases, no discuss cycles, no ceremony (per Anthony's `feedback_no_ceremony_on_bugs.md`).
5. Each change to durable planning artifacts (PROJECT.md, MILESTONES.md, DECISIONS.md) should be announced to Anthony before editing (per `feedback_review_before_official_writes.md`).
6. Every technical topic introduced to Anthony should lead with a plain-language analogy before the code-side vocabulary (per `feedback_plain_language_short.md`).

**What this document does NOT contain:**

- Phase-8 (MCU cross-compile) plan. That stays parked per Anthony's 2026-04-24 decision.
- M2 (ADPCM) planning. Kicks off after M1 close.
- M4 plugin scaffolding. Not started, not scoped here.

**The one open question that requires Anthony's confirmation before proceeding:**

Whether to adopt the elegance passes (Part 5) *before* M1 closes, or *after* M1 closes in a separate M1.x cleanup milestone. My recommendation is **after** — Stage 1 + Stage 2 + Stage 4 close M1 on correctness; the elegance passes are a separate, low-risk cleanup that wouldn't block M2 but would make M2 easier to build cleanly on top of.

---

## Appendix A — Cross-reference: Review findings → this document's sections

| Review | Finding | Root Cause in this doc |
|--------|---------|------------------------|
| C-core | CR-01 (reverb input path) | Root Cause #2 / Part 3 |
| C-core | HI-01, HI-02, HI-03, HI-04 | Root Cause #1 / Part 4.2, 4.3 |
| C-core | ME-01 through ME-05 | Defense-in-depth, Part 4 details |
| C-core | LO-01, LO-02, LO-03, NT-01, NT-02 | Elegance pass Part 5.4 |
| CLI-Python | C-01 (24-bit WAV silent downconvert) | Root Cause #1 / Stage 1 step 8 |
| CLI-Python | C-02 (main.c UB + unbounded tail) | Stage 1 step 8 |
| CLI-Python | H-01 (work_buf defaults) | Root Cause #1 / Part 4.2 |
| CLI-Python | H-02 (self_test false-confidence) | Root Cause #1 / Stage 1 step 6 |
| CLI-Python | H-03 (destroy() leaves dangling handle) | Stage 1 step 8 |
| CLI-Python | H-04..H-06 | Stage 1 step 8 |
| Tests-CI | CR-01 (silent drop in C core) | Root Cause #1 / Part 4.3 |
| Tests-CI | CR-02 (Python default) | Root Cause #1 / Part 4.2 |
| Tests-CI | CR-03 (stability tests on degraded audio) | Root Cause #3 / Stage 1 step 6 |
| Tests-CI | CR-04 (test_init_success pinning 64-byte buf) | Stage 1 step 3 side-effect |
| Tests-CI | CR-05 (witness-diff no gate) | Root Cause #3 / Part 4.5 |
| Tests-CI | CR-06 (goldens only as good as CLI) | Root Cause #3 / Stage 2 step 13 |
| Tests-CI | WR-01 through WR-13 | Stage 1 step 8, Stage 2 step 12 |

---

## Appendix B — File inventory (what M1 actually shipped)

**C core:** 27 files, 4,715 LOC.
- 4 public headers: `spu94.h`, `spu94_q15.h`, `spu94_registers.h`, `spu94_register_facade.h`
- 3 internal headers: `spu94_state_internal.h`, `spu94_reverb_internal.h`, `spu94_fir_internal.h`
- 20 TUs: state, registers (enum + names + offsets + types), register_io, write_policy, pending, tick, buffer, reverb (1 large file), reverb sub-stages folded into reverb.c, fir, fir_coef, io_chain, process, presets

**CLI:** 7 files, 1,119 LOC.
- `main.c`, `json_config.c` + `.h`, `wav_io.c` + `.h`, `preset_names.c` + `.h`
- Vendored `dr_wav.h` + `jsmn.h` (not counted)

**Python binding:** 6 files, 1,344 LOC.
- `__init__.py`, `_binding.py`, `api.py`, `reverb.py`, `presets.py`, `cli.py`

**Tests:** 86 files, 47,168 LOC (test code — heavier than production by 4x, as expected for bit-faithful claims).

**CI + scripts:** 2,754 LOC across ~20 files.

---

*End of audit. Ready for handoff.*
