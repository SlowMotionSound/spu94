# SPU-94

## Current State

**Shipped:** M1 — Reverb Core foundation (2026-04-25). 7 phases, 33 plans, 58 tasks complete. The bit-faithful PS1 SPU reverb network is implemented end-to-end as `libspu94`, with Python bindings, a native CLI, the 10 PS1 factory presets, full spec-conformance + golden-file + witness-diff + modulation test infrastructure, and a polished README. 82/82 ctest green. M1 close-out remediation cycle (Steps 1-15 from `ARCHITECTURAL-AUDIT.md`) hardened the test surface and closed every audit-flagged correctness gap; see `.planning/milestones/v1.0-MILESTONE-AUDIT.md` for the post-remediation re-audit (filename uses GSD's internal milestone numbering) and `.planning/MILESTONES.md` for the shipped accomplishments list.

**Shipped:** M2 — Sony 4-bit ADPCM Encode/Decode (2026-04-27, tag `v1.1`). Bit-faithful ADPCM codec added to libspu94 as toggleable coloration stage. 4 phases, 10 plans, 23/23 requirements verified. 380 LOC C core + 841 LOC tests + 30 golden files + 7 ADRs.

**Shipped:** v1.2 — DAC Modeling (2026-04-30, tag `v1.2`). AK4309 interpolation filter + delta-sigma noise model as toggleable DAC coloration stage. Send/return mixer architecture with 3 buses, 6 faders, latency compensation. 5 phases, 12 plans, 14/14 requirements verified. 55 DAC golden files, frequency response characterization script, 99-row coverage map.

**Shipped:** v1.3 — True Oversampled DAC (2026-05-01, tag `v1.3`). Genuine 8x oversampling at 352.8kHz replacing v1.2's single-rate approximation. Sum-of-8 proper decimation, unified HP-shaped noise model, A/B mode toggle across all surfaces. 3 phases, 8 plans. 55 v1.3 DAC golden files, characterization comparison script, ADR-0055.

**Shipped:** v1.4 — Preset System (2026-05-02, tag `v1.4`). Human-readable preset save/load for all 46 engine fields. C core API (`spu94_preset_save` / `spu94_preset_load`), CLI subcommands (`preset-dump`, `--load-preset`), JUCE Save/Load buttons with native file dialogs and custom preset dropdown. 3 phases, 5 plans, 10/10 requirements verified. Integration-level golden round-trip proof (bit-identical audio after save/load).

**Shipped:** v1.5 — Preset Interpolation Engine (2026-05-06, tag `v1.5`). Single morph knob between Sony's 9 factory presets via a bit-identical-at-waypoint linear-interpolation C engine. 280px JUCE rotary with 9 PS1-colored dots, detent snap, Macro/Advanced toggle. 2 phases, 3 plans, 8/8 requirements verified. Replaces archived v1.5/v1.6 macro control approach (gang clamping, Spread/Sweep/Rotate) — archived on branch `archive/v1.5-v1.6-macro-approach`.

**Shipped:** v1.6 — User Programmable Waypoints (2026-05-10, tag `v1.6`). 8 user-programmable waypoint slots at midpoint positions between Sony's 9 anchors, turning the morph dial into a user-customisable 17-position continuum. Per-tick EDIT/EXPORT/LOAD action stack on MorphPanel, SAVE/REVERT edit flow, `.spu94` preset persistence with byte-identical back-compat for pre-feature files. Engine state mirroring overhaul. 3 phases, 4 plans, 17/17 requirements verified.

**Tagged:** `m1-reverb-core`, `v1.0`, `v1.1`, `v1.2`, `v1.3`, `v1.4`, `v1.5`, `v1.6`.

## Current Milestone: v1.7 DAW Plugin Port

**Goal:** Package SPU-94 as a multi-format DAW plugin (VST3 + AU + LV2 + CLAP) on Linux + macOS + Windows so beta testers can run it in their DAWs at any project sample rate and bit depth — feeding back waypoint presets that will inform future musical-range curation. The bit-faithful C core is unchanged; all SR/BD conversion happens in a real-time wrapper layer around it.

**Target features:**
- VST3, AU, LV2, CLAP plugin builds (AU on macOS only — 10 unique binaries across the 3-OS × 4-format matrix)
- Linux + macOS + Windows build matrix with per-OS toolchains and CI
- Real-time sample-rate conversion in the plugin wrapper (host any-rate ↔ 44.1k core)
- Real-time bit-depth conversion (host float32 ↔ int16 core)
- DAW project state persistence (registers + user slots + mixer + toggles + morph position round-trip via `getStateInformation` / `setStateInformation`)
- DAW parameter surface for automation (morph position + key musical controls; exact set defined during requirements)
- Multi-instance behavior verified (each instance independent state)
- Plugin validators pass (pluginval, `auval`, `lv2lint`, VST3 SDK validator)
- Beta-tester distribution via GitHub releases (per-OS / per-format artifacts)
- Manual UAT in Reaper / Ableton Live / Logic / FL Studio at minimum

**Hard constraint:** The C core (`libspu94`) MUST stay bit-faithful and unmodified. SR/BD compatibility is a wrapper concern only — verified by golden regression. Standalone (current v1.6) continues to ship alongside the new plugin formats, not in place of them.

**Out of scope for v1.7:**
- Beta-tester preset return-loop mechanism (how their `.spu94` files reach the maintainer) — settled separately by whatever channel is already in use
- Custom plugin UI redesign — current standalone GUI is reused for the plugin window
- Code signing / notarization for public distribution — testers can override on Mac/Windows; revisit if install friction is reported

## What This Is

SPU-94 is a bit-faithful software reimplementation of the Sony PlayStation 1 SPU reverb algorithm, built from the spec (nocash psx-spx) rather than ported from any existing emulator. It ships as a plain C library with Python bindings for development, and is designed to be wrapped later as a desktop audio plugin (JUCE) and eventually as hardware (Eurorack module, MCU firmware, or FPGA). SPU-94 is designed as a *living instrument*, not a static bank of presets — every parameter that moves in the original algorithm is designed to be controllable at runtime, smoothly and glitch-free, in service of performance, modulation, and CV control. The immediate audience is the author and a small circle of musicians who want the recognizable character of the PS1 reverb available as a modern, playable tool.

## Core Value

**Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.**

Rigor governs the algorithm; musicality governs everything that surrounds it. The algorithm must be faithful; the experience of using it must be alive.

## Requirements

### Validated

- ✓ Implement all 35 SPU registers that affect reverb output with documented behavior — v1.0 (Phases 2 + 3, all 35 SPU94_REG_* enum entries with engine-layer typed setters/getters and 35-entry write-policy table)
- ✓ Implement the all-pass + comb filter network topology on a work buffer — v1.0 (Phase 3, `spu94_reverb_body` with SAME/DIFF IIR + 4-tap comb + APF1/APF2 cascade, mBASE-anchored buffer addressing per ADR-0006)
- ✓ Implement fixed-point arithmetic with integer truncation (not rounding) matching SPU semantics — v1.0 (Phase 1, `q15_mul_truncate` + `sat_s16` + Q15 helpers per ADR-0001 Q15 semantics)
- ✓ Implement hard clip / overflow behavior on the mix bus feeding the reverb — v1.0 (Phase 3, `spu94_hard_clip` with documented INT16_MIN² → INT16_MAX saturation per ADR-0002 vIIR anomaly)
- ✓ Implement the 39-tap half-band FIR at both I/O boundaries — v1.0 (Phase 4, nocash coefficients verbatim; closes the lv2-psx-reverb high-band gap)
- ✓ Design the C API so that mid-stream register updates are a first-class use case — v1.0 (Phase 5, D-08 split write-timing policy + 10⁶-step `fuzz_process` harness)
- ✓ Provide a modulation test — v1.0 (Phase 5 fuzz harness + Phase 7 `modulation_harness.py` with 105 cases × 3 modes; Step 6 added `oob_tap_count == 0` assertion)
- ✓ Expose the core as a plain C library (`libspu94`) with a small, stable public API — v1.0 (Phase 5, `spu94_process` + `spu94_flush` + `spu94_load_preset` + `spu94_presets[]` exported as T/D symbols)
- ✓ Ship Python bindings via ctypes — v1.0 (Phase 6, PYBIND-01..06)
- ✓ Ship a small CLI (`spu94`) for WAV file processing — v1.0 (Phase 6, CLI-01..04, vendored dr_wav + jsmn, native C binary)
- ✓ Build on Linux — v1.0 (continuously since Phase 1; Phase 6 added `manylinux_2_28` wheel via cibuildwheel)
- ✓ Provide spec-conformance test coverage — v1.0 (Phase 7, `docs/COVERAGE.md` with 77 tests mapped to 35 registers, CI-enforced validator)
- ✓ Provide register-level unit tests — v1.0 (Phase 2 + Phase 5 unit suites covering every register family)
- ✓ Provide witness-output diff harness against lv2-psx-reverb — v1.0 (Phase 7, in-process LV2 host at pinned SHA, split-band scipy divergence, per-preset threshold gate landed in M1 close-out Step 12 / ADR-0024)
- ✓ Provide golden-file regression tests — v1.0 (Phase 7, 50 committed `.wav` + `.sha256` pairs, `regenerate_goldens.py` with `--check`, Docker reproducibility)
- ✓ Ship the 10 documented PS1 factory reverb presets — v1.0 (Phase 5, byte-for-byte three-source audit per BIB-011/012/013 priority chain in `.planning/research/05-preset-values-audit-resolutions.md`)
- ✓ Maintain `DECISIONS.md` — v1.0 (24 numbered ADRs + 11 phase-prefixed ADRs covering every gray-area resolution; `docs/DECISIONS.md` is a first-class deliverable per the project framing)
- ✓ Maintain `docs/LEVERS-CATALOG.md` — v1.0 (Phase 7, 35-register catalog with AUTO columns populated: 12 free / 6 sample-quantized / 17 catastrophic; HAND columns seeded for M4)
- ✓ Resolve and document the delay-length-register-change gray area — v1.0 (Phase 5 fuzz harness + ADR-0006 mBASE snap-on-write + ADR-0005 split write-timing policy; mid-stream `m*`/`d*` writes are first-class via TICK_LATCHED semantics)

### Validated — M2 ADPCM (Shipped 2026-04-27, tag `v1.1`)

- ✓ ADPCM decode: 5 filters, round-to-nearest, shift/filter clamping, state carry — v1.1 (Phase 1)
- ✓ ADPCM encode: brute-force 65-combination search, reconstructed state, int64 L2 metric — v1.1 (Phase 1)
- ✓ Pipeline integration: toggleable upstream stage, 28-sample latency, default-off, rt_safety clean — v1.1 (Phase 2)
- ✓ I/O layer: CLI subcommands, VAG format, Python ctypes, JUCE toggle — v1.1 (Phase 3)
- ✓ Verification: coverage maps, 30 ADPCM goldens + regression gate, 7 ADRs (0047-0053) — v1.1 (Phase 4)

See `.planning/milestones/v1.1-REQUIREMENTS.md` for full 23-requirement traceability.

### Validated — v1.2 DAC Modeling (Shipped 2026-04-30, tag `v1.2`)

- ✓ AK4309 8x cascaded half-band FIR designed in scipy, ported to Q15 C — v1.2 (Phase 5-6)
- ✓ Delta-sigma noise model: LFSR + 2nd-order HP shaping, +12dB/octave slope — v1.2 (Phase 6)
- ✓ Send/return mixer: 3 buses, 6 faders, latency compensation, DAC coloration section — v1.2 (Phase 7)
- ✓ I/O surface: CLI --dac, Python ctypes, JUCE 4-zone GUI with mixer/DAC controls — v1.2 (Phase 8)
- ✓ Verification: 55 DAC goldens, frequency response characterization, 4 integration C tests, 99-row coverage map — v1.2 (Phase 9)

See `.planning/milestones/v1.2-REQUIREMENTS.md` for full 14-requirement traceability.

### Out of Scope (shipped or deferred, not abandoned)

- **4-bit Sony ADPCM encode/decode** — SHIPPED as M2 / v1.1 (2026-04-27)
- **AK4309 DAC digital modeling** — SHIPPED as v1.2 (2026-04-30)
- **Real oversampling engine** — ACTIVE as v1.3 (True Oversampled DAC)
- **DAC analog output stage** (op-amps, coupling caps, output impedance) — deferred; needs real hardware measurement
- **JUCE DAW plugin (VST3 / AU / LV2 / CLAP)** — ACTIVE as v1.7 (DAW Plugin Port); wraps the existing C core for use in Reaper / Ableton / Logic / FL Studio etc.
- **Named musical levers ("Room Size", "Pre Delay", etc.), parameter smoothing, CV mappings, plugin UI** — superseded by v1.5 interpolation engine approach for initial musical controls. Individual register levers may return as decoupled parameters on top of the interpolation base.
- **Tempo-synced taps, BPM subdivision snapping** — archived with v1.5/v1.6 macro approach. May return as a layer on top of interpolation engine.
- **Hardware validation via PSX homebrew + digital capture** — deferred to Milestone 5; Anthony has an original PSX
- **Eurorack module** — explicitly future direction, separately documented in `ps1-reverb-eurorack.md`
- **FPGA implementation** — future direction; C core chosen specifically to keep FPGA HLS path open
- **MCU firmware port (Daisy, Cortex-M)** — Phase 8 (cross-compile smoke test) was scoped for v1.0 but parked per Anthony's 2026-04-24 decision; moves to between M4 and M5 as a portability gate. The portability claim is upheld by the M1 design discipline (no heap, no locks, no syscalls, no STL) and the existing `arm-none-eabi-gcc` build infrastructure that the v1.0 codebase already supports — the smoke test landing is a paper formality, not a discovery.
- **SPU voice engine, envelope generation, pitch modulation, noise, ADSR** — out of scope for the entire project; reverb-only reimplementation
- **Windows and macOS builds** — ACTIVE as v1.7; brought in alongside the DAW plugin port so beta testers on those OSes can participate
- **Reading Mednafen / lv2-psx-reverb / DuckStation / MiSTer source as a primary development activity** — excluded by licensing posture (see Constraints)

## Context

Shipped v1.4 additions: `spu94_preset_io.c` (259 LOC serializer/parser), `cmd_preset_dump.c` (155 LOC CLI subcommand), JUCE Save/Load GUI with custom preset dropdown and modified-state asterisk. Total project C LOC: ~8,200. Total ctest: 107 (47 C unit tests + 60 Python/CLI/packaging tests).

Shipped v1.1 additions: 380 LOC ADPCM C core (`spu94_adpcm.c`, `spu94_adpcm_encode.c`, `vag.c`), 841 LOC unit tests, 30 ADPCM golden WAV files with SHA-256 sidecars, 7 numbered ADRs (ADR-0047 through ADR-0053).

Shipped v1.0 totals: ~33,000 LOC across `src/spu94/` (C core), `src/cli/` (CLI), `python/spu94/` (binding), `tests/` (35 directories of test code), `docs/` (5 first-class deliverables — DECISIONS, BIBLIOGRAPHY, COVERAGE, LEVERS-CATALOG, plus README).

Tech stack realities at v1.0 close: plain C99 core (zero heap in hot path verified by `nm -u` + grep-guard CI gate), Python 3.10+ binding via ctypes (no cffi/pybind11), scikit-build-core wheel build, cibuildwheel for `manylinux_2_28` Linux distribution, dr_wav + jsmn vendored for CLI WAV/JSON I/O (CLI-link only — `verify-no-drwav-in-libspu94.sh` gate confirms `libspu94.so` stays vendor-symbol-free), Unity for unit tests, pytest for integration + binding + CLI + packaging tests, pytest-benchmark report-only timing harness.

**Domain:** The PS1 SPU reverb is a Schroeder/Gardner-style network — all-pass filters and comb filters operating on a shared reverb work buffer in SPU RAM — whose musicality comes not from algorithmic sophistication but from *implementation artifacts*: 16-bit fixed-point arithmetic with truncation at each stage, hard-clipping overflow behavior, ADPCM source coloration, and period-appropriate DAC coloration. "Bit-accuracy is not optional — it is the sound." Any reimplementation that rounds instead of truncates, or replaces the fixed-point math with floats, loses the character.

**Primary reference:** nocash's psx-spx documentation (problemkaputt.de / psx-spx.consoledev.net) documents every SPU register that affects reverb (vIIR, vWALL, dAPF1/2, dCOMB1-4, vCOMB1-4, dLSAME/dRSAME, dLDIFF/dRDIFF, dLAPF1/2, dRAPF1/2, vAPF1/2, mBASE, vLOUT, vROUT, and related), the processing topology, the 39-tap half-band FIR coefficients for internal 44.1↔22.05kHz sample rate conversion, and the factory preset register values. Secondary references: hitmen.c02.at SPU docs, archived Sony PSX SDK documentation.

**Caveat on nocash:** The psx-spx maintainers have publicly acknowledged that some content was derived from Sony confidential materials. SPU-94 treats nocash as a *factual reference* (register layouts, coefficients, algorithmic behavior — uncopyrightable facts are freely usable) but does not transcribe nocash's explanatory prose, tables, or phrasing into SPU-94's own documentation. All citations are paraphrased with a bibliography entry pointing back to the specific nocash section. This discipline is cheap and preserves options if the nocash text's status is ever challenged.

**Witnesses (not sources):** Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, and MiSTer FPGA PSX core each contain independent implementations of the same spec. They are consulted as *behavioral witnesses* when the spec is ambiguous — their output audio can be diffed against SPU-94's output; their source code is not read as a primary activity. v1.0's witness-diff harness pins lv2-psx-reverb at SHA `424e1e8...` with split-band aligned-RMS divergence per the per-preset thresholds in `config/witness_diff_thresholds.json` (ADR-0024). Where they agree and spec is silent, SPU-94 follows consensus and documents it. Where they disagree, Anthony's taste decides and the decision is logged.

**Gray-area philosophy:** The spec is detailed but not exhaustive. Overflow semantics, specific truncation behavior, ordering of register updates within a sample, and DAC reconstruction are all places where spec is incomplete. Every such gap is resolved deliberately, with the resolution documented in `DECISIONS.md`. v1.0 ships 24 numbered ADRs (ADR-0001 through ADR-0024) plus 11 phase-prefixed ADRs covering every gray-area resolution from the M1 cycle.

**Hardware future:** Anthony owns an original PS1 and has documented a future Eurorack module concept (`ps1-reverb-eurorack.md`). The choice of plain C for the DSP core — not C++, not Rust — is specifically because C ports cleanly to MCUs (Daisy, Cortex-M), DSP chips (SHARC, Blackfin), and FPGA HLS toolchains, while C++/JUCE does not. Desktop JUCE plugin wraps the C core; hardware shells will wrap the same core differently.

## Constraints

- **Tech stack (core):** Plain C99/C11, no heap allocations in the hot path, no dynamic dispatch, no exceptions, no STL. Self-contained except for a WAV I/O dep (CLI only, not the library). Verified at v1.0 by `nm -u libspu94.so` + grep-guard + 4 `rt_safety` ctest targets.
- **Tech stack (tooling):** Python 3.10+ + numpy + scipy + matplotlib + pytest for test harness and exploration. ctypes (not cffi/pybind11) for the binding layer to minimize maintenance surface.
- **Licensing posture:** Pragmatic original work. nocash + Sony SDK docs + Claude-authored code from spec are the source material. GPL sources (Mednafen, lv2-psx-reverb, DuckStation, MiSTer) are not read as a primary activity; if consulted to resolve a specific ambiguity, the consultation is logged in DECISIONS.md. No copy-paste, no line-by-line translation, no mirroring of source file structure. Final license pick (MIT vs Apache-2.0) deferred — decide before public release.
- **Trademark:** "PlayStation", "PS1", "PSX", and the Sony logo are Sony marks. Not used in product names or marketing. Working directory name "PSX Reverb" is internal only; product name is SPU-94.
- **Algorithmic fidelity:** Where the spec is explicit, SPU-94 follows spec. Where the spec is ambiguous and witnesses disagree, the chosen behavior is documented in DECISIONS.md. No silent divergences. The witness-diff threshold gate (Step 12 / ADR-0024) is the runtime regression gate for this constraint.
- **Real-time safety:** The DSP core must be real-time safe — no allocations, no locks, no syscalls, no variable-latency operations — verified at v1.0 by 4 `rt_safety` ctest targets (`rt_no_heap`, `rt_no_locks`, `rt_no_syscalls`, `rt_bench_latency`) all green; observed (p99-median)/median ratio 0.741 against threshold 2.0.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Build from spec, not by porting existing emulators | User wants a personal reimplementation, not a copy. Spec is public; implementations are GPL. Preserves licensing flexibility and design agency. | ✓ Good — v1.0 shipped with zero GPL-source-derived code; nocash + hitmen + Sony SDK as factual references only |
| Plain C for the DSP core (not C++, not Rust) | MCU/FPGA/DSP-chip toolchains all support C cleanly. C++ (especially JUCE-style) does not port to MCU. | ✓ Good — `libspu94.so` is heap-free, lock-free, syscall-free; portability claim supported by the design discipline even though Phase 8's smoke test was parked |
| C library + Python bindings (ctypes) + small CLI | Covers all four verification methods with best ergonomics. numpy/matplotlib indispensable for gray-area exploration. | ✓ Good — Phase 6 + Phase 7 both rode this triangle; ctypes + numpy zero-copy contract proved out at scale via 10⁶-step fuzz |
| All four verification methods adopted | Project's value depends on defensible accuracy claims; redundant methods catch different classes of error. | ✓ Good — spec checklist (Phase 7 COVERAGE.md) + register tests (Phase 2/5) + witness diffs (Phase 7 + Step 12 gate) + golden files (Phase 7) all green at v1.0 |
| M1 narrow: reverb network + hard clip only | Reverb network alone has weeks of gray-area surface. Shipping a complete, well-tested reverb before layering ADPCM keeps scope honest. | ✓ Good — v1.0 shipped on schedule; ADPCM is M2 |
| DECISIONS.md as first-class committed deliverable | Gray-area resolutions *are* part of the project's value. | ✓ Good — 24 numbered ADRs + 11 phase-prefixed at v1.0; `docs/DECISIONS.md` is a meaningful PSX-reverse-engineering contribution in its own right |
| LEVERS-CATALOG.md annotated during M1 implementation | Lever abstraction is M4 work, but cataloging which registers are musical candidates costs nothing during M1. | ✓ Good — 35-register catalog AUTO columns done at v1.0 (12 free / 6 sample-quantized / 17 catastrophic); HAND columns seeded for M4 |
| Ship PS1 factory presets (all 10) as M1 fixtures | Presets are register-config files; free to include; give witness diffs concrete targets. | ✓ Good — 10 presets shipped with three-source audit (BIB-011/012/013 priority chain) |
| Linux primary, Daisy/Cortex-M cross-compile smoke test | Linux matches Anthony's workstation. | ⚠ Revisit — Linux primary good; Cortex-M smoke test (Phase 8) parked per 2026-04-24 decision, moves to between M4 and M5 |
| License pick deferred | MIT vs Apache-2.0 is a minor decision with low blast radius. | ⚠ Revisit — pick outstanding; decide before public release |
| SPU-94 is a living instrument, not a preset engine | Anthony's framing: presets are test fixtures, not the product. | ✓ Good — Phase 7 modulation harness empirically proved every register but vAPF1 clean through 11 kHz under modulation, validating the M4 lever direction |
| Implement 22.05kHz half-rate processing with nocash's 39-tap half-band FIR | The SPU reverb hardware runs internally at 22.05kHz; the FIR is what makes SPU-94 bit-faithful at the I/O boundary. | ✓ Good — Phase 4 shipped FIR with bit-identity to a hand-audited integer reference; Phase 7 witness-diff confirms the high-band brightness gap closes vs lv2 |
| lv2-psx-reverb explicitly excluded as a witness for frequency-response | lv2-psx-reverb's README acknowledges it skips the half-band FIR. | ✓ Good — ADR-Phase-4-I documents the high-band exclusion; Step 12's witness-diff threshold gate respects it |
| Paraphrase nocash's prose; cite facts with bibliography | psx-spx maintainers acknowledge some content derives from Sony confidential materials. | ✓ Good — v1.0 ships zero verbatim-nocash sentences; `docs/BIBLIOGRAPHY.md` 20 entries cite every fact used |
| Default Python work_buf_size = SPU94_WORK_BUF_MAX_BYTES | M1 close-out audit found the original 8192-byte default silently degraded Hall-and-larger presets. | ✓ Good — Step 5 (commit fdeeb57) closed; ADR-0022 (work-buf contract) + ADR-0023 (oob_tap_count observable) + Step 6 assertion floor lock in the regression gate |
| Per-preset witness-diff threshold gate (ADR-0024) | Witness-diff harness was measurement-only by design; M1 close-out lands the deferred follow-up gate. | ✓ Good — Step 12 (commit 009b636); thresholds deliberately liberal at v1.0 (regression gate not correctness gate); M2 will tighten after lv2 baseline calibration |
| All DSP signal flow lives in the C core, not host layers | C core is the product — JUCE/CLI/Python are thin wrappers. Wet/dry crossfade and DAC model must be C-core responsibilities so every consumer gets identical behavior. Hosts should never contain DSP logic. | Pending — Phase 7 will move wet/dry from JUCE into C core |
| Send/return mixer architecture | Three-bus design (dry, patina/ADPCM, reverb) with independent faders summed at a master mixer, followed by DAC model on/off. Avoids cascading wet/dry phase-alignment problems. Two independent reverb sends (dry send + patina send) let user control what feeds the reverb. Six controls: input gain, dry fader, patina fader, dry reverb send, patina reverb send, reverb fader, plus DAC toggle. | Pending — Phase 7 implementation |
| ADPCM position may become movable in future | User wants flexibility to place ADPCM encode/decode at different points in the signal chain (e.g. after reverb instead of before). Not building now, but avoid designs that hardwire ADPCM position. | Future — don't paint ourselves into a corner |
| Preset interpolation engine replaces macro control approach | v1.5/v1.6 macro engine (gang clamping, Spread/Sweep/Rotate, safety constraints, Sync/Free snap) proved too complex — interlocking constraints fought each other, GUI exposed complexity rather than taming it. Interpolation between Sony's known-good presets eliminates clamping by construction. | ✓ Good — design confirmed 2026-05-05; v1.5 shipped on this design; archived code recoverable on `archive/v1.5-v1.6-macro-approach` branch |
| Bit-faithful C core untouchable through the DAW plugin port | DAW hosts deliver audio at any sample rate and bit depth. SR/BD compatibility is a wrapper concern only — float→int16 in / SRC to 44.1k / core untouched / SRC back / int16→float out. Same architectural principle as the standalone WAV loader, just real-time. | Pending — v1.7 (DAW Plugin Port) |

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
*Last updated: 2026-05-10 — v1.7 DAW Plugin Port milestone started.*
