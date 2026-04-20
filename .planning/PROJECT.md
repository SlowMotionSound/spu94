# SPU-94

## What This Is

SPU-94 is a bit-faithful software reimplementation of the Sony PlayStation 1 SPU reverb algorithm, built from the spec (nocash psx-spx) rather than ported from any existing emulator. It ships as a plain C library with Python bindings for development, and is designed to be wrapped later as a desktop audio plugin (JUCE) and eventually as hardware (Eurorack module, MCU firmware, or FPGA). SPU-94 is designed as a *living instrument*, not a static bank of presets — every parameter that moves in the original algorithm is designed to be controllable at runtime, smoothly and glitch-free, in service of performance, modulation, and CV control. The immediate audience is the author and a small circle of musicians who want the recognizable character of the PS1 reverb available as a modern, playable tool.

## Core Value

**Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.**

Rigor governs the algorithm; musicality governs everything that surrounds it. The algorithm must be faithful; the experience of using it must be alive.

## Requirements

### Validated

(None yet — ship to validate)

### Active — Milestone 1 (reverb network + hard clip)

- [ ] Implement all 35 SPU registers that affect reverb output with documented behavior — the 32 reverb-block registers at `1F801DC0–DFE` (filter coefficients `vIIR`/`vWALL`/`vCOMB1-4`/`vAPF1/2`, comb/APF delay addresses `dCOMB1-4`/`dAPF1/2`, SAME/DIFF/APF delay-address pairs `dLSAME`/`dRSAME`/`dLDIFF`/`dRDIFF`/`dLAPF1/2`/`dRAPF1/2`, plus reverb input volumes `vLIN`/`vRIN`) plus `mBASE` buffer base and `vLOUT`/`vROUT` output gains (outside the `DC0–DFE` block)
- [ ] Implement the all-pass + comb filter network topology on a work buffer
- [ ] Implement fixed-point arithmetic with integer truncation (not rounding) matching SPU semantics
- [ ] Implement hard clip / overflow behavior on the mix bus feeding the reverb
- [x] Implement the 39-tap half-band FIR at both I/O boundaries — nocash's documented coefficients verbatim — to correctly convert between the 44.1kHz host rate and the reverb's internal 22.05kHz processing rate. This is what closes the fidelity gap that lv2-psx-reverb explicitly leaves open. *(Validated in Phase 4: sample-rate-conversion-39-tap-half-band-fir)*
- [ ] Design the C API so that mid-stream register updates are a first-class use case — register writes during audio processing must not glitch, crash, or require reinitialization
- [ ] Resolve and document the delay-length-register-change gray area: what happens when dCOMB1-4, dAPF1/2, or similar position-dependent registers change mid-stream (work-buffer reindexing, phase discontinuity policy, whether interpolation is in scope)
- [ ] Provide a modulation test — continuously modulate each register (sine, sweep, random walk) during a live audio stream and verify output remains stable, bounded, and free of zipper noise or crashes
- [ ] Expose the core as a plain C library (`libspu94`) with a small, stable public API
- [ ] Ship Python bindings via ctypes so tests, analysis, and exploration happen in Python+numpy
- [ ] Ship a small CLI (`spu94`) that processes WAV files end-to-end for witness diffs and golden-file tests
- [ ] Build on Linux (primary dev target)
- [ ] Cross-compile smoke test to Daisy / Cortex-M (no audio I/O; proves MCU-portability)
- [ ] Provide spec-conformance test coverage — each documented behavior has a corresponding test
- [ ] Provide register-level unit tests — each register exercised in isolation
- [ ] Provide witness-output diff harness against lv2-psx-reverb (output-only; no source reading)
- [ ] Provide golden-file regression tests — snapshot signed-off outputs and diff future runs
- [ ] Ship the 10 documented PS1 factory reverb presets as register-config fixtures (Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay, Off)
- [ ] Maintain `DECISIONS.md` — a first-class deliverable documenting every gray-area resolution with rationale
- [ ] Maintain `docs/LEVERS-CATALOG.md` — annotate each register with its musical role and candidacy for real-time/CV control in future milestones

### Out of Scope (for Milestone 1 — most deferred, not abandoned)

