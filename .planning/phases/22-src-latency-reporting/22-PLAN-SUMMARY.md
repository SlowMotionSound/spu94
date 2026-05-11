---
phase: 22-src-latency-reporting
plan: 01
subsystem: plugin
tags: [libsamplerate, src, latency, pdc, denormals, juce, rt-safety, scopednodenormals]

# Dependency graph
requires:
  - phase: 21-build-skeleton-ci-matrix
    provides: spu94_plugin target name, 4-format Linux build matrix (Standalone/VST3/LV2/CLAP), cmake/clap_juce_extensions.cmake parallel-shim pattern, pluginval-early-warning advisory CI job at strictness-7
provides:
  - libsamplerate (BSD-2-Clause) integrated via cmake/libsamplerate.cmake FetchContent shim pinned to SHA 2ccde9568cca73c7b32c97fefca2e418c16ae5e3
  - SrcChain class - bidirectional SRC sandwich (host_SR <-> 44100) using SRC_SINC_MEDIUM_QUALITY, two SRC_STATE handles per direction (one per channel)
  - prepareToPlay-allocated SRC state + scratch buffers; processBlock is allocation-free (PLUG-12)
  - 44.1 kHz host SR fast-path bypass (no libsamplerate calls; straight float<->int16 conversion)
  - Impulse-response-measured group delay reported via setLatencySamples (centre-of-energy formula, kMeasureLen=4096)
  - juce::ScopedNoDenormals wraps the entire processBlock body (first statement)
  - Plugin formats (VST3/LV2/CLAP) now process host audio - the v1.6 "silent in plugin wrappers" gate is gone, replaced with a wrapperType_Standalone-gated split
affects: [phase-23-bit-depth-conversion, phase-24-state-persistence, phase-25-validators-and-buses, phase-26-installers]

# Tech tracking
tech-stack:
  added:
    - "libsamplerate (libsndfile/libsamplerate, BSD-2-Clause) pinned at 2ccde9568cca73c7b32c97fefca2e418c16ae5e3"
  patterns:
    - "cmake/libsamplerate.cmake FetchContent shim mirroring cmake/clap_juce_extensions.cmake (one-file swap path for find_package(SampleRate))"
    - "prepareToPlay-owns-allocation discipline: SRC_STATE, scratch buffers, impulse measurement all run in srcChain_.prepare(); processBlock is allocation-free"
    - "Per-channel mono SRC_STATE handles instead of libsamplerate's interleaved-channels mode - simpler bookkeeping, lower cache pressure"
    - "Pull-callback userdata model: PullCtx { data, remaining } one-shot per processIn/processOut call"
    - "Group delay measured at integration time via impulse response with centre-of-energy formula (sum(i*y[i]^2)/sum(y[i]^2)) instead of estimated from library docs"
    - "Fast-path tolerance: std::abs(hostSR - 44100.0) < 1.0e-9 (some hosts represent 44.1 kHz as 44099.999... due to internal double conversion)"
    - "Plugin/standalone branching in processBlock keyed on wrapperType == wrapperType_Standalone; v1.6 WavSource gate stays intact for the standalone testbed"

key-files:
  created:
    - "cmake/libsamplerate.cmake"
    - "src/plugin/SrcChain.h"
    - "src/plugin/SrcChain.cpp"
  modified:
    - "src/plugin/CMakeLists.txt (include the shim; link samplerate; add SrcChain.cpp to target_sources)"
    - "src/plugin/PluginProcessor.h (SrcChain member, hostSampleRate_ cache)"
    - "src/plugin/PluginProcessor.cpp (prepareToPlay: srcChain_.prepare + setLatencySamples; releaseResources: srcChain_.release; processBlock: ScopedNoDenormals first statement, plugin-vs-standalone branch, SRC sandwich)"

