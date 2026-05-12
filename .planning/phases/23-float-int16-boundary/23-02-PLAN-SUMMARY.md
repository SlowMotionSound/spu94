---
phase: 23-float-int16-boundary
plan: 02
subsystem: plugin/boundary
tags: [feature, plugin, input-gain, boundary, ux]
dependency_graph:
  requires:
    - 23-01   # spu94::plugin::boundary::{toInt16, toFloat} named seam this plan pushes Input Gain through
  provides:
    - "spu94::plugin::boundary::applyInputGain(float, float)"      # pre-clamp gain helper
    - "SPU94AudioProcessor::kInputGainDefault = 0.5f"               # public anchor for Phase 24 GUI wiring
    - "SPU94AudioProcessor::kInputGainMax = 16.0f"                  # public anchor for Phase 24 GUI wiring
    - "inputGainScratch_[2] pre-clamp host-rate float scratch on processor"
  affects:
    - src/plugin/BoundaryConverter.h     # third inline helper added (applyInputGain)
    - src/plugin/PluginProcessor.h       # inputLevel comment widened to 0.0..16.0 semantics; constants + scratch added
    - src/plugin/PluginProcessor.cpp     # pre-clamp gain wired on BOTH paths; engine register pinned at 0x7FFF on BOTH paths (UAT outcome)
    - src/plugin/PluginEditor.cpp        # standalone GUI slider widened to 0.0..16.0 with unity at knob midpoint (UAT correction; in-scope per v1.7 standalone-as-testbed)
tech_stack:
  added: []
  patterns:
    - "pre-clamp float gain stage at the plugin boundary (host-rate, before SRC sandwich, before BoundaryConverter::toInt16's clamp)"
    - "boundary clamp (sat_s16) deliberately used as a saturator/overdrive stage when gain > unity"
    - "atomic-load-once-per-block pattern for parameter reads inside the audio callback (matches existing dryLevel/wetLevel pattern)"
key_files:
  created: []
  modified:
    - src/plugin/BoundaryConverter.h
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.cpp
decisions:
  - "D-01 + D-02 walked: Input Gain repositioned from a post-clamp Q15 engine-register multiply to a pre-clamp host-rate float multiply applied BEFORE the SRC sandwich and BEFORE BoundaryConverter::toInt16's clamp. Sub-unity values give real headroom (0.5 = -6 dB); super-unity values up to ~16.0 drive sat_s16 deliberately, turning the boundary into a saturator/overdrive (North Star: fixed-point quirks ARE the product)."
  - "D-03 resolved with a POST-UAT amendment: engine register `spu94_set_input_gain` pinned at unity (0x7FFF) on BOTH paths (plugin AND standalone), not wrapperType-conditional as the plan-as-written specified. Reason: with the atomic range widened to 0.0..16.0, the standalone path's existing `static_cast<int16_t>(inputLevel * 0x7FFF)` Q15 cast overflowed int16_t at any value > 1.0 and wrapped to a negative number. The wrapperType-conditional design was incompatible with the widened range. Both paths now share the pre-clamp float multiply, and the register is uniformly pinned at unity."
  - "Standalone GUI slider range widened in-phase to 0.0..16.0 with `setSkewFactorFromMidPoint(1.0)` so unity sits at the knob midpoint. The plan-as-written had treated GUI exposure as Phase 24 (PLUG-28) work; UAT proved this was needed in-phase to make the new range testable on the standalone testbed (which is internal dev-only per v1.7). The Phase 24 / PLUG-28 work — host-automation `AudioProcessorParameter` surface for the plugin formats — is still future work; the standalone GUI slider and the plugin-format host-param surface are different code paths."
  - "Multiply form: scalar loop calling `spu94::plugin::boundary::applyInputGain` per sample (plan offered scalar-loop OR `juce::FloatVectorOperations::multiply`; executor picked scalar-loop for grep-ability via the named seam and for symmetry with the standalone-path per-sample toFloat/toInt16 round trip)."
  - "Buffer strategy: per-channel `juce::HeapBlock<float> inputGainScratch_[2]` allocated to kMaxBlock=4096 in prepareToPlay; processBlock writes scratch and hands scratch pointers to `srcChain_.processIn` (host buffer is never mutated). Same pattern as existing scratch members. R1/R3/R4 all addressed."
  - "Engine-register policy is now UNIFORM (post-UAT amendment): `spu94_set_input_gain(engines[0], 0x7FFF)` on both per-block atomic-sync (:284) and savePresetToString sync (:553). Consequence: plugin-saved AND standalone-saved presets now carry `input_gain = 0x7FFF` in the engine state. R2's documented cross-path divergence narrows: there is no longer cross-path divergence in this plan (both paths share the same register policy), but old v1.6 presets with stored `input_gain != 0x7FFF` are still effectively no-ops because both paths now ignore the register and use the GUI atomic. Acceptable per the D-03 framing."
