# Phase 22: Sample-Rate Conversion & Latency Reporting — Context

**Gathered:** 2026-05-11
**Status:** Ready for planning
**Source:** Carried forward from v1.7 milestone-level discussion (locked decisions in `.planning/PROJECT.md` Key Decisions table + `.planning/milestones/v1.7-REQUIREMENTS.md`). No separate discuss-phase session — Phase-22 decisions were resolved at milestone level.

<domain>
## Phase Boundary

**What this phase delivers:** real-time sample-rate conversion between the host's audio rate and the SPU-94 core's 44.1 kHz internal rate, with the conversion sandwiched around the existing core call. The plugin's reported latency (`setLatencySamples`) must match the conversion's measured group delay so the host's Plugin Delay Compensation keeps the reverb time-aligned with other tracks.

This is the **v1.7 engineering risk hotspot** per research synthesis. SRC is the largest new RT-safety surface (`prepareToPlay` allocation discipline), latency reporting depends on measurement-not-folklore numbers from libsamplerate, and PDC alignment must be confirmed by null-test in a real DAW.

**What this phase does NOT deliver:**
- Float ↔ int16 conversion at the wrapper boundary (Phase 23 — but Phase 22 must leave a clean integration point where Phase 23 will plug in)
- Channel-bus declarations / `isBusesLayoutSupported` (Phase 25)
- Validators as required CI gates (Phase 25)
- State serialisation, automation surface (Phase 24)
- Installers, signing (Phases 26/27)

## Inputs the planner must read

1. `.planning/milestones/v1.7-REQUIREMENTS.md` — PLUG-09..16 (SRC + RT-safety + latency reporting + denormals)
2. `.planning/milestones/v1.7-ROADMAP.md` — Phase 22 section
3. `.planning/research/ARCHITECTURE-v1.7.md` §2 (SRC library tradeoffs) and §6 (RT-safety patterns)
4. `.planning/research/PITFALLS-v1.7.md` B1 (RT-safety regression), B6 (PDC), C3 (denormals)
5. `.planning/research/STACK-v1.7.md` §3 (libsamplerate sourcing notes)
6. `src/plugin/PluginProcessor.h` and `src/plugin/PluginProcessor.cpp` — current `processBlock` + `prepareToPlay`. The existing path runs the SPU core directly with int16 buffers from the WAV loader; Phase 22 introduces the SRC sandwich that wraps the core call.
7. `src/plugin/CMakeLists.txt` — needs `FetchContent` integration for libsamplerate (or equivalent vendoring)
8. Phase 21's commit chain (c495648 / 3f7ab8d / 31b2bd1 / feeff5e) for current build/CMake state
</domain>

<decisions>
## Locked decisions (treat as inputs — do not re-decide)

### SRC library
- **libsamplerate** (Secret Rabbit Code), BSD-2-Clause licensed
- Quality preset: **`SRC_SINC_MEDIUM_QUALITY`** — chosen over Best (transparent improvement is academic, ~3× CPU) and Fastest (audible aliasing on HF content)
- Integration via CMake `FetchContent` pinned to a specific commit SHA (planner chooses the SHA from the libsamplerate upstream tags; record it in PLAN.md for reproducibility)
- A JUCE binding (e.g., `talaviram/juce_libsamplerate`) is optional — planner picks whichever path is cleaner. Direct libsamplerate use is fine; the binding is just convenience.

### Direction and topology
- **Bidirectional** SRC: host_rate → 44.1 kHz at `processBlock` input; 44.1 kHz → host_rate at `processBlock` output
- The SPU-94 C core (`libspu94`) remains untouched. It still consumes and emits 16-bit integer samples at 44.1 kHz. Phase 22 does NOT touch the core or its bit-faithful contract.
- At this phase, the buffer crossing into the core is still int16 (the existing standalone path); the float ↔ int16 conversion that's needed for plugin/host integration arrives in Phase 23. **Phase 22 must integrate cleanly with the existing int16-into-core convention without precluding the Phase 23 float boundary.** This is the trickiest scoping line of the phase.

### Real-time safety (hard rule)
- All SRC state (`SRC_STATE *src_in`, `SRC_STATE *src_out`), scratch buffers, and per-direction context must be allocated in `prepareToPlay`. **Zero allocation, zero locking, zero syscalls inside `processBlock`.**
- Maximum scratch buffer sizes must be computed in `prepareToPlay` from `maximumExpectedSamplesPerBlock`, with a generous headroom factor (planner chooses; typical safe factor is the max-ratio × 2 + tail margin).
- `prepareToPlay` allocates; `releaseResources` releases. `reset` should re-init libsamplerate state (`src_reset`) without re-allocating.

### Fast path at 44.1 kHz
- When `hostSampleRate == 44100.0` (with float-equality tolerance), bypass SRC entirely — no resampling roundtrip, no added latency. Implementation may be a runtime branch inside `processBlock` or a separately-prepared no-op chain that `prepareToPlay` selects. Planner picks the cleaner of the two.

### Latency reporting (`setLatencySamples`)
- Group delay = `SRC_in_delay + core_internal_delay + SRC_out_delay`
- Numbers must be **measured at integration time**, not estimated from libsamplerate documentation. Measurement procedure must be documented in PLAN.md as a task step — e.g., feed an impulse into the chain and locate the centre of the output impulse.
- Latency reporting happens from `prepareToPlay` (or wherever the host wants the number) so the host's PDC graph picks it up before the first `processBlock`.