key-decisions:
  - "libsamplerate pinned at SHA 2ccde9568cca73c7b32c97fefca2e418c16ae5e3 (libsndfile/libsamplerate master, 2025-08-28). FetchContent_Declare with GIT_SHALLOW for fast fetch. Tests/examples/install disabled."
  - "SRC_SINC_MEDIUM_QUALITY preset, one SRC_STATE per channel per direction (4 total on stereo) - matches juce_libsamplerate convention. Allocation amortised in prepare()."
  - "Scratch sizing formula: core-side coreScratchN = maxBlock+32; host-side hostScratchN = maxBlock*5+32 - the *5 covers 192 kHz hostSR upsampling (192/44.1 = 4.36, ceil to 5), +32 headroom is well above the Sinc-Medium ~121-tap polyphase response."
  - "Group delay measured at prepare() with impulse response + centre-of-energy formula, kMeasureLen=4096. Both SRC directions measured separately; result expressed in host samples (input SRC: centroid * hostSR/44100; output SRC: centroid is already at host rate)."
  - "Fast-path is a runtime branch inside processIn/processOut keyed on a prepare-time-computed isFastPath_ bool, NOT a separately-prepared no-op chain. Rationale: a single code path is simpler, the per-block branch predicts trivially, and the cost is below noise floor."
  - "setLatencySamples is called every prepareToPlay (the only correct policy per PITFALLS B6: some hosts re-poll on transport start, others ignore mid-stream changes)."
  - "Core latency conversion to host samples uses ceil(): under-reporting drifts wet AHEAD of dry which is the bad direction; over-reporting by <= 1 sample is the safe failure mode."
  - "Plugin-vs-standalone branch keyed on wrapperType == wrapperType_Standalone preserves v1.6 WavSource gate byte-identically for the standalone testbed and lets plugin formats process host audio."
  - "ScopedNoDenormals MUST be the first statement of processBlock - RAII guarantees DAZ/FTZ are set for the entire block including SRC sandwich and core call, preventing denormal microcode-trap stalls on Intel CPUs (PLUG-16 / R3 / PITFALLS C3)."

patterns-established:
  - "Pull-callback userdata struct (PullCtx) is a static-pure data-pipe primitive for libsamplerate; per-channel instances live as private members of SrcChain."
  - "Impulse-based group-delay measurement runs through the SAME SRC_STATE that subsequently runs in processBlock, so any quality-setting/filter-tap variation is captured automatically (R2 mitigation)."
  - "Allocation-free convention: every audio-thread function explicitly documents the absence of new/malloc/vector/string/Logger/Mutex calls. RT-safety is a contract, not a hope."

requirements-completed:
  - PLUG-09
  - PLUG-10
  - PLUG-11
  - PLUG-12
  - PLUG-13
  - PLUG-14
  - PLUG-16

# PLUG-15 status: PENDING UAT (see "Verification" section below)

# Metrics
duration: 1h 20m
completed: 2026-05-11
---

# Phase 22 Plan 01: Sample-Rate Conversion & Latency Reporting Summary

**libsamplerate-backed bidirectional SRC sandwich around the SPU-94 C core, with measured group-delay latency reporting and ScopedNoDenormals across processBlock - plugin formats now process host audio at any sample rate, not just 44.1 kHz.**

## Performance

- **Duration:** ~1h 20m
- **Started:** 2026-05-11T19:00:00Z (approx, fresh `/clear` session)
- **Completed:** 2026-05-11T20:19:29Z
- **Tasks:** 3 of 4 committed (1 manual UAT pending, see PLUG-15 below)
- **Files modified:** 6 (3 created, 3 modified)

## Accomplishments

### Task 1 - libsamplerate FetchContent shim (commit `72660bd`)

- Created `cmake/libsamplerate.cmake` mirroring `cmake/clap_juce_extensions.cmake`. FetchContent_Declare at SHA `2ccde9568cca73c7b32c97fefca2e418c16ae5e3` (libsndfile/libsamplerate master 2025-08-28), GIT_SHALLOW for fast fetch. Disabled examples/install/tests so only the `samplerate` static target is produced.
- Wired `include(.../cmake/libsamplerate.cmake)` + added `samplerate` to `spu94_plugin`'s PRIVATE link list in `src/plugin/CMakeLists.txt`.
- Verified `build/_deps/libsamplerate-src/COPYING` is BSD-2-Clause and the fetched HEAD matches the pinned SHA exactly.
- All four Linux plugin formats (Standalone, VST3, LV2, CLAP) build green at this commit; no source file consumes `<samplerate.h>` yet (Task 2 does).

### Task 2 - SrcChain skeleton (commit `496e7eb`)

- `src/plugin/SrcChain.h` (119 LOC): public `prepare/release/reset/processIn/processOut` + `isFastPath()` + `getMeasuredLatencyHostSamples()` + RT-safe debug counter `getSrcCallbacksThisBlock()` / `resetSrcCallbacksCounter()`. Forward-declares `SRC_STATE` so consumers of this header don't drag in `<samplerate.h>`.
- `src/plugin/SrcChain.cpp` (367 LOC): full libsamplerate pull-callback driver with per-channel mono SRC_STATE, scratch buffers as `juce::HeapBlock<float>`, impulse-based group-delay measurement, and a fast-path branch keyed on `isFastPath_`.
- `prepare()` runs the impulse measurement through the SAME `SRC_STATE` handles that processBlock subsequently uses (R2 mitigation), then `src_reset` to clear the impulse transient before runtime starts.
- `processIn`/`processOut` are allocation-free: `std::memcpy` + `src_callback_read` + inline clamp+truncate (no `std::clamp` here - bespoke `f32_to_s16` / `s16_to_f32` inlines for speed) + atomic relaxed `fetch_add`. No new/malloc/vector/string/Logger/Mutex.
- Class compiles cleanly across all four Linux plugin formats; not yet wired into PluginProcessor (Task 3).