- **4-bit Sony ADPCM encode/decode** — deferred to Milestone 2; its own gray-area journey
- **DAC reconstruction modeling** — deferred to Milestone 3; PS1 DAC coloration/reconstruction filter in DSP
- **JUCE / VST3 / AU / LV2 plugin** — deferred to Milestone 4; library is the M1 product
- **Named musical levers ("Room Size", "Pre Delay", etc.), parameter smoothing, CV mappings, plugin UI** — these are how aliveness is *delivered to the player*, built in Milestone 4 on top of the M1 foundation. M1's contribution to aliveness is: a glitch-free mid-stream register API and a catalog of candidate levers (`docs/LEVERS-CATALOG.md`). The abstraction itself is M4 work.
- **Hardware validation via PSX homebrew + digital capture** — deferred to Milestone 5; Anthony has an original PSX; mini-project of its own
- **Eurorack module (PCB, panel, analog output stage, period-appropriate DAC hardware)** — explicitly future direction; not part of current project scope; documented separately in `ps1-reverb-eurorack.md`
- **FPGA implementation** — future direction; C core is chosen specifically to keep FPGA HLS path open
- **MCU firmware port (Daisy, etc.)** — future direction; M1 proves portability via cross-compile smoke test only
- **SPU voice engine, envelope generation, pitch modulation, noise, ADSR** — explicitly out of scope for the entire project; this is a reverb-focused reimplementation only
- **Windows and macOS builds** — deferred; Linux-first during M1; cross-platform is straightforward C but not blocking
- **Reading Mednafen, lv2-psx-reverb, DuckStation, or MiSTer source as a primary development activity** — excluded by licensing posture (see Constraints)

## Context

**Domain:** The PS1 SPU reverb is a Schroeder/Gardner-style network — all-pass filters and comb filters operating on a shared reverb work buffer in SPU RAM — whose musicality comes not from algorithmic sophistication but from *implementation artifacts*: 16-bit fixed-point arithmetic with truncation at each stage, hard-clipping overflow behavior, ADPCM source coloration, and period-appropriate DAC coloration. "Bit-accuracy is not optional — it is the sound." Any reimplementation that rounds instead of truncates, or replaces the fixed-point math with floats, loses the character.

**Primary reference:** nocash's psx-spx documentation (problemkaputt.de / psx-spx.consoledev.net) documents every SPU register that affects reverb (vIIR, vWALL, dAPF1/2, dCOMB1-4, vCOMB1-4, dLSAME/dRSAME, dLDIFF/dRDIFF, dLAPF1/2, dRAPF1/2, vAPF1/2, mBASE, vLOUT, vROUT, and related), the processing topology, the 39-tap half-band FIR coefficients for internal 44.1↔22.05kHz sample rate conversion, and the factory preset register values. Secondary references: hitmen.c02.at SPU docs, archived Sony PSX SDK documentation.

**Caveat on nocash:** The psx-spx maintainers have publicly acknowledged that some content was derived from Sony confidential materials. SPU-94 treats nocash as a *factual reference* (register layouts, coefficients, algorithmic behavior — uncopyrightable facts are freely usable) but does not transcribe nocash's explanatory prose, tables, or phrasing into SPU-94's own documentation. All citations are paraphrased with a bibliography entry pointing back to the specific nocash section. This discipline is cheap and preserves options if the nocash text's status is ever challenged.

**Witnesses (not sources):** Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, and MiSTer FPGA PSX core each contain independent implementations of the same spec. They are consulted as *behavioral witnesses* when the spec is ambiguous — their output audio can be diffed against SPU-94's output; their source code is not read as a primary activity. Where they agree and spec is silent, SPU-94 follows consensus and documents it. Where they disagree, Anthony's taste decides and the decision is logged.

**Gray-area philosophy:** The spec is detailed but not exhaustive. Overflow semantics, specific truncation behavior, ordering of register updates within a sample, and DAC reconstruction are all places where spec is incomplete. Every such gap is resolved deliberately, with the resolution documented in `DECISIONS.md`. This document is expected to become a meaningful contribution to PSX reverse-engineering in its own right, independent of the code.

**Hardware future:** Anthony owns an original PS1 and has documented a future Eurorack module concept (`ps1-reverb-eurorack.md`). The choice of plain C for the DSP core — not C++, not Rust — is specifically because C ports cleanly to MCUs (Daisy, Cortex-M), DSP chips (SHARC, Blackfin), and FPGA HLS toolchains, while C++/JUCE does not. Desktop JUCE plugin wraps the C core; hardware shells will wrap the same core differently.

## Constraints

