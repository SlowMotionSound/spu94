# Phase 7: Pipeline Integration - Context

**Gathered:** 2026-04-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Rebuild `spu94_process` from a single-path reverb processor into a send/return mixer architecture. Three buses (dry, patina/ADPCM, reverb) with independent faders and sends, summed at a master mixer, followed by a DAC coloration section. This replaces the current signal flow where ADPCM feeds directly into the reverb chain and the host owns the wet/dry crossfade. After this phase, the C core owns the complete signal path — hosts become thin wrappers that pass audio and set controls.

</domain>

<decisions>
## Implementation Decisions

### Signal Flow Architecture
- **D-01:** Send/return mixer architecture. Input gain → signal splits into dry bus and patina (ADPCM) bus → two independent reverb sends (dry send + patina send) → reverb (100% wet) → three-fader master mixer (dry/patina/reverb) → DAC section → output.
- **D-02:** All DSP signal flow lives in the C core. JUCE, CLI, and Python are thin wrappers with no DSP logic. The existing JUCE wet/dry crossfade code in PluginProcessor.cpp gets deleted and replaced with straight passthrough of spu94_process output.
- **D-03:** ADPCM position is currently fixed (before the dry split), but avoid hardwiring it — future milestone may allow repositioning in the signal chain.

### Mixer Controls (C API)
- **D-04:** Six controls plus DAC section: input gain, dry fader, patina fader, dry reverb send, patina reverb send, reverb fader.
- **D-05:** All fader/send values use Q15 int16 (0x0000–0x7FFF), matching the existing SPU register value format. No floating-point in the C core. Hosts convert their float knobs to Q15 at the API boundary.
- **D-06:** No parameter smoothing/slew in the C core. Values land as raw register writes — abrupt changes produce clicks and digital stepping artifacts, which is intentional character. Smoothing is an optional host-side concern.

### ADPCM Latency Compensation
- **D-07:** Compensate the 28-sample ADPCM block delay with a matching delay buffer on the dry bus, so both arrive at the mixer time-aligned. Default: compensation ON.
- **D-08:** Compensation is toggleable via `spu94_set_latency_comp(state, 1/0)`. When OFF, the 28-sample offset between dry and patina buses creates comb filtering — musically useful as a creative effect.

### DAC Section
- **D-09:** DAC is a section with a master toggle and two sub-toggles. `spu94_set_dac_enabled()` is the parent switch — when off, nothing in the DAC section runs. When on, FIR and noise each have independent sub-toggles: `spu94_set_dac_fir_enabled()` and `spu94_set_dac_noise_enabled()`.
- **D-10:** Signal order within DAC section: FIR first, then noise added on top. Matches hardware where the interpolation filter runs before the delta-sigma modulator.
- **D-11:** All three toggles ON = faithful PS1 DAC behavior. Users can run just FIR, just noise, or neither (DAC on but both sub-toggles off = no-op passthrough).
- **D-12:** DAC section processes the master mixer output — it colors the final mixed signal, not individual buses. Matches hardware where everything hits the DAC on the way out.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Signal Flow Architecture (Session Decisions)
- `.planning/PROJECT.md` §Key Decisions — "Send/return mixer architecture" and "ADPCM position may become movable" entries
- `.planning/STATE.md` §Accumulated Context §Decisions — signal flow and mixer architecture summary

### ADPCM Integration Pattern (Template for Mixer Wiring)
- `src/spu94/spu94_process.c` — Current spu94_process implementation; will be substantially rewritten
- `src/spu94/spu94_io_chain.c` — chain_step_impl with decimator→reverb→interpolator flow; ADPCM toggle pattern; reverb-bypass variant
- `src/spu94/spu94_state_internal.h` — State struct where new mixer fields (faders, sends, delay buffers, DAC state) will be added