### Task 3 - PluginProcessor integration (commit `976553b`)

- `PluginProcessor.h`: `#include "SrcChain.h"`, added private `SrcChain srcChain_` and `double hostSampleRate_` members.
- `prepareToPlay`: after existing engine teardown + spu94_init + preset load + mixer defaults, caches `hostSampleRate_`, clamps `samplesPerBlock` to the `kMaxBlock=4096` stack ceiling, calls `srcChain_.prepare(sampleRate, maxBlock, 2)`, then calls `setLatencySamples(srcChain_.getMeasuredLatencyHostSamples() + ceil(spu94_get_total_latency_samples(engines[0]) * sampleRate/44100))`. ceil() ensures we over-report by at most 1 sample.
- `releaseResources`: added `srcChain_.release()` after engine teardown.
- `processBlock`: `juce::ScopedNoDenormals noDenormals;` is the FIRST statement (PLUG-16, line 136). `srcChain_.resetSrcCallbacksCounter()` runs second so the R4 verification accessor reads a clean per-block number. The unconditional state-management block (new-WAV swap, preset queue, file-preset load, morph re-apply, shadow sync, mixer/DAC state push) is UNCHANGED.
- New audio-I/O split: `isStandalone = (wrapperType == wrapperType_Standalone)`.
  - **Standalone path**: unchanged from end-of-Phase-21; preserves the v1.6 WavSource Load->Play->Stop gate.
  - **Plugin path**: `srcChain_.processIn` -> `spu94_process` -> `srcChain_.processOut` -> side-channel limiter on the host-rate float output. Stack int16 scratch 4 x `kMaxBlock` (32 KiB total). SRC drift handling: if `hostNOut < n` the tail is held at the last produced sample.

### Task 4 - Manual UAT null-test (PENDING - see Verification below)

Procedure documented; execution deferred to the user (see "Manual UAT pending" subsection).

## Verification

### What was verified locally (executor)

| What | Result |
|------|--------|
| `cmake -S . -B build` clean configure | PASS - libsamplerate fetched at correct SHA, COPYING is BSD-2-Clause |
| All 4 Linux plugin formats build green (Standalone, VST3, LV2, CLAP) | PASS - all artifacts present under `build/src/plugin/spu94_plugin_artefacts/` |
| `ScopedNoDenormals` is first statement of processBlock | PASS - line 136 of PluginProcessor.cpp (grep + manual read) |
| `setLatencySamples` called from prepareToPlay with measured + core formula | PASS - line 112 of PluginProcessor.cpp |
| `SRC_SINC_MEDIUM_QUALITY` literal usage count | PASS - exactly 2 in SrcChain.cpp (one per direction) |
| Fast-path tolerance is 1.0e-9 against 44100.0 | PASS - SrcChain.cpp line 104 |
| `srcChain_.processIn` / `processOut` both appear in plugin branch of processBlock | PASS - lines 432, 446 |
| Manual audio-thread allocation scan of processBlock (no new/malloc/vector/string/Logger/DBG/Mutex/CriticalSection) | PASS |
| Manual RT-safety read of `SrcChain::processIn` and `::processOut` | PASS - only memcpy, `src_callback_read`, inline arithmetic, atomic relaxed fetch_add, jassert |
| Standalone branch byte-identity with end-of-Phase-21 | PASS - WavSource gate, int16 scratch arrays, spu94_process call, side-channel limiter constants (kSideKnee=0.125, kSideCeiling=0.06) all preserved |

### PLUG-15 - manual UAT pending

**Status: PENDING UAT.** The executor (this CLI agent) cannot interactively drive Reaper or Ardour - track routing, polarity invert, hit play, read a JS spectrum/RMS meter, and listen for pre/late echo is a human-in-the-loop activity. Per the project's global Claude instructions (deployed-system work executes as a hands-on guided walkthrough), this is left for Anthony to run.

System availability check:
- Reaper: not installed.
- Ardour: installed (`/usr/bin/ardour`).

#### UAT procedure to execute (Ardour 8+ at 48 kHz host SR)