metrics:
  duration: "~2h 22m (3 commits across 2 sittings: Tasks 1+2 in one autonomous run, UAT correction in a second after Anthony's hands-on Ardour sweep)"
  completed: "2026-05-11"
  tasks: 3
  files: 4
  commits: 3
---

# Phase 23 Plan 02: Reposition Input Gain Pre-Clamp + Extend Drive Range Summary

Walked through the boundary seam Plan 01 named: Input Gain is now a pre-clamp host-rate float multiply applied BEFORE `BoundaryConverter::toInt16`'s clamp on both the plugin and standalone paths. The internal range widened from 0.0..1.0 to 0.0..16.0 (silence → -6 dB default → unity → +24 dB drive). The boundary clamp is now reachable as a deliberate saturator/overdrive stage at the top of the knob, and the bottom half of the knob gives real headroom below the int16 ceiling instead of just attenuating a clipped signal. The C core was not touched.

Anthony's audible verdict from the UAT sweep: at default knob position the dry/reverb mix is unchanged from pre-Phase-23 behavior; at the top of the knob, signals saturate cleanly with the character the project's North Star calls for. ("This sounds great btw. listening now.")

## What shipped

| Task | Description | Commit |
| ---- | ----------- | ------ |
| 1 | Add `inline float applyInputGain(float, float) noexcept` to `BoundaryConverter.h` inside `namespace spu94::plugin::boundary` (pure multiply, no clamp). Update `PluginProcessor.h` inputLevel comment to document the new 0.0..16.0 range with anchor points (0.0 silence / 0.5 -6 dB default / 1.0 unity / 16.0 +24 dB drive); preserve the `{0.50f}` literal byte-identically (PLUG-19 gate). Add `kInputGainDefault = 0.5f` and `kInputGainMax = 16.0f` constants. Declare `juce::HeapBlock<float> inputGainScratch_[2]` member for the upcoming pre-clamp multiply. | `6bffcb1` |
| 2 | Wire the pre-clamp Input Gain multiply into the plugin branch of `processBlock`. `prepareToPlay` allocates `inputGainScratch_[0..1]` to `kMaxBlock=4096`. Plugin branch reads `inputLevel.load(relaxed)` once per block, scalar-loops `spu94::plugin::boundary::applyInputGain` over the host buffer into the scratch, hands scratch pointers to `srcChain_.processIn` (host buffer never mutated). Engine register write at per-block atomic sync switched to wrapperType-aware form (Form A in the plan) — standalone path kept the existing Q15-from-atomic write, plugin path pinned at `0x7FFF`. `savePresetToString` mirrored the same policy. Side-channel limiter constants (`kSideKnee = 0.125f`, `kSideCeiling = 0.06f`) and the limiter code at :352-405 and :465-478 byte-identical (PLUG-20 gate). Build green for VST3, LV2, CLAP, Standalone with zero new warnings. | `f7f9137` |
| 3 (UAT correction, applied during the human-verify gate) | **Post-UAT amendment.** Anthony's first hands-on sweep of the standalone slider revealed that the wrapperType-conditional D-03 design from Task 2 was incompatible with the widened 0.0..16.0 atomic range: the standalone path's `static_cast<int16_t>(inputLevel * 0x7FFF)` Q15 cast overflowed int16_t at any value > 1.0 and wrapped to a negative number, audible as "quieter past unity, silent near the top." Two corrections applied: (1) Standalone path now also uses pre-clamp float gain — `boundary::toFloat → applyInputGain → boundary::toInt16` — and the engine register is pinned at `0x7FFF` on BOTH paths (the wrapperType branch in the per-block register sync was removed). (2) Standalone GUI slider widened from `0.0..1.0` → `0.0..16.0` with `setSkewFactorFromMidPoint(1.0)` so unity sits at the knob midpoint. PLUG-19 and PLUG-20 gates still pass byte-identically. | `7e9bc82` |