### Denormals
- Wrap the entirety of `processBlock` with `juce::ScopedNoDenormals`. This prevents subnormal floats (which JUCE plugins routinely produce in reverb tails) from triggering microcode-trap CPU spikes on Intel CPUs.

### Verification: null-test in Reaper
- A null-test must be performed: route a dry signal to track A, the same dry signal through SPU-94 set to fully-dry (no wet) at host SR 48 kHz to track B (polarity-inverted), sum. With correct PDC reporting, the sum should null to silence (or near-silence — SRC isn't perfectly transparent but should give >60 dB null on broadband content). This is a manual / scriptable check during the phase, not a CI gate.
- Reaper on Linux is acceptable. If unavailable, Ardour works for the same null-test.

### Bypass
- Existing `JuceAudioProcessor::getBypassParameter()` semantics are unchanged. The SRC sandwich does NOT need to bypass when host bypass is engaged — the plugin's existing bypass logic handles dry-routing; SRC still runs in the bypass-on case unless it can be safely no-opped without changing output. Planner decides whether bypass-fast-path is worth the complexity for v1.7 — recommended deferral.
</decisions>

<canonical_refs>
- `.planning/milestones/v1.7-REQUIREMENTS.md` (LOCKED — Phase 22 satisfies PLUG-09..16)
- `.planning/milestones/v1.7-ROADMAP.md` (Phase 22 section)
- `.planning/research/ARCHITECTURE-v1.7.md` §2 (SRC library tradeoffs) + §6 (RT-safety)
- `.planning/research/PITFALLS-v1.7.md` B1 / B6 / C3
- `.planning/research/STACK-v1.7.md` §3 (libsamplerate notes)
- `.planning/PROJECT.md` (Key Decisions table — v1.7 SRC library row)
- libsamplerate upstream: `https://github.com/libsndfile/libsamplerate` (planner picks a specific tag/SHA at planning time and records it)
</canonical_refs>

<code_context>
- `src/plugin/PluginProcessor.{h,cpp}` — current `processBlock` reads from `wavSource` (when standalone) and feeds int16 buffers to the SPU core. In plugin formats today the path early-returns silence because `wavSource.loaded` is false.
- `src/plugin/PluginProcessor.cpp:587-595` — currently-empty `getStateInformation` / `setStateInformation` stubs (Phase 24 territory, do not touch).
- `src/plugin/PluginProcessor.cpp:315-320` — early-return-on-silence gate for `!wavSource.loaded || !wavSource.playing`. Phase 22 must rewrite this section so plugin formats actually exercise host-provided buffers through the SRC sandwich. Standalone behaviour stays gated on `wavSource` for back-compat with v1.6.
- `src/spu94/spu94_process.c` (or equivalent) — the core's `spu94_process` entry. UNTOUCHED in this phase.
- libspu94's int16 contract — `int16_t *in`, `int16_t *out`, samples in halfwords. SRC output must be int16 when feeding the core.
</code_context>

<scope_creep_redirects>
DO NOT pull into Phase 22:
- Float ↔ int16 conversion at the float-side boundary (Phase 23). Phase 22 keeps the int16-into-core invariant; the host-float-to-int16 conversion arrives in Phase 23 by inserting a converter inside the SRC sandwich.
- `isBusesLayoutSupported` declarations (Phase 25). Phase 22 can assume stereo-in / stereo-out at the plugin entry point.
- Validator CI gates (Phase 25). Phase 22 may add a non-blocking pluginval RT-safety probe to the existing early-warning workflow, but it does NOT become a merge gate yet.
- `getStateInformation` / `setStateInformation` wiring (Phase 24).
- Automation parameter surface (Phase 24).
- Installers (Phase 26), signing (Phase 27).

If "while we're touching the audio thread anyway, we should..." comes up, capture as a deferred idea, not a Phase 22 task.
</scope_creep_redirects>

<risks_inherited_from_research>
Surface these in PLAN.md so the executor handles them deliberately:

1. **RT-safety regression** (PITFALLS B1) — easiest way to fail Phase 22 is to allocate inside `processBlock`. Mitigation: explicit `prepareToPlay` allocation + a `pluginval --strictness-level 7 --validate-in-process` smoke step that catches audio-thread allocations.
2. **PDC misalignment** (PITFALLS B6) — wrong `setLatencySamples` causes reverb to arrive too early or too late relative to dry tracks. Mitigation: measure group delay empirically, document the procedure in PLAN.md, null-test in Reaper.
3. **Denormal CPU spikes** (PITFALLS C3) — reverb tails generate subnormal floats; without `ScopedNoDenormals`, Intel CPUs can stall mid-block. Already covered by PLUG-16.
4. **Host SR ≠ 44.1 fast-path slip** — code that "looks correct" but accidentally still runs SRC at 44.1 kHz wastes CPU. Mitigation: an explicit test or runtime log line confirming the fast path triggers.
5. **Scratch-buffer sizing** — under-sized scratch buffers cause clipping or silent dropped samples; over-sized buffers waste cache. Planner specifies the sizing formula explicitly.
</risks_inherited_from_research>