### DAC Modules (Phase 6 Outputs — Slot Into DAC Section)
- `include/spu94/spu94_dac_fir.h` — FIR module public API
- `include/spu94/spu94_dac_noise.h` — Noise module public API
- `src/spu94/spu94_dac_fir.c` — Three-stage cascaded half-band FIR implementation
- `src/spu94/spu94_dac_noise.c` — LFSR + 2nd-order HP noise shaping implementation

### Existing Toggle Pattern (Template for New Toggles)
- `src/spu94/spu94_io_chain.c` lines 148-178 — `spu94_set_adpcm_enabled()` / `spu94_get_adpcm_enabled()` pattern with state reset on disable

### Phase 6 Code Review (Unfixed Findings)
- `.planning/phases/06-dac-core-implementation/06-REVIEW.md` — CR-01 (int32 multiply width), WR-01 (noise quantization), WR-02 (deterministic seed). Address before or during integration.

### Requirements
- `.planning/REQUIREMENTS.md` — DAC-INT-01 (toggle API), DAC-INT-02 (state budget, zero regression), DAC-INT-03 (rt_safety gates)

### Project Constraints
- `.planning/PROJECT.md` §Constraints — C99, no heap, no locks, rt_safety gates
- `src/spu94/spu94_q15.h` — Q15 arithmetic primitives

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_set_adpcm_enabled()` / `spu94_get_adpcm_enabled()`: Toggle pattern with state reset on disable. Template for all new toggles (DAC master, FIR sub-toggle, noise sub-toggle, latency compensation).
- `spu94_adpcm_state` embedding in `spu94_state`: Pattern for embedding DAC FIR/noise state structs.
- `chain_step_impl()`: The core decimator→reverb→interpolator pipeline. The reverb path stays intact; the wrapper around it changes to implement the mixer architecture.
- ADPCM double-buffer in `spu94_process()`: 28-sample accumulation pattern. The dry bus delay compensation buffer follows the same idea (simple ring buffer of 28 int16 pairs).

### Established Patterns
- All toggles default to OFF (adpcm_enabled=0 at init). New toggles follow the same convention.
- State reset on disable: when a feature is toggled off, its internal state is zeroed so re-enable starts clean.
- `_Static_assert` on struct size: `SPU94_STATE_SIZE_MAX` will need bumping to accommodate new mixer state (faders, sends, delay buffers, DAC state).
- `-Werror/-pedantic`: all new code must compile clean.
- rt_safety gates: rt_no_heap, rt_no_locks, rt_no_syscalls, rt_bench_latency must pass.

### Integration Points
- `spu94_process()` is the primary rewrite target — transforms from single-path to mixer architecture.
- `spu94_state_internal.h` grows to hold: 6 fader/send Q15 values, latency comp toggle + delay buffer, DAC master toggle + FIR/noise sub-toggles + DAC module state structs.
- Public API in `include/spu94/spu94.h` gains new setters/getters for all controls.
- JUCE `PluginProcessor.cpp` wet/dry crossfade (lines 158-178) gets deleted; output becomes straight passthrough of spu94_process.

</code_context>

<specifics>
## Specific Ideas

- The mixer architecture should read like a mixing console: clear bus routing, obvious signal flow in the code, easy to trace which sample goes where.
- DAC section is a self-contained block at the end — easy to bypass entirely or extend later.
- Latency compensation toggle is a creative tool, not just a technical detail — name it clearly in the API.
- The ADPCM module's position in the chain should be structurally easy to move in a future milestone, even though it's fixed for now.

</specifics>

<deferred>
## Deferred Ideas

- **Parameter slew/smoothing control** — A user-facing knob in the C core that controls how quickly parameter changes reach their target value. All the way down = raw register slams with full digital crunch. All the way up = smooth float-interpolated transitions. Gives the musician direct control over the character of parameter changes. Belongs in M4 (real-time lever layer) alongside the other performance controls.
- **Movable ADPCM insert point** — Ability to place ADPCM encode/decode at different positions in the signal chain (e.g., after reverb instead of before). Noted in PROJECT.md as a future capability.

</deferred>

---

*Phase: 7-Pipeline Integration*
*Context gathered: 2026-04-29*