## Decisions made

### Plan-as-written decisions (Tasks 1–2)

1. **Pre-clamp placement, host-rate, float-domain.** Confirmed D-01: gives real headroom below unity, real drive above. Applied via a scratch float buffer pair (not in-place on the host buffer) to stay safe across all 4 plugin formats and any read-only-input wrapper contracts. R1 (in-place mutation hazard) addressed by the scratch path.
2. **Multiply form: scalar loop calling `applyInputGain`** rather than `juce::FloatVectorOperations::multiply`. Reason: the named seam is the point of Plan 01's lift — a grep for `applyInputGain` returns one definition and one calling site (now two, post-UAT). Both forms are RT-safe and produce identical optimized code at `-O2`; the scalar-loop form is more discoverable.
3. **Engine register pin (D-03), plan-as-written form: Form A wrapperType-conditional ternary.** Plan offered Form A (ternary in the unconditional sync block) and Form B (move the sync inside the standalone branch). Form A picked: smaller diff, preserves the "atomics sync happens above the branch split" structure. Standalone path got the existing Q15 register multiply; plugin path got the unity pin. This was **revised post-UAT** — see next section.
4. **Scratch sizing: `kMaxBlock = 4096`**, matching the existing `SrcChain.cpp:116-117` and `PluginProcessor.cpp:348` scratch sizing pattern. Allocated in `prepareToPlay`; processBlock only reads/writes pre-allocated memory. R3 (RT-safety regression) and R4 (atomic-read placement) addressed by the once-per-block load pattern.
5. **Atomic semantic change WITHOUT initializer change.** `inputLevel{0.50f}` literal preserved byte-identically (PLUG-19 gate). Only the inline comment changed — from "0.0..1.0 unity gain" to the four-anchor block (silence / -6 dB default / unity / +24 dB drive). The atomic's interpretation changed from a Q15 register multiplier to a linear float gain factor on the host-rate signal; the stored value did not.

### Post-UAT decisions (Task 3 / commit `7e9bc82`)

6. **D-03 amended: register pinned at unity on BOTH paths, not wrapperType-conditional.** Reason recorded above (int16 overflow in the Q15 cast at atomic values > 1.0). The wrapperType-conditional design was a consequence of treating "standalone path back-compat" as a separable concern, but back-compat against an atomic range that no longer exists (0.0..1.0) was not actually preserved by the Q15 cast — it was actively broken by overflow. Pinning at unity on both paths and routing both through the pre-clamp float multiply is the only design that is correct against the widened range.
7. **Standalone path migrated to pre-clamp float gain.** Plan-as-written had explicitly deferred this ("standalone-path parity is deliberately not pursued; internal dev-only per v1.7"). UAT proved this deferral was incompatible with the widened range — the deferred path's existing code was actively broken by the new atomic semantics. Migration scope: the int16 wav-sample read inside the standalone branch now goes `WAV sample → boundary::toFloat → boundary::applyInputGain(gain) → boundary::toInt16`, with the same `inputLevel.load(relaxed)` once-per-block pattern as the plugin path. Side-channel limiter and the standalone-path `srcChain` invocation are unchanged.
8. **Standalone GUI slider widened in-phase to 0.0..16.0 with unity-at-midpoint skew.** Plan-as-written had pushed GUI exposure to Phase 24 (PLUG-28). UAT proved the new internal range was not reachable from the standalone testbed without a slider that exposed it. Standalone GUI ≠ plugin-format host-automation surface: the standalone GUI is the in-process JUCE Slider in `PluginEditor.cpp:88-105`; the plugin-format host-automation surface is the (still-future) `AudioProcessorParameter` exposed to VST3/LV2/CLAP hosts. Phase 24 / PLUG-28 still owns the latter. Exposing the new range to the former is in scope because v1.7 treats the standalone build as the internal testbed for these audio-path changes.

