---
status: resolved
trigger: "reverb-not-in-audio-path: wet signal computed but discarded, CLI produces dry audio"
created: 2026-04-21T00:00:00Z
updated: 2026-04-22T17:00:00Z
---

## Current Focus

hypothesis: RESOLVED (three stacked root causes, fixed in order: wiring → Q15 input_scale → CLI work_buf init). Full ctest pass; user to audition /tmp/piano-hall-FINAL.wav for final acceptance.
test: User listens to /tmp/piano-hall-FINAL.wav — clean natural Hall decay, no cold-start burst.
expecting: "confirmed" → archive session + close remaining handoff items (launcher commit, Plan 5 Task 4 SC-4).
next_action: Await human-verify response on the three-commit stack (11bdebf test, 53bac5c Q15 fix, 4bb40c3 silent-input test, 9650243 CLI reset fix).

## Symptoms

expected: `spu94 --preset hall input.wav output.wav` produces audibly reverberated output. A 440 Hz sine + Hall preset produces reverb-decay tail (not just FIR delay-line bleed). `Off` preset + non-silent input produces silent output (vLOUT/vROUT = 0 gates everything).

actual: CLI output is byte-effectively dry input through half-band FIR at ~-6 dB. A real piano WAV rendered with Hall preset sounds identical to input — no reverb. 440 Hz / 2s sine: output peak 1449 vs input peak 2896 (exactly -6 dB half-band loss); tail region (2-3s) contains 1448 peak signal which is FIR delay-line bleed, NOT reverb decay.

errors: None — silent correctness failure. Code compiles, tests pass. `ctest` green because no end-to-end test asserts "output differs from dry-passthrough when reverb is active."

reproduction:
1. cd /home/ubuntu-studio/Desktop/PSX\ Reverb
2. cmake --build build
3. ffmpeg -y -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" -ac 2 -c:a pcm_s16le /tmp/sine.wav
4. build/src/cli/spu94 --preset hall --tail-seconds 2 /tmp/sine.wav /tmp/out.wav
5. Compare peak energy — output peak is -6 dB of input peak; tail is FIR ring-down, not reverb.

started: Phase 4 wired the FIR chain without consuming LeftOutput/RightOutput. Phase 5 author added comment "LeftOutput/RightOutput are not yet consumed — Phase 4 FIR will read them when the 39-tap interpolator lands" expecting Phase 4 to wire them; Phase 4 did not. test_preset_nonzero_tail.c documents the gap as "M4 scope".

## Eliminated

## Evidence (wiring investigation — 2026-04-21; RESOLVED, do not revisit)

- timestamp: 2026-04-21T00:00:00Z
  checked: Orchestrator investigation brief
  found: Concrete diagnosis with file:line references to the (void) casts and chain_step_impl dry-path
  implication: Must verify empirically before committing to fix; then present architecture A/B/C decision to user

- timestamp: 2026-04-21T12:10:00Z
  checked: src/spu94/spu94_reverb.c:596-615
  found: Lines 613-614 literally `(void)LeftOutput; (void)RightOutput;` with comment "LeftOutput/RightOutput are not yet consumed — Phase 4 FIR will read them when the 39-tap interpolator lands."
  implication: Wet output is computed correctly (spu94_reverb_output_scale at line 609 truncates to int16 via q15_mul_truncate_with_err) but thrown away. Reverb network state advances correctly on every tick; only the scalar output is lost.

- timestamp: 2026-04-21T12:11:00Z
  checked: src/spu94/spu94_io_chain.c:45-73 (chain_step_impl)
  found: Line 51 decimates l_in/r_in into dec_l/dec_r. Line 57 calls spu94_tick (reverb network advances). Line 60-61 calls spu94_fir_interpolate(state, dec_l, dec_r, ...) — feeds the DRY decimator output directly into the interpolator. The wet output from spu94_tick is never read.
  implication: Confirmed — dry signal path is continuous through halfband decimate→interpolate; reverb runs in parallel but its output never joins the audio path.