- **Tech stack (core):** Plain C99/C11, no heap allocations in the hot path, no dynamic dispatch, no exceptions, no STL. Self-contained except for a WAV I/O dep (CLI only, not the library). This is a non-negotiable portability constraint.
- **Tech stack (tooling):** Python 3 + numpy + scipy + matplotlib + pytest for test harness and exploration. ctypes (not cffi/pybind11) for the binding layer to minimize maintenance surface.
- **Licensing posture:** Pragmatic original work. nocash + Sony SDK docs + Claude-authored code from spec are the source material. GPL sources (Mednafen, lv2-psx-reverb, DuckStation, MiSTer) are not read as a primary activity; if consulted to resolve a specific ambiguity, the consultation is logged in DECISIONS.md. No copy-paste, no line-by-line translation, no mirroring of source file structure. Final license pick (MIT vs Apache-2.0) deferred to end of M1.
- **Trademark:** "PlayStation", "PS1", "PSX", and the Sony logo are Sony marks. Not used in product names or marketing. Working directory name "PSX Reverb" is internal only; product name is SPU-94.
- **Algorithmic fidelity:** Where the spec is explicit, SPU-94 follows spec. Where the spec is ambiguous and witnesses disagree, the chosen behavior is documented in DECISIONS.md. No silent divergences.
- **Real-time safety:** The DSP core must be real-time safe — no allocations, no locks, no syscalls, no variable-latency operations — even though M1 has no real-time audio path yet. The discipline is set now because retrofitting it later is painful.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Build from spec, not by porting existing emulators | User wants a personal reimplementation, not a copy. Spec is public; implementations are GPL. Preserves licensing flexibility and design agency. | — Pending |
| Plain C for the DSP core (not C++, not Rust) | MCU/FPGA/DSP-chip toolchains all support C cleanly. C++ (especially JUCE-style) does not port to MCU. Rust has no JUCE and weak DSP-chip support. Direct line from spec to code. | — Pending |
| C library + Python bindings (ctypes) + small CLI | Covers all four verification methods (spec checklist, register tests, witness diff, golden files) with best ergonomics. numpy/matplotlib indispensable for gray-area exploration. Core ships as pure C. | — Pending |
| All four verification methods adopted | Project's value depends on defensible accuracy claims; redundant methods catch different classes of error. Spec checklist for coverage, unit tests for isolation, witness diffs for ambiguity, golden files for regression. | — Pending |
| M1 narrow: reverb network + hard clip only | Reverb network alone has weeks of gray-area surface. Shipping a complete, well-tested reverb before layering ADPCM keeps scope honest. | — Pending |
| DECISIONS.md as first-class committed deliverable | Gray-area resolutions *are* part of the project's value, not a side effect. Document becomes a contribution to PSX reverse-engineering independent of the code. | — Pending |
| LEVERS-CATALOG.md annotated during M1 implementation | Lever abstraction is M4 work, but cataloging which registers are musical candidates costs nothing during M1 and makes M4 half-done. | — Pending |
| Ship PS1 factory presets (all 10) as M1 fixtures | Presets are register-config files; free to include; give spec checklist and witness diffs concrete targets; useful as sanity checks even before M4 plugin. | — Pending |
| Linux primary, Daisy/Cortex-M cross-compile smoke test | Linux matches Anthony's workstation. Cortex-M smoke test validates the MCU-portability claim early instead of at M4+ when it's expensive to fix. | — Pending |
| License pick deferred to end of M1 | MIT vs Apache-2.0 is a minor decision with low blast radius; picking now doesn't help. Placeholder LICENSE file until M1 ships. | — Pending |
| SPU-94 is a living instrument, not a preset engine | Anthony's framing: presets are test fixtures, not the product. Every parameter that moves in the original algorithm must be runtime-controllable, glitch-free, and ready for modulation/CV. Shapes M1 API design (mid-stream register writes first-class) and M4 delivery (named musical levers with smoothing). | — Pending |
| Implement 22.05kHz half-rate processing with nocash's 39-tap half-band FIR at both I/O boundaries | The SPU reverb hardware runs internally at 22.05kHz; the 39-tap half-band FIR is how Sony hardware converts to/from the 44.1kHz interface. Implementing this is what makes SPU-94 bit-faithful at the I/O boundary; skipping it is what gives lv2-psx-reverb its known "brightness" deviation from hardware. In the core library (not a boundary adapter) because it's part of what the PS1 console did. | — Pending |
| lv2-psx-reverb explicitly excluded as a witness for frequency-response / sample-rate-accuracy | lv2-psx-reverb's README acknowledges it skips the half-band FIR by design. It remains a valid witness for reverb-network-behavior questions (comb/all-pass structure, register semantics), but cannot be trusted for anything involving spectral accuracy near and above 10kHz. Mednafen and DuckStation to be empirically tested at Phase 4 to determine their FIR implementation status. | — Pending |
| Paraphrase nocash's prose; cite facts with bibliography | psx-spx maintainers acknowledge some content derives from Sony confidential materials. Facts (registers, coefficients, algorithms) are freely usable; nocash's specific wording is not. Cheap discipline that preserves legal options. | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-04-20 — Phase 4 complete (39-tap half-band FIR at both I/O boundaries, folded-form integer arithmetic with D-02 accumulator-width proof, SPU94_LATENCY_SAMPLES=58u public contract, ADR-0012..ADR-0020 filed including lv2-psx-reverb out-of-axis exclusion on frequency response, 38/38 ctest green with `fuzz_fir` 10⁶-step ctypes harness). Phase 3 completed earlier: reverb-network topology (SAME/DIFF IIR + 4-tap comb + APF1/APF2) with ADR-0007..ADR-0011. Next: Phase 5 — `spu94_process` public API + 10 factory presets.*