1. Open Ardour. New Session -> Sample Rate 48000 Hz, Stereo Master out.
2. Create three audio tracks:
   - **Track 1 (Dry)**: insert a noise/pink-noise source (e.g. import a 10-second pink-noise WAV item, or use the built-in "white-noise" plugin if present). Output to Master.
   - **Track 2 (Plugin)**: receive the SAME audio source as Track 1 (route via a Bus or duplicate the item). Insert SPU-94 VST3 (`build/src/plugin/spu94_plugin_artefacts/VST3/SPU-94.vst3`) or LV2/CLAP. On the plugin GUI, set:
     - **Dry Level: 1.0**, **ADPCM Level: 0.0**, **Reverb Level: 0.0** (passthrough mode).
   - **Track 3 (Sum)**: receive from Track 1 (normal polarity) and Track 2 (POLARITY INVERTED via Ardour's polarity-flip on the track input). Output to Master. Mute the Master output of Tracks 1 and 2 so only the sum is monitored.
3. Hit Play. The summed audio is the residual of (dry - plugin_passthrough). With correct PDC the residual is silence + SRC roundtrip coloration noise floor.
4. Measure the residual RMS over a 5-10 second window of broadband noise (Ardour's built-in meter, or `a-Inverter` + `a-Meter` + `EQ Spectrum` plugins).
5. **Pass criterion: residual ≤ -60 dBFS RMS.** Expected range -60 to -90 dBFS (libsamplerate Sinc-Medium gives ~120 dB SNR best-case; the roundtrip isn't perfectly transparent).
6. **Fail criterion: residual > -60 dBFS, or visible signal-shaped energy (pre/late echo) in the spectrum.** That indicates PDC misalignment - setLatencySamples is reporting a wrong number. Diagnosis:
   - Verify `srcChain_.getMeasuredLatencyHostSamples()` returns a positive integer at 48 kHz (add a temporary debug log in `prepareToPlay` and observe the printout when the host loads the plugin).
   - Verify the conversion formula uses `ceil(coreLatency44k * sampleRate/44100)` (not `floor` / `round`).
   - Verify `setLatencySamples` runs BEFORE the first `processBlock` (JUCE guarantees `prepareToPlay` ordering).

#### To complete this requirement

After running the UAT, update this SUMMARY with the residual RMS:

```
PLUG-15 result: residual = -XX.X dBFS RMS at 48 kHz host SR, Ardour 8.x, 10-second pink-noise window. PASS / FAIL.
```

If the test fails, the most likely cause is a sign/scale error in the latency conversion - we ceil() and we don't apply the SRC measurement in fast-path mode (which is the right behaviour at 44.1 kHz but worth eyeballing if 48 kHz is wrong).

## Goal-Backward Verification Mapping

| Truth | Requirement | Status | Evidence |
|-------|-------------|--------|----------|
| libsamplerate (BSD-2) in build graph at pinned SHA | PLUG-09 | PASS | `build/_deps/libsamplerate-src/.git/HEAD` resolves to `2ccde9568cca73c7b32c97fefca2e418c16ae5e3`; COPYING is BSD-2-Clause |
| Every SRC_STATE constructed with SRC_SINC_MEDIUM_QUALITY | PLUG-10 | PASS | `grep -c "SRC_SINC_MEDIUM_QUALITY" src/plugin/SrcChain.cpp` = 2 (one per direction) |
| Bidirectional SRC, both directions exercised per non-fast-path block | PLUG-11 | PASS | `srcIn_[2]` and `srcOut_[2]` constructed in `prepare()`; both called via `src_callback_read` inside processIn/processOut; `srcCallbacksThisBlock_` increments in non-fast-path branch |
| Zero allocation/locks/syscalls in processBlock | PLUG-12 | PASS | Manual scan of `processBlock` + `SrcChain::processIn` + `SrcChain::processOut` returns clean. pluginval `--strictness-level 7 --validate-in-process` advisory CI job (already in plugins.yml from Phase 21) catches regressions. |
| 44.1 kHz fast-path, no SRC calls, latency = core only | PLUG-13 | PASS | `isFastPath_ = std::abs(hostSR - 44100.0) < 1.0e-9`; fast-path branch in processIn/processOut does raw f32<->s16; `measuredLatencyHostSamples_ = 0` in fast-path; setLatencySamples reduces to just core latency |
| setLatencySamples = measured_in + core_in_host + measured_out, measured via impulse | PLUG-14 | PASS | `measureGroupDelayInHostSamples()` uses centre-of-energy on impulse response (kMeasureLen=4096), runs inside prepare(); `setLatencySamples` called at line 112 of PluginProcessor.cpp |
| Reaper/Ardour null-test residual ≤ -60 dBFS at 48 kHz | PLUG-15 | **PENDING UAT** | Procedure documented above. Executor cannot drive an interactive DAW session; user runs the test and updates this SUMMARY with the residual. |
| ScopedNoDenormals first statement of processBlock | PLUG-16 | PASS | Line 136 of PluginProcessor.cpp; RAII covers entire block |

## Deviations from Plan

None of Rules 1-4 fired. The plan was executed as written, with these small implementation-detail choices left to the executor:

1. **`f32_to_s16` / `s16_to_f32` written as bespoke inlines** (in an anonymous namespace inside SrcChain.cpp) instead of `std::clamp` + cast. Both are correct; the inline form is one branch faster per sample and matches the existing standalone WavLoader idiom. Truncate semantics preserved (no rounding) per the project's North Star.

2. **Plugin-branch drift handling: hold last sample** when `hostNOut < n` (off-by-one libsamplerate output-frame drift). The plan called this out as expected behaviour ("the last sample is held; zero-order; drift smoothed by PDC"); the implementation is a short for-loop padding the buffer tail.

3. **`#include <cmath>`** added to PluginProcessor.cpp for `std::ceil` (used in the core-latency host-sample conversion). Trivial; not worth flagging as a deviation but recorded for completeness.

4. **Anonymous-namespace helpers in SrcChain.cpp** (`centreOfEnergy`, `f32_to_s16`, `s16_to_f32`) instead of class statics. Implementation-detail; consumers don't see them.

## Threat Flags

None. This phase adds no new network endpoints, auth paths, file access patterns, or schema changes. The libsamplerate fetch happens at CMake-configure time inside a sandboxed build directory; the runtime surface is a numeric audio buffer in/out only.

## Known Stubs

None. The class is fully wired; no UI rendering depends on placeholder data; PluginProcessor's plugin branch produces real audio.

## Pre-Existing Warnings (Out of Scope)

Carried forward from Phase 21 / earlier, not caused by this phase, NOT fixed (deferred items per the project's standing "no scope creep" discipline):
- `src/plugin/RegisterPanel.cpp:81,88` - `-Wshadow` on `reg` and `type` inside a for-loop.
- `src/plugin/RegisterPanel.cpp:100` - `-Wfloat-equal` on a slider-value comparison.
- `src/plugin/MorphPanel.cpp:14` - `-Wunused-variable` on the unused `kWaypointNames` array.

These are pre-existing and out of scope for Phase 22. Flag them if a later phase audits the GUI surface.

## Open Follow-ups

- **PLUG-15 UAT pending** (above). After Anthony runs the Ardour null-test, update this SUMMARY in-place with the residual RMS and pass/fail. If FAIL, diagnostic path is in the verification section.
- **pluginval CI matrix expansion to multiple host SRs** - deferred to Phase 25 per `ci_decisions` in PLAN frontmatter. The advisory job at strictness-7 already covers 44.1 kHz; adding 48/96/192 kHz would catch fast-path-only regressions where the SRC-engaged path allocates but the fast-path doesn't.
- **Promote core int16 scratch from stack to HeapBlock** - 32 KiB stack allocation is fine on Linux/macOS/Windows audio threads today but ARCHITECTURE-v1.7.md §4.3 flags this as fragile. Deferred to Phase 23 - likely folded into the BoundaryConverter class.
- **SrcChain unit test** - a standalone impulse round-trip test would catch the same things as the manual Reaper test but in CI. Deferred per `Deferred Ideas` in PLAN.md - it requires standing up new JUCE-test-utility build plumbing.

## Self-Check: PASSED

- `cmake/libsamplerate.cmake` - FOUND
- `src/plugin/SrcChain.h` - FOUND
- `src/plugin/SrcChain.cpp` - FOUND
- `src/plugin/CMakeLists.txt` modified (samplerate link, SrcChain.cpp in target_sources) - FOUND
- `src/plugin/PluginProcessor.h` modified (SrcChain member, hostSampleRate_ cache) - FOUND
- `src/plugin/PluginProcessor.cpp` modified (prepareToPlay, releaseResources, processBlock) - FOUND
- Commit `72660bd` (build/cmake) - FOUND
- Commit `496e7eb` (SrcChain class) - FOUND
- Commit `976553b` (PluginProcessor integration) - FOUND
- All four Linux plugin artifacts present in `build/src/plugin/spu94_plugin_artefacts/` - FOUND

PLUG-15 (manual UAT residual RMS) marked PENDING UAT - this is the only requirement not green-checked at SUMMARY-write time and is called out explicitly above so the orchestrator + user can resolve it after running Ardour.