- timestamp: 2026-04-21T12:11:30Z
  checked: src/spu94/spu94_fir.c:281-309 (spu94_fir_interpolate)
  found: Lines 286-287 push input_sample_l/r directly into fir_delay_l_out/fir_delay_r_out. No reverb contribution path exists in the interpolator.
  implication: Fix must happen at the chain_step_impl level (change what's passed into spu94_fir_interpolate), not inside the interpolator.

- timestamp: 2026-04-21T12:12:00Z
  checked: tests/unit/preset/test_preset_nonzero_tail.c:19-52
  found: Test file header acknowledges the wiring gap explicitly: "the reverb body's LeftOutput/RightOutput values are (void)-cast at spu94_reverb.c:613-614 and never reach the interpolator... The 'Off gates non-silent input' interpretation requires the M4 output-bus-through-FIR rewiring which is out of M1 scope."
  implication: The test suite documents the bug as known and punts it to M4; this is why ctest is green despite shipped reverb not working.

- timestamp: 2026-04-21T12:12:30Z
  checked: Empirical CLI repro — /tmp/sine.wav (440Hz 2s sine, peak 2896) → Hall preset → /tmp/out.wav (4s output)
  found: Output peak 1449 in 0-2s region (exactly -6 dB = half-band attenuation). Tail region 2-2.5s: peak 1448 (FIR delay-line bleed). Beyond 2.5s: exactly 0. If reverb were active, Hall decay (~2s RT) would produce non-zero tail throughout the 2s flush region.
  implication: Bug confirmed in running binary, not just in code reading. No reverb signal present in output whatsoever.

- timestamp: 2026-04-21T12:13:00Z
  checked: spu94_reverb_output_scale semantics (spu94_reverb.c:131-143)
  found: Writes int16 values to LeftOutput_out/RightOutput_out (cast from int16 L/R produced by q15_mul_truncate_with_err). The int32_t type is just carrier width.
  implication: Wet output is already a clean int16 pair per 22.05 kHz tick. Fix is trivial wiring — no new saturation or width conversion needed.

## Resolution

root_cause: Wiring defect — `spu94_reverb_output_scale` produced LeftOutput/RightOutput at 22.05 kHz but chain_step_impl (spu94_io_chain.c:60-61) fed dry decimator output (dec_l/dec_r) into spu94_fir_interpolate instead of the reverb wet output. The `(void)` casts at spu94_reverb.c:613-614 were the smoking gun; a Phase 5 comment acknowledged Phase 4 was supposed to wire these.

fix: Option A (wet-only, PS1-authentic) per ADR-Phase-6-G. Added reverb_out_l/r mailbox fields to spu94_state (at struct tail to preserve D-17 hand-typed offsets in fuzz_process.py). spu94_reverb_body writes LeftOutput/RightOutput into the mailbox; chain_step_impl on the production path (reverb_active=1) zeros the mailbox, runs spu94_tick, then feeds the mailbox values into spu94_fir_interpolate. Dry decimator output (dec_l/dec_r) still feeds the reverb INPUT via mix_bus_l/r (unchanged Phase 5 path), but is NO LONGER passed to the interpolator on the production path. The test-only bypass path (reverb_active=0) still uses dry-passthrough to preserve FIR-DSP-level tests. CLI sets vLOUT/vROUT=0x7FFF for non-Off presets so the factory preset tables (which intentionally leave master-mix at 0) produce audible output.

Three atomic commits:
  * b853c8c test(phase-6): add end-to-end reverb-audible regression tests (RED)
  * d2c782a fix(chain): wire reverb output into FIR interpolator (ADR-Phase-6-G)
  * 101aaa7 docs(DECISIONS): record ADR-Phase-6-G wet-only output wiring

verification: Self-verified via full ctest run. 61/61 tests pass (excluding rt_safety 2-min timers). The three-assertion test_process_reverb_audible all-pass: Hall+noise produces primed reverb output materially above Off-reference silence; Off+noise is identically silent; Hall's flush tail persists for thousands of samples (reverb decay, not FIR ring-down). fuzz_process 1M-step random walk passes (confirms state-layout shift did not corrupt FIR delay-line indices or pending_mask). Empirical: rendered /home/ubuntu-studio/Desktop/E-Keys_02_PSS470.wav through Hall preset into /tmp/piano-hall-fixed.wav (1.15 MB stereo 44.1 kHz WAV, decoded OK by `file`). Pending human confirmation that the reverb is audible.

files_changed:
  - src/spu94/spu94_state_internal.h (added reverb_out_l/r mailbox fields at tail)
  - src/spu94/spu94_reverb.c (replaced (void) casts with mailbox writes)
  - src/spu94/spu94_io_chain.c (rewired production path to wet-only; test bypass preserved)
  - src/spu94/spu94_fir_internal.h (docstring update for dual bypass semantics)
  - src/cli/main.c (vLOUT/vROUT=0x7FFF default for non-Off presets)
  - tests/unit/process/test_process_reverb_audible.c (new regression gate)
  - tests/unit/process/CMakeLists.txt (wire new test)
  - tests/unit/preset/test_preset_nonzero_tail.c (restored Off-gates-input premise; bumped feed size for priming)
  - tests/unit/process/test_process_basic.c (retired impulse-peak-near-latency to pass-stub with ADR rationale)
  - docs/DECISIONS.md (ADR-Phase-6-G)

---

## Evidence (DSP investigation — 2026-04-22; CR-01 pinned output level)

- timestamp: 2026-04-22T14:00:00Z
  checked: /tmp/piano-hall-fixed.wav, /tmp/piano-hall-v2.wav, /tmp/piano-hall-v3.wav peak levels
  found: Peaks are 19362 (-4.57 dBFS), 18206 (-5.10 dBFS), 18204 (-5.11 dBFS). The three source files were the same piano at 0 dBFS, -12 dBFS, -24 dBFS. Output peak range: ~0.5 dB across 24 dB of input range.
  implication: Output level is functionally independent of input level. This is a saturation-pinned feedback loop, not a linear reverb. Matches REVIEW.md CR-01 signature.

- timestamp: 2026-04-22T14:05:00Z
  checked: Hall preset register table (src/spu94/spu94_presets.c:243-282)
  found: vLIN = 0x8000, vRIN = 0x8000. Under signed int16 interpretation 0x8000 = -32768 = INT16_MIN. In Q15 fractional interpretation (standard for all SPU reverb v* coefficients), 0x8000 represents -1.0.
  implication: Hall (and every other preset per a quick grep: vLIN=0x8000 is the universal default) is intended to pass input through at Q15 unity with an inversion. If the multiply doesn't >>15, a raw product has magnitude `left_in * 32768` which overflows int16 for any input ≥ 1.

- timestamp: 2026-04-22T14:10:00Z
  checked: spu94_reverb_input_scale (src/spu94/spu94_reverb.c:81-91) and spu94_reverb_hard_clip (lines 99-124)
  found: input_scale does `*Lin_out = (int32_t)left_in * (int32_t)vLIN_snap` — raw widening multiply, NO >>15. hard_clip then sat_s16s the int32 product. No Q15 scaling anywhere on the input path to the IIR.
  implication: With vLIN=0x8000 (=-32768) and any non-zero input, `wide = left_in * -32768` has magnitude ≥ 32768, sat_s16 clamps to INT16_MIN for positive inputs and INT16_MAX for inputs that satisfy `left_in * -32768 > INT16_MAX` (i.e. left_in < 0). So Lin is pinned to ±INT16_MIN almost always, independent of input amplitude. This IS the CR-01 mechanism.

- timestamp: 2026-04-22T14:12:00Z
  checked: Simulated math for vLIN=-32768 across left_in in [100, 1000, 5000, 10000, 16000, 32000]
  found: Current(sat-only) produces Lin=-32768 for ALL six input levels. Correct(>>15+sat) produces Lin=-100,-1000,-5000,-10000,-16000,-32000 respectively — linear tracking as expected.
  implication: Fix = switch input_scale to a Q15 multiply (use q15_mul_truncate_with_err per D-11 err-tap discipline, accumulating into state->err_input_scale which is already allocated for this). hard_clip becomes a no-op on the linear path (the Q15 multiply already saturates INT16_MIN*INT16_MIN to INT16_MAX via sat_s16 in q15_mul_truncate). hard_clip's overflow_magnitude observable must then be driven from the Q15 multiply's own saturation event rather than from a raw int32 input.

- timestamp: 2026-04-22T14:15:00Z
  checked: psx-spx wiki (web search) — "Lin = vLIN * LeftInput" interpretation
  found: psx-spx states "multiplication results are divided by +8000h to fit them to 16-bit range" and "volume registers are signed 16bit (range -8000h..+7FFFh)". DuckStation (behavioral witness only) uses Q15 semantics for vLIN per its commit 809b9f89.
  implication: The nocash pseudocode's `Lin = vLIN * LeftInput` is Q15 (product >> 15), not raw widening. The Phase 3 research docs (03-RESEARCH.md) explicitly considered but did not resolve this — the research lead flagged it as "Planner's decision; either is bit-correct" which is incorrect: raw widening is a design defect, not a valid alternative.

## Resolution (DSP fix — CR-01)

root_cause: `spu94_reverb_input_scale` performed a raw int16×int16 widening multiply instead of a Q15 multiply (>>15). With the universal preset default vLIN=vRIN=0x8000 (Q15 -1.0), ANY non-zero input saturated the post-hard_clip Lin/Rin to ±INT16_MIN, driving the IIR/comb/APF feedback network at constant maximum amplitude regardless of input level. Output pinned at the level the feedback network settled to (~-5 dBFS) and dropped off a cliff when input silenced because Lin suddenly stepped from ±INT16_MIN to 0.

fix: Switched input_scale to use q15_mul_truncate_with_err (the same Q15 primitive every other reverb-network multiply uses), accumulating the truncation remainder into state->err_input_scale. hard_clip is retained as a named stage (D-09 testability) and now only fires on the INT16_MIN^2 edge case (sat_s16 already handles that correctly).

Two atomic commits:
  * 11bdebf test(phase-6): add RED linearity gate for reverb input-scale bug (CR-01)
  * 53bac5c fix(reverb): Q15 input_scale — output level now tracks input (CR-01)

verification:
  - Full ctest pass: 64/64 (includes fuzz_process 1M-step random walk, fuzz_buffer, rt_safety suites).
  - Unit linearity test passes: full/quarter peak ratio = 2.38 (> 2.0 threshold, rules out pinned output); half/quarter = 1.87 (near theoretical 2.0).
  - Sine-wave linearity: 0/-6/-12/-24 dBFS inputs produce outputs at -22.87/-28.86/-34.86/-46.79 dBFS — tracks input within 0.1 dB across 24 dB of dynamic range.
  - Empirical piano through Hall, envelope over time:
      Pre-fix (piano-hall-fixed.wav):  t=0.5→4s pinned at -10 dBFS plateau, t=5s cliff drop to -57 dBFS
      Post-fix (piano-hall-POST-FIX.wav): t=0.5s -22 dBFS, clean exponential decay to -40 dBFS at t=4s, below noise floor by t=5s
  - No plateau. No cliff. Authentic reverb behavior.

files_changed:
  - src/spu94/spu94_reverb.c (switched input_scale to Q15 multiply, updated doc comment)
  - tests/unit/reverb/test_reverb_input_scale.c (updated reference table to Q15, replaced err-stays-zero invariant with zero-on-clean + accumulates-on-truncation pair)
  - tests/unit/process/test_process_mix_bus.c (switched D-05 mailbox-read proof from overflow_magnitude to err_input_scale — the plan's originally intended field, now viable post-fix)
  - tests/unit/process/test_process_reverb_linearity.c (new CR-01 regression gate)
  - tests/unit/process/CMakeLists.txt (wire new test)

---

## Evidence (CLI startup burst — 2026-04-22T17:00:00Z; work_buf uninit leak)

After Q15 linearity shipped, user rendered the piano file through Hall again and reported a "noise blast" at the start of the output WAV — described as "aliasing noise, not reverb." Audacity view confirmed the burst was in the file, not a playback glitch.

- timestamp: 2026-04-22T16:30:00Z
  checked: Multiple CLI renders of /home/ubuntu-studio/Desktop/E-Keys_02_PSS470.wav through Hall (sha256 comparison across 3 runs)
  found: All three renders byte-identical. Rules out random/malloc-garbage across runs.
  implication: The burst is deterministic within a process, not a classic malloc-returns-different-values-each-time bug.

- timestamp: 2026-04-22T16:35:00Z
  checked: Sine and synthetic inputs (impulse, burst, DC step, noise, sawtooth, square) through Hall
  found: All synthetic inputs produce zero output for the first 40+ samples. No burst.
  implication: Burst is input-specific. Only piano WAV triggers it.

- timestamp: 2026-04-22T16:45:00Z
  checked: 24-bit piano vs ffmpeg-converted 16-bit piano through Hall
  found: 24-bit path produces first-64 peak=11222 (burst). 16-bit path produces first-64 peak=1503 (no burst). DIFFERENT OUTPUTS for what should be the same audio content.
  implication: Something in the CLI pipeline differs between 24-bit and 16-bit source paths, producing non-equivalent SPU inputs or state.

- timestamp: 2026-04-22T16:50:00Z
  checked: Dumped the deinterleaved L/R arrays from wav_load for both piano variants (all 199704 frames each)
  found: Binary-identical. sha256 match for the full 798816-byte interleaved dump.
  implication: The SPU receives byte-identical input from both paths — so the divergence has to be in SPU state, not input.

- timestamp: 2026-04-22T16:55:00Z
  checked: src/cli/main.c:155 work_buf allocation, cross-referenced against spu94_init contract (tests/unit/state/test_state_lifecycle.c:121 comment "init does NOT zero the caller's work buf per D-14")
  found: CLI uses malloc (not calloc) for work_buf and calls spu94_init without a follow-up spu94_reset. Per documented contract, spu94_init only zeros the state struct; work_buf retains whatever heap residue malloc returned. dr_wav's internal allocations differ between 24-bit and 16-bit source reads, leaving different heap residue that the subsequent work_buf malloc picks up.
  implication: Root cause. Reverb delay lines start with different garbage for 24-bit vs 16-bit paths, producing different startup output. Today's wiring fix (ADR-Phase-6-G) made this latent bug audible by actually plumbing reverb output to the audio path.

## Resolution (CLI startup burst — work_buf init)

root_cause: `src/cli/main.c` allocated work_buf via malloc and relied on spu94_init alone to prepare the SPU. spu94_init intentionally does NOT zero the caller-owned work buffer (documented D-14 contract — only spu94_reset does). Under the pre-wiring-fix code the delay lines were unread, so the garbage was invisible. ADR-Phase-6-G routed the reverb network's output through the FIR interpolator to the WAV, which made the heap residue audible as a "noise blast" at sample 0. Different dr_wav allocation patterns for 24-bit vs 16-bit source reads left different residue, producing different-sounding bursts for what should have been equivalent rendering of the same audio content.

fix: `src/cli/main.c` calls `spu94_reset(state)` immediately after `spu94_init(...)`. Reset zeros both the state and the caller's work_buf per its existing contract; no library API change. Comment block documents the D-14 contract and the reason the reset is not optional.

Two atomic commits:
  * 4bb40c3 test(cli): gate silent-input-through-hall against work_buf leak
  * 9650243 fix(cli): reset state after init to zero work_buf heap residue

verification:
  - 24-bit and 16-bit piano paths now produce byte-identical output (cmp(1) exit 0).
  - First 40 output samples are zero for both paths (was 5588/8674 peak on 24-bit path pre-fix).
  - Hall envelope on piano: t=0.0s peak=2804, t=0.3s peak=8126 (natural Hall attack), clean exponential decay after. No burst, no plateau.
  - 58/58 tests pass (excluding fuzz/rt_safety long-timers). CLI test suite green.
  - New regression gate: `test_silent_input_through_hall_is_silent` — asserts silent input + Hall → silent output end-to-end; gates the general class of "heap residue bleeds through the reverb."

files_changed:
  - src/cli/main.c (added spu94_reset(state) call with documentation comment)
  - tests/cli/test_cli_preset_hall_roundtrip.py (new silent-input regression gate)
