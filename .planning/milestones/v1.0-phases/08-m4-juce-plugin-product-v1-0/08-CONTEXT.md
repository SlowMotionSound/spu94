# Phase 8: SPU-94 Standalone GUI (product v1.0) — Context

**Gathered:** 2026-04-25
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 8 delivers **SPU-94 as a standalone audio tool** — a single-window JUCE-built GUI app that loads any-sample-rate / any-bit-depth WAV file, normalizes it via a thin I/O wrapper, processes it through the existing M1 reverb core (`libspu94`) at the SPU's native 44.1 kHz int16 internally, and plays the processed audio back in real-time. The user (Anthony) gets a way to actually HEAR what the M1 algorithm sounds like — twist register sliders during playback, A/B against dry signal, switch presets, hear character changes live.

**Why this is product v1.0 (re-framed during this discussion):** The original "v1.0 = JUCE plugin shippable to a DAW" framing is replaced. v1.0 is now a standalone GUI testing tool whose entire purpose is closing Anthony's "I can't easily hear what's being built" gap. Plugin formats and DAW integration are explicitly **out of scope for v1.0**, deferred to separate future phases.

**In scope:**
- JUCE standalone application — `juce_add_plugin(... FORMATS Standalone ...)`. No VST3 / LV2 / CLAP / AU.
- Light I/O wrapper that handles any-SR / any-bit-depth WAV input, normalizes to 44.1 kHz int16 stereo for the SPU
- WAV file load + realtime playback (no save/export)
- 10 PS1 factory presets selectable via flat dropdown
- 18 raw labeled SPU register sliders (the 12 free-class + 6 sample-quantized registers; m\* family stays preset-fixed)
- Wet/Dry mix knob — the only added DSP outside `libspu94` (routes the dry input alongside the SPU's wet output)
- JUCE stock look-and-feel (no custom skin)
- Builds via the existing root CMake, extended with JUCE — `libspu94` is linked unmodified
- Linux primary

**Out of scope (deferred to later phases):**
- Plugin formats (VST3 / LV2 / CLAP / AU)
- DAW integration / DAW-automatable parameters
- Named musical lever curation (Room Size / Pre Delay / Damping / Width / Mix etc.) — deferred to a follow-up phase informed by listening evidence Anthony generates from using v1.0
- All plugin-layer DSP additions beyond Wet/Dry: true Pre-Delay, Input HPF, Freeze, Tail modulation/LFO
- WAV file save / export (deferred — Anthony explicitly said "I don't really need file save/export right now")
- Live audio input (mic / line-in via JACK / PipeWire / ALSA)
- Custom UI skin / visual identity
- Preset categories / advanced disclosure UX
- macOS / Windows builds
- License pick (MIT vs Apache-2.0) — explicitly not relevant at design stage

</domain>

<decisions>
## Implementation Decisions

### Area A — Plugin format scope
- **D-01-A:** v1.0 ships **standalone only**. No VST3 / LV2 / CLAP / AU. Reason: the primary v1.0 need is closing Anthony's "I can't easily hear or test what's being built" gap; a standalone tool meets that need fully and is the smallest viable scope. Plugin and DAW integration are explicitly out of scope until the primary need is met. Plugin formats become a separate future phase if/when a DAW user emerges or Anthony installs a DAW.

### Area B — Lever surface
- **D-01:** v1.0 ships **all 18 viable SPU registers as raw labeled sliders**. Named musical lever curation (Room Size / Pre Delay / Damping etc.) is deferred to a follow-up phase informed by listening evidence Anthony generates from using the v1.0 tool. Reason: prior `vIIR = decay`, `vWALL = damping` musical-role labels were topology inferences, not empirical knowledge — Phase 7 measured stability + determinism, not perceptual character. The right way to design named levers is from listening, not from algorithm topology.
- **The 18 sliders:** `vLOUT`, `vROUT`, `vLIN`, `vRIN`, `vIIR`, `vWALL`, `vCOMB1`, `vCOMB2`, `vCOMB3`, `vCOMB4`, `vAPF1`, `vAPF2` (12 free-class — smooth at any modulation rate) + `dLSAME`, `dRSAME`, `dLDIFF`, `dRDIFF`, `dAPF1`, `dAPF2` (6 sample-quantized — audible stepping is character).
- **The 17 m\* registers** stay preset-fixed; switching presets is how they change. They are NOT exposed as sliders in v1.0.

### Area C — Plugin-layer additions
- **D-02:** **Wet/Dry mix only.** No Pre-Delay, no Input HPF, no Freeze toggle, no Tail-modulation LFO. Wet/Dry is the only added DSP outside `libspu94`; effectively required to A/B against the dry signal during testing. Reason: v1.0 should not patch additional DSP onto the native reverb algorithm — all other plugin-layer extras are deferred to later phases.

### Area D — Audio I/O scope
- **D-03:** **WAV file load + realtime playback only.** No file save / export. No live audio input. The minimum viable workflow is: load a WAV, press play, hear it processed through the reverb. Save/export and live input are not needed at v1.0; both deferred.

### Area E — UI direction
- **D-04:** **JUCE stock look-and-feel.** No custom skin, no painted backgrounds, no bespoke widgets. Functional, matches debug-tool framing. Custom UI / visual identity belongs in a future polish phase.

### Area F — Preset selector UX
- **D-05:** **Flat 10-item dropdown.** No categories, no Standard/Advanced disclosure split. Just the 10 PS1 factory presets in a simple combo box.

### Area G — Sample rate / bit depth handling
- **D-06:** **Light I/O wrapper handles any-SR / any-bit-depth WAV input.** SPU core unchanged. JUCE-side I/O layer adapts: bit-depth conversion (any → int16), sample-rate conversion (any → 44.1 kHz, via JUCE's built-in interpolators), channel adaptation (mono → duplicate to stereo if needed). Architectural intent: SPU stays bit-faithful; the GUI's I/O layer is what adapts. The internal reverb code is never touched to accommodate input format variations.

### Claude's Discretion (within the locked decisions above)
- Slider layout / grouping on the panel (by class? by signal-flow position? flat?) — planner's call within JUCE stock components
- Knob/slider widget choice (rotary vs vertical-strip) — planner's call within JUCE stock
- Specific JUCE interpolator choice for resampling (LagrangeInterpolator vs CatmullRomInterpolator vs WindowedSincInterpolator) — planner's call
- Whether the GUI shows underlying register values as numbers next to sliders — recommended yes for debug, but planner's call
- Whether "play" auto-starts on file load or requires a button press — planner's call
- File picker UX — JUCE's `FileChooser` with whatever default path makes sense
- Whether to use JUCE's built-in `AudioFormatReader` for WAV I/O or the existing vendored `dr_wav` — planner's call (JUCE's built-in is the standard JUCE pattern)
- JUCE version pin — JUCE 7.x or JUCE 8.x both viable; planner picks
- Specific JUCE module set imported — planner's call

### Folded Todos
None — `gsd-tools todo match-phase 8` returned 0 matches at discussion time.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before proceeding.**

### Project Spec (internal) — note about pending framing updates

The standalone re-scope captured in this CONTEXT.md is a v1.0 product redefinition. The following project artifacts still contain the older "v1.0 = M4 plugin shippable to a DAW" framing as of this CONTEXT.md commit; CONTEXT.md is authoritative for Phase 8 scope, and the artifact updates are a follow-up clean-up pass:

- `.planning/PROJECT.md` — Constraints that govern Phase 8 are still valid: plain C99 core stays unmodified (`libspu94` is linked, not forked); Linux primary; trademark posture (product name = "SPU-94", NOT "PSX Reverb"); paraphrase-not-transcribe licensing posture (applies to any user-facing text in the standalone GUI). The "Active — Next Milestone: M4 (JUCE plugin → product v1.0)" section needs rewriting to reflect standalone-tool scope.
- `.planning/REQUIREMENTS.md` — Currently lists PLUGIN-01..PLUGIN-09 (DAW-plugin scope). The standalone re-scope changes the requirement set; PLUGIN-* should be replaced with STANDALONE-* requirements that match this CONTEXT.md.
- `.planning/ROADMAP.md` § Phase 8 — Currently titled "M4 — JUCE Plugin (product v1.0)" with DAW-targeting goal + Reaper-as-reference-DAW framing. Should be retitled "M4 — SPU-94 Standalone GUI (product v1.0)" and goal/success criteria rewritten for the standalone scope.

### Public C API surface (wrapped by the standalone)
- `include/spu94/spu94.h` — full M1 public surface: `spu94_init`, `spu94_reset`, `spu94_destroy`, `spu94_process`, `spu94_flush`, `spu94_load_preset`, `spu94_state_size`, `spu94_get_buffer_address`, `spu94_get_latency_samples`, `spu94_presets[]`. The standalone calls these directly from C++.
- `include/spu94/spu94_registers.h` — `spu94_reg_t` enum (35 entries), engine-layer setters/getters (`spu94_set_reg_i16` / `spu94_set_reg_u16` / `spu94_get_reg_i16` / `spu94_get_reg_u16`), `spu94_reg_name`, `spu94_reg_hw_offset`. The standalone uses these to drive the 18 raw register sliders (one slider per viable register, label = `spu94_reg_name(i)`).
- `include/spu94/spu94_register_facade.h` — 105 hand-written inline per-register wrappers. Standalone may use these for code readability if planner prefers; the engine-layer iteration pattern (Phase 6 binding pattern) is also valid.

### Phase 7 outputs that inform Phase 8
- `docs/LEVERS-CATALOG.md` — Modulation cost classification (12 free / 6 sample-quantized / 17 catastrophic). The 18 viable registers in D-01 are the union of `free ∪ sample-quantized`. The HAND columns ("musical role", "M4 lever") stay empty for v1.0 — they're filled by the future named-lever-curation phase using listening evidence from v1.0.
- `docs/COVERAGE.md` — Spec-conformance map (informs which library behaviors the standalone is exercising via the 18 sliders + preset switching).

### Prior phase CONTEXT.md files (must read for consistency)
- `.planning/phases/06-python-binding-cli/06-CONTEXT.md` — D-22 extensibility seams + D-23 observability principle (Phase 8 is a pure consumer of the public C surface; adds no new seams or mutators). Engine-layer iteration pattern over `spu94_reg_name` / `spu94_set_reg_*` (Phase 8 reuses this pattern, C++ side).
- `.planning/phases/07-verification-golden-files-witness-diff-modulation/07-CONTEXT.md` — D-16/D-17 modulation cost classification and stability/determinism gates (the 18 viable registers come from `free ∪ sample-quantized`).

### External References
- **JUCE framework** (juce.com) — C++ application/plugin framework. Standalone target via `juce_add_plugin(... FORMATS Standalone ...)`. JUCE 7.x or 8.x both viable. JUCE is licensed under GPL v3 OR commercial — for v1.0 (Anthony's personal-use tool, not redistributed), GPL is fine; if/when public release begins, license posture review needed.
- **dr_wav** (already vendored at `vendor/dr_wav/`) — used by the existing CLI; standalone can reuse for WAV I/O, OR JUCE's built-in `AudioFormatManager` / `AudioFormatReader` can handle it. Planner's call.

### Not to be read as primary source
- Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, MiSTer source — per PROJECT.md licensing posture. No GPL-source-derived code in Phase 8.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `libspu94.so` (CMake target `spu94_shared`) — full M1 public surface, ready to link from C++ standalone code via `extern "C"` includes
- `include/spu94/*` — public headers, used directly from the C++ standalone
- Root `CMakeLists.txt` — extended to add a JUCE subproject and a standalone executable target
- `vendor/dr_wav/` — already vendored; standalone may reuse OR planner may pick JUCE's built-in WAV support
- `python/spu94/` — existing Python binding; NOT consumed by the standalone (developer tooling only). The runtime-reflected register IntEnum pattern from Phase 6 informs how the C++ standalone iterates registers (engine-layer iteration via `spu94_reg_name` + `spu94_set_reg_*`).

### Established Patterns
- **Engine-layer register iteration** (from Phase 6 binding) — the C++ standalone iterates all 35 registers via `for (int i = 0; i < SPU94_REG__COUNT; ++i) { name = spu94_reg_name(i); ... }`, filters to the 18 viable ones via the cost-class mapping, builds 18 sliders.
- **One-concern-per-TU grain** (Phase 2 onwards) — applies to the standalone's source organization
- **License-placeholder discipline** (Phase 1) — `LICENSE` file is still a placeholder; standalone metadata respects that

### Integration Points
- **C public API is the sole dependency boundary.** Standalone code calls `spu94_*` symbols only; never reaches into `src/spu94/*_internal.h`.
- **Phase 6 Python binding is unaffected** — standalone and Python binding are independent consumers of the same C API. Both continue to ship.
- **Future plugin-formats phase** — the JUCE codebase scaffolded in Phase 8 becomes the basis for VST3 / LV2 / CLAP wrappers later (just changes the `FORMATS` argument in `juce_add_plugin`). Phase 8 architecture should keep this future easy without designing for it.
- **Future named-lever-curation phase** — the 18 raw register sliders feed Anthony's ear; the curation phase reads his listening notes (subjectively gathered) plus optionally Phase 7's modulation harness data and produces a curated named-lever surface that wraps the underlying registers.

</code_context>

<specifics>
## Specific Ideas

### v1.0 standalone shape (sketch — planner refines)

Single window. Top: WAV file load button, transport (play / pause / stop), preset dropdown, Wet/Dry knob.

Middle: 18 register sliders. Suggested grouping (planner's call):
- **Master / I/O (4):** `vLOUT`, `vROUT`, `vLIN`, `vRIN`
- **IIR (2):** `vIIR`, `vWALL`
- **Comb (4):** `vCOMB1`, `vCOMB2`, `vCOMB3`, `vCOMB4`
- **APF (2):** `vAPF1`, `vAPF2`
- **Delay positions (6):** `dLSAME`, `dRSAME`, `dLDIFF`, `dRDIFF`, `dAPF1`, `dAPF2`

Each slider labeled with its raw register name (`vIIR`, `dCOMB1`, etc.) — NOT musical labels like "Decay" / "Damping". Numeric value display next to each slider is recommended for debug clarity.

### What the user does
1. Drop a WAV onto the GUI (any sample rate, any bit depth)
2. Pick a preset from the dropdown ("Hall")
3. Hit play — hear the WAV processed through the reverb in real-time
4. Twist the sliders — hear character changes live (gain-class smooth, delay-class steps audibly = character)
5. Twist Wet/Dry — A/B against dry signal
6. Switch presets while playing — hear the room geometry change

### What the standalone does NOT do (per Anthony's guidance during discussion)
- No file save / export
- No live audio input
- No custom UI skin
- No named musical levers (raw register names only)
- No plugin-layer DSP additions beyond Wet/Dry
- No preset categories or advanced disclosure
- No sample-rate-conversion in the SPU itself (conversion happens in the I/O wrapper)
- Does NOT ship as VST3 / LV2 / CLAP / AU

### Architecture sketch

```
WAV file (any SR, any bit depth, mono or stereo)
   ↓
[ JUCE AudioFormatReader → float samples ]
   ↓
[ I/O wrapper: SR convert → 44.1 kHz, channel convert → stereo, format convert → int16 ]
   ↓
[ libspu94: spu94_process (bit-faithful, untouched) ]
   ↓
[ Wet/Dry mix: blend dry input + spu94 wet output ]
   ↓
[ JUCE audio output → speakers ]
```

</specifics>

<deferred>
## Deferred Ideas

### Deferred to a follow-up phase: Named-Lever Curation
Once Anthony has used the v1.0 standalone with raw register sliders for a while, a follow-up phase curates the named-lever surface (Room Size, Pre Delay, Damping, Width, Mix at minimum, possibly more) based on his listening evidence. This phase fills in the LEVERS-CATALOG.md HAND columns with empirically-grounded musical-role descriptions and lever groupings.

### Deferred to a follow-up phase: Plugin Format Support
VST3 / LV2 / CLAP plugin builds. Same JUCE codebase, just changes the `FORMATS` argument in `juce_add_plugin`. Lands as a separate phase if/when Anthony installs a Linux DAW, or a DAW user requests it, or the named-lever-curation phase produces a polished surface worth shipping.

### Deferred to later phases: Plugin-Layer DSP Additions
- True Pre-Delay (sample buffer before SPU input)
- Input HPF (high-pass filter before SPU input)
- Freeze (max `vIIR` + lock — could be UI trick on existing slider, or dedicated toggle)
- Tail modulation / LFO module (built-in LFO targeting a chosen register)

All deferred until v1.0 use surfaces a real need.

### Deferred: WAV File Save / Export
Anthony explicitly said "I don't really need file save/export right now." Lands when he wants to keep processed audio.

### Deferred: Live Audio Input
Mic / line-in via JACK / PipeWire / ALSA. Adds audio device picker UI, latency tuning, lower-level audio device handling. Not blocking for "hear the WAV processed through the reverb" goal.

### Deferred: Custom UI / Visual Identity
Custom-painted look-and-feel, branded backgrounds, bespoke widgets. v1.0 ships JUCE stock; visual polish lands when the named-lever surface is curated.

### Deferred: macOS / Windows Builds
Linux-first per project constraints. Cross-platform straightforward via JUCE; deferred to post-v1.0 only.

### Deferred: License Pick (MIT vs Apache-2.0)
Not relevant at the design stage. Lands when Anthony is ready to address it.

### Raised in Discussion, Routed Elsewhere
- **DAW reference target** — REQUIREMENTS.md / ROADMAP.md hardcoded "Reaper" as the reference DAW; Anthony has no Linux DAW installed. Resolved by dropping plugin formats from v1.0 entirely. The plugin-formats phase will revisit DAW-target picks if/when Anthony installs one.
- **M2 ADPCM and M3 DAC testing** — Anthony mentioned wanting the standalone partly to test future M2/M3 work. Confirmed: standalone is built to be the testbed for those when they land later; M2/M3 sequencing unchanged (still post-v1.0). The standalone-as-testbed framing is implicit in the v1.0 scope.
- **Topology-inferred musical-role labels are not knowledge** — Surfaced that Claude was passing off algorithm-topology inferences ("vIIR = decay", "vWALL = damping") as if they were facts. Resolved: empirical listening evidence comes from using the v1.0 tool; named-lever curation deferred to a phase that has actual listening data to work from.

### Reviewed Todos (not folded)
None — no pending todos existed at discussion time.

</deferred>

---

*Phase: 08-m4-juce-plugin-product-v1-0 (note: directory name uses pre-discussion framing; phase scope is now SPU-94 Standalone GUI)*
*Context gathered: 2026-04-25*
*Next step: `/gsd-plan-phase 8` — planner consumes this CONTEXT.md. Standalone scope is much smaller than the original plugin scope; expect ~3-5 plans (build-system extension + JUCE scaffolding, I/O wrapper + WAV loader, register slider wiring + preset dropdown, Wet/Dry mix, audio output + transport).*