## Success criteria — status

| Criterion | Status | Evidence |
| --------- | ------ | -------- |
| PLUG-19: `std::atomic<float> inputLevel{0.50f}` literal preserved byte-identically | PASS | `grep -n 'std::atomic<float> inputLevel' src/plugin/PluginProcessor.h` returns the line with `{0.50f}` initializer unchanged (only the inline comment changed). |
| PLUG-20: side-channel limiter (`kSideKnee = 0.125f`, `kSideCeiling = 0.06f`, code at :352-405 and :465-478) byte-identical | PASS | `git diff -U0 main -- src/plugin/PluginProcessor.cpp` over the limiter line ranges shows zero hunks intersecting :352-405 or :465-478. Constants byte-identical. |
| Pre-clamp gain wired on plugin path BEFORE `srcChain_.processIn` | PASS | `grep -n 'applyInputGain\|inputLevel\.load' src/plugin/PluginProcessor.cpp` shows `inputLevel.load(std::memory_order_relaxed)` loaded once at the top of the plugin branch and `spu94::plugin::boundary::applyInputGain` called per-sample at :476-477 into `inputGainScratch_[0/1]`, before the `srcChain_.processIn` call. |
| Pre-clamp gain wired on standalone path (post-UAT amendment) | PASS | `grep -n 'applyInputGain' src/plugin/PluginProcessor.cpp` shows two additional call sites at :413, :415 inside the standalone branch, applied to the WAV-sample-derived float values between `boundary::toFloat` and `boundary::toInt16`. |
| Engine register pinned at `0x7FFF` on BOTH paths (post-UAT amendment) | PASS | `grep -n 'spu94_set_input_gain' src/plugin/PluginProcessor.cpp` shows two call sites — :284 (per-block atomic sync, unconditional) and :553 (`savePresetToString`) — both writing `0x7FFF`. The wrapperType branch from Form A has been removed. |
| `kInputGainDefault`, `kInputGainMax`, `inputGainScratch_[2]` declared in `PluginProcessor.h` | PASS | Grep confirms all three present in the same private section as `inputLevel`. Constants are documentation anchors for Phase 24 GUI wiring (not used to re-initialize the atomic). |
| Build green: VST3, LV2, CLAP, Standalone Release | PASS | `cmake --build build_test --target spu94_plugin -j` succeeds across all 4 Linux-available formats; zero `error:` lines; no new warnings attributable to this plan. Pre-existing `-Wfloat-equal` warnings in other files unchanged. |
| D-02 audible behavior at default (0.5) | PASS (Anthony UAT) | Standalone slider at default position (= -6 dB pre-clamp gain): dry/reverb mix sounds identical to pre-Phase-23 behavior — Anthony's wording: "This sounds great btw. listening now." No clip artifacts, real -6 dB of headroom below the boundary clamp. |
| D-02 audible behavior at ceiling (16.0) | PASS (Anthony UAT) | Standalone slider at the top of its range (= +24 dB pre-clamp gain): signals saturate cleanly against the int16 ceiling with the deliberate sat_s16 / overdrive character the North Star calls for. After the post-UAT correction the slider sweep no longer has the "quieter past unity, silent near the top" failure mode. |
| PDC unchanged at neutral gain | DEFERRED (user UAT, same procedure as Phase 22 Task 4) | Plan 01's null-test residual baseline is the reference. At default Input Gain (0.5) the pre-clamp multiply is a deterministic -6 dB float scalar applied to the host-rate buffer before SRC; it shouldn't shift PDC alignment. Formal null-test reproduction in Ardour is the same deferred manual UAT step Plan 01 and Phase 22 also deferred (no automated runner exists). Will be reproduced alongside the next null-test pass when one is scheduled. |
| `pluginval --strictness-level 7` zero new violations | DEFERRED (CI advisory job) | Same deferral as Plan 01 and Phase 22: `pluginval` runs in the Phase 21 CI advisory matrix, not locally. The new code adds zero allocation in processBlock (scratch is pre-allocated in `prepareToPlay`), zero locks, zero syscalls, zero logging — preserves the same RT-safety contract Phase 21 strictness-7 already cleared. |

## Byte-identical limiter proof

Verified `git diff -U0` over the PLUG-20 line ranges across all three commits (`6bffcb1`, `f7f9137`, `7e9bc82`):

- `kSideKnee = 0.125f` and `kSideCeiling = 0.06f` at :352-355: zero hunks.
- Standalone-branch limiter at :357-405: zero hunks. (The standalone-branch lines that DID change are the Input Gain pre-clamp multiply lines :411-416 — different line range; the limiter itself is byte-identical.)
- Plugin-path limiter at :465-478: zero hunks.

## Deviations from plan

### Auto-fixed during initial Task 2 execution

None of Rules 1–4 fired during Tasks 1 and 2. The plan-as-written was executed cleanly.

### Post-UAT amendment (commit `7e9bc82`)

Two divergences from the plan-as-written, both forced by UAT findings:

**1. [Rule 1 — Bug] D-03 register policy now uniform across both paths, not wrapperType-conditional.**
- **Found during:** Task 3 (manual UAT, Anthony's first standalone slider sweep).
- **Issue:** The plan-as-written kept the standalone path's existing `spu94_set_input_gain(engines[0], static_cast<int16_t>(inputLevel.load() * 0x7FFF))` register write to "preserve v1.6 back-compat." With the atomic range widened to 0.0..16.0, the cast `inputLevel * 0x7FFF` overflowed `int16_t` at any atomic value > 1.0 and wrapped to a negative number — audible as "signal got quieter past 1.0, then went silent near the top of the slider." The wrapperType-conditional design from Form A was incompatible with the widened atomic range, full stop.
- **Fix:** Standalone path also uses the pre-clamp float multiply (`boundary::toFloat → applyInputGain → boundary::toInt16` on each WAV sample); engine register pinned at `0x7FFF` on BOTH paths; the wrapperType branch in the per-block register sync block was removed.
- **Files modified:** `src/plugin/PluginProcessor.cpp` (standalone branch :380-416; per-block register sync :284; preset save sync :553).
- **Commit:** `7e9bc82`.

**2. [Rule 2 — Missing critical functionality] Standalone GUI slider range widened in-phase.**
- **Found during:** Task 3 (manual UAT).
- **Issue:** The plan-as-written deferred GUI exposure of the new range to Phase 24 / PLUG-28. UAT proved that the new 0.0..16.0 internal range was not reachable from the standalone testbed at all without a slider that exposed it — the standalone GUI slider was hard-capped at 0.0..1.0, so the +24 dB drive zone (the entire D-02 audible-behavior gate above unity) could not be auditioned. Phase 24 owns the **plugin-format** host-automation parameter surface (`AudioProcessorParameter` for VST3/LV2/CLAP), which is a separate code path from the in-process JUCE Slider in `PluginEditor.cpp`. Widening the latter is in scope per v1.7's standalone-as-testbed framing.
- **Fix:** `PluginEditor.cpp:92-98` slider range widened to `setRange(0.0, 16.0, 0.01)` with `setSkewFactorFromMidPoint(1.0)` so unity sits at the knob midpoint (bottom half attenuates, top half drives into the int16 ceiling).
- **Files modified:** `src/plugin/PluginEditor.cpp` (lines 92-98).
- **Commit:** `7e9bc82`.
- **Phase 24 / PLUG-28 dependency unchanged:** the host-automation `AudioProcessorParameter` surface for plugin formats is still future work and still needs to consume `kInputGainMax` for its declared range.

## Deferred items (out of scope for Plan 02)

- **Phase 24 / PLUG-28 — host-automation parameter surface.** Plugin-format hosts (VST3, LV2, CLAP) still see Input Gain through whatever the existing `AudioProcessorParameter` registration looks like (or doesn't); widening the host-param range to 0.0..16.0 with the right skew and labeling is Phase 24's job. The constants `kInputGainDefault = 0.5f` and `kInputGainMax = 16.0f` are public anchors for that work.
- **Phase 22 Ardour null-test reproduction at default Input Gain.** Same deferral as Plan 01 and Phase 22 itself: no automated null-test runner exists, the procedure is hands-on Ardour, and Anthony's UAT confirmed audible parity at default. Will be reproduced alongside the next scheduled null-test pass.
- **`pluginval --strictness-level 7` widening to 48/96/192 kHz.** Carries forward from Phase 22 deferred items; the new pre-clamp loop is RT-safe by inspection (pre-allocated scratch, once-per-block atomic load, no allocations / locks / syscalls / logging in processBlock).
- **Preset format storing the float gain factor separately.** Currently the GUI's Input Gain knob position is host-managed APVTS state (saved by the host plugin chunk), NOT stored in the SPU94 preset file. Plugin-saved AND standalone-saved presets now carry `input_gain = 0x7FFF` in the engine state, which is no longer meaningful on either path. A future preset format revision could persist the float gain factor explicitly if cross-host preset portability matters. Documented as known divergence (R2 narrowed: there's no longer cross-path divergence, but there is cross-version divergence — old v1.6 presets with stored `input_gain != 0x7FFF` are now effectively no-ops because both paths use the GUI atomic).
- **Revert the standalone-GUI slider widening once Phase 24 lands the host-param surface.** The comment at `PluginEditor.cpp:92-95` explicitly flags this as "UAT-ONLY (Phase 23); REVERT after UAT approval — slider exposure is Phase 24/PLUG-28 work." After Phase 24 ships, the standalone GUI can either continue to expose the wide range (since it's the testbed) or be brought back into parity with whatever the host-param surface settles on. That's a Phase 24 decision, not a Phase 23 deferred item per se.

## Known stubs

None. The pre-clamp gain stage is fully wired on both paths; the GUI slider is fully functional across its widened range; no placeholder data, no "coming soon" copy, no unwired controls.

## Threat surface scan

No new network endpoints, no new auth paths, no new file-system access patterns, no new trust-boundary schema. The pre-clamp multiply operates entirely within the existing audio-thread float<->int16 boundary that Plan 01 already established. No threat flags.

## Self-Check: PASSED

- `src/plugin/BoundaryConverter.h` modified — verified by `git log --oneline f52993e..HEAD -- src/plugin/BoundaryConverter.h` returning commit `6bffcb1` (Task 1).
- `src/plugin/PluginProcessor.h` modified — verified by `git log --oneline f52993e..HEAD -- src/plugin/PluginProcessor.h` returning commit `6bffcb1` (Task 1).
- `src/plugin/PluginProcessor.cpp` modified — verified by `git log --oneline f52993e..HEAD -- src/plugin/PluginProcessor.cpp` returning commits `f7f9137` (Task 2) and `7e9bc82` (Task 3 UAT correction).
- `src/plugin/PluginEditor.cpp` modified — verified by `git log --oneline f52993e..HEAD -- src/plugin/PluginEditor.cpp` returning commit `7e9bc82` (Task 3 UAT correction).
- Three commits found in `git log --format='%H %s' f52993e..HEAD`:
  - `6bffcb1` — Task 1 (applyInputGain helper + inputLevel range docs + constants + scratch declaration)
  - `f7f9137` — Task 2 (pre-clamp wiring on plugin path; register pin on plugin path; PLUG-20 preserved)
  - `7e9bc82` — Task 3 UAT correction (extend pre-clamp gain to standalone path; expose new range in standalone slider)
- Release build artifacts present at `build_test/src/plugin/spu94_plugin_artefacts/Release/{VST3, LV2, CLAP, Standalone}`.
- PLUG-19 byte-identical: `grep -n 'std::atomic<float> inputLevel' src/plugin/PluginProcessor.h` returns the unchanged `{0.50f}` initializer.
- PLUG-20 byte-identical: limiter line ranges :352-405 and :465-478 untouched across all three commits.
