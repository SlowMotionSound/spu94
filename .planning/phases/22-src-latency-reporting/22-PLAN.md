---
phase: 22-src-latency-reporting
plan: 01
type: feature
wave: 1
depends_on:
  - 21-build-skeleton-ci-matrix
files_modified:
  - cmake/libsamplerate.cmake                  # new: FetchContent shim, SHA-pinned (parallel pattern with cmake/clap_juce_extensions.cmake)
  - src/plugin/CMakeLists.txt                  # include(libsamplerate.cmake); target_link_libraries(... samplerate)
  - src/plugin/SrcChain.h                      # new: bidirectional SRC sandwich, prepareToPlay-allocated, RT-safe
  - src/plugin/SrcChain.cpp                    # new: libsamplerate src_callback_read driver + fast-path bypass + group-delay measurement
  - src/plugin/PluginProcessor.h               # adds SrcChain member, hostSampleRate cache, latency atomics
  - src/plugin/PluginProcessor.cpp             # prepareToPlay allocates SrcChain; setLatencySamples(measured); processBlock SRC sandwich + ScopedNoDenormals; standalone wavSource path stays gated
  - .github/workflows/plugins.yml              # OPTIONAL: extend the existing pluginval-early-warning job with --strictness-level 7 RT-safety probe (already present at strictness-7; this phase tightens its wording, NOT adds a new gate)

autonomous: true

requirements:
  - PLUG-09   # libsamplerate (BSD-2) integrated
  - PLUG-10   # SRC_SINC_MEDIUM_QUALITY preset
  - PLUG-11   # bidirectional SRC (in + out)
  - PLUG-12   # zero allocation in processBlock; prepareToPlay-allocated state and scratch
  - PLUG-13   # 44.1 kHz host SR fast-path bypass
  - PLUG-14   # setLatencySamples reports measured SRC_in + core + SRC_out group delay
  - PLUG-15   # null-test confirms PDC alignment in Reaper at 48 kHz host SR
  - PLUG-16   # ScopedNoDenormals wraps processBlock

must_haves:
  truths:
    - "Loading the SPU-94 plugin (VST3 or CLAP) into a 48 kHz Linux host (Reaper or Ardour), playing a dry signal through it with full wet/dry mix, and inspecting the output produces reverb audio. The plugin is no longer silent in plugin wrappers."
    - "Loading the same plugin at a 44.1 kHz host SR engages the fast-path bypass: no SRC roundtrip runs, and the reported latency from getLatencySamples() equals exactly the C core's spu94_get_total_latency_samples() value (i.e. 2 × SRC_group_delay terms are zero)."
    - "At host SR 48 kHz, getLatencySamples() returns a single fixed integer that is the sum (SRC_in_group_delay_host_samples + core_latency_host_samples + SRC_out_group_delay_host_samples). The number is the same on every prepareToPlay call with the same SR, and is set BEFORE the first processBlock so PDC graphs see it."
    - "Null test (procedure documented in Task 4): drying the plugin (Dry=1.0, Reverb=0.0, ADPCM=0.0) and summing its output against a polarity-inverted dry copy in Reaper at 48 kHz host SR nulls to below -60 dBFS on broadband content. Drift (visible as a non-flat residual) indicates PDC misalignment and FAILS this phase."
    - "pluginval --strictness-level 7 --validate-in-process on the Linux VST3 reports zero allocations on the audio thread across at least 44.1 / 48 / 96 / 192 kHz host SR runs. (The existing strictness-7 advisory job in plugins.yml exercises 44.1 by default; this phase widens that job's matrix in CI commentary, but Phase 22 does NOT promote it to a merge gate — that is Phase 25.)"
    - "processBlock is wrapped in juce::ScopedNoDenormals at function entry. A reverb-tail CPU spike test (sustained silence after a loud transient at 48 kHz on an Intel CPU) does not exhibit microcode-trap stalls."
    - "The C core (libspu94) and its int16 contract are unchanged. spu94_process is still called with int16 buffers at 44.1 kHz; the float↔int16 boundary remains exactly where it was at end of Phase 21. Phase 23 will slot the float↔int16 converter into the SRC sandwich; Phase 22 leaves a clean integration point and no architectural debt."
    - "Standalone behaviour is byte-identical to end-of-Phase-21: the WavSource gate at the former PluginProcessor.cpp:315-320 site still produces silence when no WAV is loaded; Load → Play → Stop still round-trips a WAV through the engine."
    - "libsamplerate is pulled in via cmake/libsamplerate.cmake using FetchContent pinned to commit SHA 2ccde9568cca73c7b32c97fefca2e418c16ae5e3 (2025-09-07 master, BSD-2-Clause). The shim mirrors the cmake/clap_juce_extensions.cmake pattern so a future swap (e.g. system-package on Linux distros) is a single-file change."

  artifacts:
    - path: "cmake/libsamplerate.cmake"
      provides: "FetchContent_Declare(libsamplerate ...) pinned to a specific upstream SHA + FetchContent_MakeAvailable + a documented swap path for future system-package replacement"
      contains: "FetchContent_Declare"
    - path: "src/plugin/SrcChain.h"
      provides: "Bidirectional SRC wrapper class. Public surface: prepare(double hostSR, int maxHostBlockSize, int numChannels), release(), reset(), processIn(const float* hostInL, const float* hostInR, int hostN, int16_t* coreOutL, int16_t* coreOutR, int& coreNOut), processOut(const int16_t* coreInL, const int16_t* coreInR, int coreN, float* hostOutL, float* hostOutR, int& hostNOut), isFastPath() const, getMeasuredLatencyHostSamples() const."
      contains: "class SrcChain"
    - path: "src/plugin/SrcChain.cpp"
      provides: "libsamplerate driver, scratch buffer allocation in prepare(), fast-path detection, impulse-based group-delay measurement run inside prepare(), zero-allocation streaming process methods."
      contains: "src_callback_new\\|src_callback_read\\|SRC_SINC_MEDIUM_QUALITY"
    - path: "src/plugin/PluginProcessor.h"
      provides: "SrcChain member, double hostSampleRate cache, std::atomic<int> measuredLatencySamples accessor for tests/UI."
      contains: "SrcChain"
    - path: "src/plugin/PluginProcessor.cpp"
      provides: "prepareToPlay calls srcChain.prepare(hostSR, maxBlock, numChans), then setLatencySamples(srcChain.getMeasuredLatencyHostSamples() + spu94_get_total_latency_samples_in_host_samples). processBlock opens with juce::ScopedNoDenormals; plugin path runs host buffer → srcChain.processIn → spu94_process → srcChain.processOut → host buffer; standalone wavSource gate is unchanged."
      contains: "ScopedNoDenormals\\|setLatencySamples\\|srcChain\\.process"

  key_links:
    - from: "src/plugin/CMakeLists.txt"
      to: "cmake/libsamplerate.cmake"
      via: "include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake) followed by target_link_libraries(spu94_plugin PRIVATE samplerate)"
      pattern: "libsamplerate\\.cmake"
    - from: "src/plugin/PluginProcessor.cpp::prepareToPlay"
      to: "src/plugin/SrcChain.cpp::prepare"
      via: "srcChain.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels())"
      pattern: "srcChain\\.prepare"
    - from: "src/plugin/PluginProcessor.cpp::prepareToPlay"
      to: "juce::AudioProcessor::setLatencySamples"
      via: "setLatencySamples(srcChain.getMeasuredLatencyHostSamples() + coreLatencyInHostSamples)"
      pattern: "setLatencySamples\\("
    - from: "src/plugin/PluginProcessor.cpp::processBlock"
      to: "src/plugin/SrcChain.cpp::processIn / processOut"
      via: "srcChain.processIn(...) -> spu94_process(...) -> srcChain.processOut(...) on the plugin branch only; standalone path bypasses srcChain"
      pattern: "srcChain\\.process(In|Out)"
    - from: "src/plugin/PluginProcessor.cpp::processBlock"
      to: "juce::ScopedNoDenormals"
      via: "juce::ScopedNoDenormals noDenormals; at function entry, before any audio work"
      pattern: "ScopedNoDenormals"

success_criteria:
  - "PLUG-09: cmake -S . -B build resolves libsamplerate via FetchContent; build/_deps/libsamplerate-src/ contains the BSD-2-Clause sources at the pinned SHA. No system package required."
  - "PLUG-10: All SrcChain instances are constructed with SRC_SINC_MEDIUM_QUALITY (grep src/plugin/SrcChain.cpp for the literal; exactly one occurrence per direction = exactly two total)."
  - "PLUG-11: Both an input SRC (host SR → 44100) and an output SRC (44100 → host SR) exist; both are exercised in every non-fast-path processBlock call. Verified by an SRC-call counter that the executor reads from a debug build at host SR 48 kHz."
  - "PLUG-12: pluginval --strictness-level 7 --validate-in-process on the Linux VST3 reports zero RT-safety violations at host SR 48 kHz. Manual inspection of SrcChain::processIn and ::processOut confirms no operator new, no malloc, no std::vector mutation, no std::string construction, no juce::String operations, no Logger / DBG calls, no Mutex / CriticalSection acquisition."
  - "PLUG-13: At hostSampleRate == 44100.0 (exact float equality; tolerance ≤1e-9), SrcChain::isFastPath() returns true; processIn copies host floats directly to int16 scratch (clamped via std::clamp, no libsamplerate call); processOut performs the reverse copy. getMeasuredLatencyHostSamples() returns 0 in fast-path mode. Verified by a unit-test-friendly call counter (incremented only when libsamplerate.src_callback_read fires)."
  - "PLUG-14: getLatencySamples() returns a positive integer at non-fast-path host SRs and is the sum of (a) the impulse-measured input-SRC group delay converted to host samples, (b) spu94_get_total_latency_samples(engines[0]) converted from 44100 to host samples, and (c) the impulse-measured output-SRC group delay in host samples. The measurement procedure is documented inline in SrcChain.cpp with the formula and executed inside SrcChain::prepare() — NOT estimated from libsamplerate's documentation."
  - "PLUG-15: Manual UAT in Reaper at host SR 48 kHz: route a noise/sweep test tone to two parallel tracks; on track A insert the SPU-94 plugin with Dry=1.0, Reverb=0.0, ADPCM=0.0 (passthrough-style); polarity-invert track A; sum to a third track. The summed residual measures below -60 dBFS RMS across the test (no audible signal, no pre-echo, no late echo). If Reaper is unavailable on the executor's Linux box, Ardour is acceptable — both honour PDC. The null-test result (dB RMS of the residual) is captured in the phase SUMMARY."
  - "PLUG-16: juce::ScopedNoDenormals noDenormals; is the first statement of SPU94AudioProcessor::processBlock. Verified by grep + manual code read."

src_decisions:
  library: "libsamplerate (Secret Rabbit Code), BSD-2-Clause"
  pinned_sha: "2ccde9568cca73c7b32c97fefca2e418c16ae5e3"
  pinned_sha_resolved: "2026-05-11 via api.github.com/repos/libsndfile/libsamplerate/commits/master"
  pinned_sha_authored: "2025-08-28"
  pinned_sha_resolved_on: "2026-05-11"
  quality_preset: "SRC_SINC_MEDIUM_QUALITY"
  topology: "Bidirectional: one SRC_STATE* for host_SR→44100 input, one SRC_STATE* for 44100→host_SR output. Both per-channel (L+R), so 4 SRC_STATE* total on a stereo plugin."
  fast_path_branch: "Runtime branch inside SrcChain::processIn / processOut, selected by an isFastPath_ bool computed once in prepare(). Rationale: a single code path is simpler than two prepared chains; the branch predicts trivially well and is taken once per block (not per sample). Trade-off acknowledged: a separately-prepared no-op chain would have zero runtime branch, but the per-block cost of one bool test is below noise floor and not worth the extra plumbing."
  scratch_buffer_sizing: "maxHostBlockSize × (44100 / minHostSR) × headroom_factor, where minHostSR = 44100 (worst case is downsampling 192k→44.1k, which produces FEWER core samples than host samples — so the core scratch is sized for the SAME N as the host buffer + safety; the host-side scratch fed into the OUTPUT SRC is the opposite case and is sized for maxHostBlockSize × (maxHostSR / 44100). Concrete planner choice: scratch_core_samples_per_channel = maxHostBlockSize + 32 (safety), scratch_host_samples_per_channel = maxHostBlockSize × ceil(maxSupportedHostSR / 44100) + 32 = maxHostBlockSize × 5 + 32 (covers up to 192 kHz host SR with margin for 384 kHz oversample paths). Headroom factor = 32 sample tail margin per buffer, chosen because libsamplerate's worst-case sample-count drift around the ratio average is bounded by the polyphase filter length (Sinc-Medium ≈ 121 taps), well under 32 even at extreme block sizes."
  fast_path_tolerance: "std::abs(hostSampleRate - 44100.0) < 1.0e-9. Float-equality with epsilon; rationale = some hosts represent 44.1 kHz as 44099.999... due to internal double conversion. 1e-9 is strict enough that 44099.9 still goes through the SRC path (correct: that is NOT 44.1 kHz)."

latency_decisions:
  measurement_method: "Impulse response inside SrcChain::prepare(). For each direction's SRC_STATE*, push (kMeasureLen) samples through src_callback_read where the input is a Kronecker delta at index 0 and zeros elsewhere. Locate the centre-of-energy of the output: latency_samples = sum(i × output[i]^2) / sum(output[i]^2). Sinc filters smear the impulse over ~kSincTaps samples (~121 for Sinc-Medium), so a single argmax peak is unreliable — the energy-weighted centroid is the correct measure. Convert to host samples by multiplying by hostSR / 44100 — the same ratio applies in both upsampling (host > 44100, ratio > 1) and downsampling (host < 44100, ratio < 1) directions; the input SRC outputs at core rate and the output SRC outputs at host rate, but the impulse measurement always lives in the SRC's output buffer, which for the input SRC is at core rate and needs ratio scaling to express in host samples."
  measurement_buffer_length: "kMeasureLen = 4096 samples. Generous — Sinc-Medium one-way impulse smears over <300 samples (~121 taps at the higher rate). 4096 ensures full energy capture even at 192k host SR where the upsampler outputs ~4.4× the input length."
  total_latency_formula: "totalLatencyHostSamples = measuredInputSrcLatencyInHostSamples + (spu94_get_total_latency_samples(engines[0]) × hostSampleRate / 44100.0, rounded up) + measuredOutputSrcLatencyInHostSamples"
  reported_via: "setLatencySamples(totalLatencyHostSamples) from inside prepareToPlay AFTER srcChain.prepare() has run and AFTER engines[0] is constructed. setLatencySamples is called every prepareToPlay (correct per PITFALLS B6: some hosts re-poll on transport start, others ignore mid-stream changes — calling it every time is the only correct policy)."
  fast_path_latency: "Zero. In fast-path mode getMeasuredLatencyHostSamples() returns 0, and totalLatencyHostSamples reduces to just the core latency (which is reported at 44.1 kHz, equal to host samples at host SR 44.1)."

ci_decisions:
  pluginval_strictness_7_probe: "ALREADY PRESENT in plugins.yml as the pluginval-early-warning job (continue-on-error: true, Linux VST3 only). Phase 22 keeps it as-is and confirms RT-safety; it does NOT promote it to a merge-blocking gate (that is Phase 25). No new job is added by this phase."
  ci_rationale: "The early-warning job is already wired and will catch any allocation regression introduced in Tasks 1-4 of this plan. Promoting to a hard gate now would: (a) duplicate work that Phase 25 is scoped for, (b) potentially block merges on transient pluginval flakes before the validator-CI hardening pass, (c) violate the scope-creep redirect in 22-CONTEXT.md. Decision: KEEP advisory, document the strictness-7 expectation in this PLAN's success criteria so executor verifies locally as well."

risks:
  - id: R1
    title: "RT-safety regression in processBlock (PITFALLS B1)"
    impact: "Hard release blocker. Any allocation, lock acquisition, syscall, or logging on the audio thread fails pluginval strictness-7 and creates dropouts in real hosts."
    mitigation: "Tasks 2 and 3 explicitly enumerate the libsamplerate APIs that allocate (src_new, src_delete, src_callback_new) vs the ones that don't (src_callback_read, src_set_ratio, src_reset). prepare() owns all src_callback_new + src_callback_read scratch allocations. processBlock calls ONLY src_callback_read + src_reset (and only inside reset() on host-SR-change, which JUCE serializes against processBlock via prepareToPlay). Task 5 verifies with pluginval strictness-7 locally before the phase closes."
    owned_by: "Task 2 (SrcChain skeleton — RT-safe construction + alloc-in-prepare), Task 3 (processBlock rewrite — verified by pluginval --strictness-level 7 --validate-in-process advisory probe already in plugins.yml)"
  - id: R2
    title: "PDC misalignment from incorrect group-delay numbers (PITFALLS B6)"
    impact: "Plugin loads and produces audio but arrives offset from dry tracks in every multi-track mix. Symptom is ghosted/double transients in null-test."
    mitigation: "Group delay is MEASURED at prepare() via impulse response — never estimated from documentation. The measurement code is inline in SrcChain::prepare() and uses the same SRC_STATE* that subsequently runs in processBlock (so any quality-setting / filter-tap variation is captured). Null-test in Reaper at 48 kHz is the gate (Task 4). Pass criterion: residual below -60 dBFS RMS on broadband content."
    owned_by: "Task 2 (measurement implementation), Task 4 (null-test verification)"
  - id: R3
    title: "Denormal CPU spikes in reverb tails (PITFALLS C3)"
    impact: "Reverb tails generate subnormal floats; on Intel CPUs without DAZ/FTZ flags set, every subnormal arithmetic op traps to microcode and stalls the audio thread. Symptom is intermittent xruns / clicks under apparently idle conditions."
    mitigation: "juce::ScopedNoDenormals noDenormals; is the FIRST statement of processBlock. RAII guarantees DAZ/FTZ stays set for the entire block including all SRC and core work. Non-negotiable per PLUG-16."
    owned_by: "Task 3"
  - id: R4
    title: "Fast-path bypass silently slips (44.1 kHz host runs SRC anyway)"
    impact: "Wastes ~10-20% of host CPU and adds 2× SRC group delay where the latency report would be zero. Sneaky failure mode — output sounds correct, just expensive and time-shifted."
    mitigation: "SrcChain holds an atomic<int> srcCallbacksThisBlock_ counter, incremented only inside the non-fast-path branch. Task 3's verify step at 44.1 kHz reads the counter after a block and asserts == 0. SrcChain::isFastPath() is also exposed as a public const accessor so a future unit test or assertion can check it."
    owned_by: "Task 2 (counter implementation), Task 3 (verify step at 44.1 kHz)"
  - id: R5
    title: "Scratch buffer under-sizing (clipping or silent dropped samples)"
    impact: "If maxHostBlockSize × ratio + libsamplerate's per-block drift exceeds the allocated scratch, samples are dropped or written out of bounds. Out-of-bounds is a heap corruption bug, not just an audio glitch."
    mitigation: "Explicit sizing formula recorded in src_decisions.scratch_buffer_sizing above. Worst-case at maxHostBlockSize=4096 and maxSupportedHostSR=192000: scratch_host = 4096*5+32 = 20512 samples per channel = ~41 KiB per channel = ~160 KiB total (4 buffers L+R × in+out). Allocated as juce::HeapBlock<int16_t> in prepare(); never on the stack. jassert() guards inside processIn/processOut that the computed coreN never exceeds the allocated size."
    owned_by: "Task 2"

tasks:
  - id: 1
    name: "Add libsamplerate to the CMake graph via cmake/libsamplerate.cmake FetchContent shim. Pin to SHA 2ccde9568cca73c7b32c97fefca2e418c16ae5e3 (BSD-2-Clause, 2025-09-07 master). Link it into spu94_plugin. Verify all 11 user-facing binaries from Phase 21 still build."
  - id: 2
    name: "Create SrcChain.{h,cpp}: bidirectional SRC class with prepare()-time allocation of SRC_STATE handles, per-channel scratch buffers (juce::HeapBlock<int16_t>), the fast-path bool, the srcCallbacksThisBlock counter, the impulse-based group-delay measurement, and zero-allocation processIn/processOut. Build green; PluginProcessor does NOT yet use the new class (next task)."
  - id: 3
    name: "Rewrite SPU94AudioProcessor::processBlock and ::prepareToPlay to use SrcChain. processBlock opens with juce::ScopedNoDenormals. The plugin branch (wrapperType != Standalone) runs the SRC sandwich on host-provided buffers, replacing the old early-return-on-silence at the WavSource gate (PluginProcessor.cpp:315-320). Standalone branch keeps the existing WavSource gate verbatim for v1.6 back-compat. prepareToPlay calls srcChain.prepare(...) then setLatencySamples(srcChain.getMeasuredLatencyHostSamples() + core_latency_in_host_samples)."
  - id: 4
    name: "Document and execute the Reaper-on-Linux null-test at 48 kHz host SR per the procedure in this plan's Verification section. Capture the residual RMS in the phase SUMMARY. If Reaper is not on the executor's Linux box, use Ardour. If the residual exceeds -60 dBFS, FAIL the phase, diagnose (almost certainly latency-number drift), iterate. This is a manual UAT step — NOT added to CI."
---

# Phase 22 Plan 01: Sample-Rate Conversion & Latency Reporting

Wrap a bidirectional libsamplerate SRC chain around the existing spu94_process call so plugin formats running at any host sample rate (48 / 88.2 / 96 / 176.4 / 192 kHz) feed the C core at its fixed 44.1 kHz internal rate, and so the host's plugin-delay-compensation (PDC) graph receives an accurate `setLatencySamples()` number derived from a measured impulse response. Wrap `processBlock` in `juce::ScopedNoDenormals` to prevent denormal-flush CPU spikes in reverb tails. Add a fast-path bypass when the host already runs at 44.1 kHz exactly.

This phase is the v1.7 engineering risk hotspot. Three of the four blocking pitfalls in `PITFALLS-v1.7.md` (B1 RT-safety regression, B6 PDC misalignment, C3 denormals) land here. The plan treats them as first-class risks (see the `risks:` block in frontmatter) and binds each to a specific task and verification step. The C core itself is untouched — Phase 22 is wrapper-only work.

The float↔int16 boundary stays at its end-of-Phase-21 location: int16 still goes into and out of `spu94_process`. SRC operates on int16 buffers on the core side and on float buffers on the host side. Phase 23 will insert the explicit float↔int16 converter inside the sandwich (between host buffer and SRC input on the way in, and between SRC output and host buffer on the way out — preserving the same overall topology). Phase 22 is engineered to make that drop-in painless.

## Rationale

Per `ARCHITECTURE-v1.7.md` §2, the SRC engineering decision dominates this phase. The library choice was made at milestone level: libsamplerate at `SRC_SINC_MEDIUM_QUALITY`, BSD-2-Clause, integrated via FetchContent. That decision is reproduced verbatim in `22-CONTEXT.md` and is non-negotiable here — this PLAN treats it as input.

What this PLAN decides:
- Concrete pinned libsamplerate SHA (`2ccde9568cca73c7b32c97fefca2e418c16ae5e3` from upstream master at 2025-09-07).
- Scratch buffer sizing formula (host-side: `maxBlock × 5 + 32`; core-side: `maxBlock + 32`).
- Fast-path branch structure (runtime branch inside `SrcChain::processIn/processOut`, not a separately-prepared no-op chain — simpler and the per-block branch cost is below noise floor).
- Group-delay measurement method (impulse response, centre-of-energy formula, `kMeasureLen = 4096`).
- Float-equality tolerance for 44.1 kHz detection (1e-9, strict).
- The pluginval `--strictness-level 7` probe is NOT promoted to a merge gate in this phase — it stays as the advisory `pluginval-early-warning` job that Phase 21 already wired. Phase 25 is the merge-gate phase.

Per the architecture map, the SRC sandwich lives entirely in the JUCE wrapper. The C core and its `int16 @ 44.1 kHz` contract are frozen — `spu94_process` is still called with exactly the buffers it expects today.

## Design

### Topology

```
  host audio (float, host_SR)
        │
        ▼ srcChain.processIn(...)
  ┌─────────────────────────────────────────────────────────────┐
  │  if isFastPath_:                                            │
  │      host floats → clamp+truncate to int16 → core scratch   │
  │  else:                                                      │
  │      host floats → src_callback_read (host_SR→44100) →      │
  │      int16 core scratch                                     │
  └─────────────────────────────────────────────────────────────┘
        │
        ▼  int16 @ 44100 Hz
   spu94_process(engines[0], L_in, R_in, L_out, R_out, coreN)
        │
        ▼  int16 @ 44100 Hz
  ┌─────────────────────────────────────────────────────────────┐
  │  if isFastPath_:                                            │
  │      int16 core scratch → /32768.0f → host floats           │
  │  else:                                                      │
  │      int16 core scratch → src_callback_read (44100→host_SR) │
  │      → host floats                                          │
  └─────────────────────────────────────────────────────────────┘
        │
        ▼ srcChain.processOut(...)
  host audio (float, host_SR)
```

Note that the float↔int16 transitions in the boxes above are still *inside* SrcChain in Phase 22 — they are the existing `× 32768.0f / clamp` and `/ 32768.0f` operations that the standalone WAV path already used. Phase 23 will move those into a dedicated `BoundaryConverter` and the sandwich becomes:
```
host float → BoundaryConverter → SRC_in (now int16-in, int16-out) → core → SRC_out → BoundaryConverter → host float
```
…but Phase 22 keeps things simple: SRC_in's `process()` consumes float from the host buffer directly and emits int16 to the core scratch, using libsamplerate's `src_float_to_short_array` (or an inline clamp+cast) at the boundary. This is sound because libsamplerate's `src_callback_read` API works with `float*` buffers; the `int16` conversion is wrapper-side. Phase 23 just refactors where the conversion lives.

### Files

```
src/plugin/SrcChain.h            (new, ~80 LOC)
src/plugin/SrcChain.cpp          (new, ~250 LOC)
cmake/libsamplerate.cmake        (new, ~20 LOC; parallel pattern with clap_juce_extensions.cmake)
src/plugin/CMakeLists.txt        (modified: include + link)
src/plugin/PluginProcessor.h     (modified: SrcChain member, hostSampleRate cache)
src/plugin/PluginProcessor.cpp   (modified: prepareToPlay, processBlock)
```

No test files are added in Phase 22 — verification is via pluginval strictness-7 (already in CI as advisory), the manual Reaper null-test (Task 4), and an executor-side debug build that reads `SrcChain::srcCallbacksThisBlock` to verify fast-path engagement. A formal SrcChain unit test could be added but introduces new test-infrastructure for a wrapper-only class; deferred (see Deferred Ideas).

### SrcChain class shape

```cpp
// src/plugin/SrcChain.h
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

struct SRC_STATE_tag;
typedef struct SRC_STATE_tag SRC_STATE;   // forward-decl, defined in <samplerate.h>

class SrcChain
{
public:
    SrcChain() = default;
    ~SrcChain();

    // Called from prepareToPlay. Allocates SRC_STATE handles, allocates scratch
    // buffers from maxHostBlockSize, runs the impulse-based group-delay
    // measurement. NOT RT-safe (allocates). Idempotent: a second call with the
    // same args is a no-op; with different args, releases and re-allocates.
    void prepare(double hostSampleRate, int maxHostBlockSize, int numChannels);

    // Called from releaseResources. Frees SRC_STATE handles and scratch buffers.
    void release();

    // Called when the host requests a soft reset (between transport stops, etc).
    // Calls src_reset on each SRC_STATE; does NOT reallocate.
    void reset();

    // RT-safe. Consumes hostN host-rate float samples per channel, produces
    // (writes) coreNOut core-rate int16 samples per channel into the supplied
    // out buffers. In fast-path mode hostN == coreNOut and no SRC runs.
    void processIn(const float* const* hostIn, int hostN,
                   int16_t* coreOutL, int16_t* coreOutR, int& coreNOut);

    // RT-safe. Consumes coreN core-rate int16 samples per channel from the
    // supplied in buffers, produces hostNOut host-rate float samples per
    // channel written into hostOut. In fast-path mode coreN == hostNOut.
    void processOut(const int16_t* coreInL, const int16_t* coreInR, int coreN,
                    float* const* hostOut, int& hostNOut);

    bool isFastPath() const noexcept { return isFastPath_; }
    int  getMeasuredLatencyHostSamples() const noexcept { return measuredLatencyHostSamples_; }
    int  getSrcCallbacksThisBlock() const noexcept { return srcCallbacksThisBlock_.load(std::memory_order_relaxed); }
    void resetSrcCallbacksCounter() noexcept { srcCallbacksThisBlock_.store(0, std::memory_order_relaxed); }

private:
    SRC_STATE* srcIn_[2]   = { nullptr, nullptr };  // host_SR → 44100, per channel
    SRC_STATE* srcOut_[2]  = { nullptr, nullptr };  // 44100   → host_SR, per channel

    // Scratch buffers allocated in prepare(), freed in release().
    juce::HeapBlock<float>   scratchInFloat_[2];     // host-rate float, sized for maxHostBlockSize
    juce::HeapBlock<float>   scratchCoreFloatIn_[2]; // core-rate float, sized for coreScratchN
    juce::HeapBlock<float>   scratchCoreFloatOut_[2];// core-rate float, sized for coreScratchN
    juce::HeapBlock<float>   scratchOutFloat_[2];    // host-rate float, sized for hostScratchN

    double hostSampleRate_   = 0.0;
    int    maxHostBlockSize_ = 0;
    int    numChannels_      = 0;
    int    coreScratchN_     = 0;   // = maxHostBlockSize + 32
    int    hostScratchN_     = 0;   // = maxHostBlockSize × 5 + 32  (covers up to 192 kHz)
    bool   isFastPath_       = false;
    int    measuredLatencyHostSamples_ = 0;

    std::atomic<int> srcCallbacksThisBlock_ { 0 };

    int measureGroupDelayInHostSamples();   // called from prepare()
};
```

### prepareToPlay integration

```cpp
void SPU94AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // ... existing engine tear-down + spu94_init + preset load + mixer defaults
    //     (unchanged from end of Phase 21)

    hostSampleRate_ = sampleRate;
    constexpr int kMaxBlock = 4096;
    const int maxBlock = juce::jmin(samplesPerBlock, kMaxBlock);

    srcChain.prepare(sampleRate, maxBlock, /*numChannels=*/2);

    // Core latency in host samples. Use ceil to over-report by ≤1 sample
    // rather than under-report (PDC under-report drifts wet AHEAD of dry).
    const auto coreLatency44k = spu94_get_total_latency_samples(engines[0]);
    const int  coreLatencyHostSamples =
        static_cast<int>(std::ceil(static_cast<double>(coreLatency44k)
                                   * (sampleRate / 44100.0)));

    setLatencySamples(srcChain.getMeasuredLatencyHostSamples() + coreLatencyHostSamples);
}
```

`releaseResources` calls `srcChain.release()` after the existing engine teardown. No other changes.

### processBlock integration

The split between standalone and plugin paths is the load-bearing change at this site. The existing `wavSource.loaded || wavSource.playing` gate at PluginProcessor.cpp:315-320 stays for the standalone path; the plugin path (every other wrapperType) runs the SRC sandwich on the host buffer.

```cpp
void SPU94AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;       // PLUG-16, R3
    srcChain.resetSrcCallbacksCounter();       // R4 — for fast-path verification

    // 1-5: existing unconditional state management (WAV swap, preset queue,
    //      file-preset load, morph re-apply, shadow sync) — UNCHANGED.

    if (engines[0] == nullptr || engines[1] == nullptr) { buffer.clear(); return; }

    const int n = buffer.getNumSamples();
    constexpr int kMaxBlock = 4096;
    jassert(n <= kMaxBlock);
    if (n > kMaxBlock) { buffer.clear(); return; }

    const bool isStandalone = (wrapperType == wrapperType_Standalone);

    if (isStandalone)
    {
        // === STANDALONE PATH (v1.6 back-compat, unchanged) ======================
        if (!wavSource.loaded.load(std::memory_order_acquire) ||
            !wavSource.playing.load(std::memory_order_relaxed))
        {
            buffer.clear();
            return;
        }
        // ... existing WavSource → int16 → spu94_process → float-output path
        //     (PluginProcessor.cpp:322-381, byte-identical to end of Phase 21)
    }
    else
    {
        // === PLUGIN PATH (NEW: host buffer through SRC sandwich) ================
        // SRC scratch lives inside SrcChain; the only audio-thread allocations
        // are the int16 core scratches which we still place on the stack for now
        // (Phase 23 will move them into SrcChain as well or into BoundaryConverter).
        int16_t coreInL[kMaxBlock], coreInR[kMaxBlock];
        int16_t coreOutL[kMaxBlock], coreOutR[kMaxBlock];

        const float* hostIn[2] = {
            buffer.getReadPointer(0),
            buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0)
        };
        int coreN = 0;
        srcChain.processIn(hostIn, n, coreInL, coreInR, coreN);

        spu94_process(engines[0], coreInL, coreInR, coreOutL, coreOutR,
                      static_cast<uint32_t>(coreN));

        float* hostOut[2] = {
            buffer.getWritePointer(0),
            buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr
        };
        int hostNOut = 0;
        srcChain.processOut(coreOutL, coreOutR, coreN, hostOut, hostNOut);
        // hostNOut should equal n within ±1 sample (libsamplerate drift). If
        // off-by-one over-production, the last sample is dropped (slight drift
        // smoothed by PDC); under-production, last sample is held (zero-order).
        // This drift is invisible after the host's PDC pulls the latency back.

        // Apply the existing side-channel limiter in float space — unchanged
        // from PluginProcessor.cpp:357-377, just on host-rate float buffers
        // instead of int16-converted-to-float buffers. Constants unchanged
        // (kSideKnee = 0.125, kSideCeiling = 0.06).
    }
}
```

The two scratch int16 arrays `coreInL/coreInR/coreOutL/coreOutR` remain stack-allocated at `kMaxBlock = 4096` int16s each (32 KiB total). This mirrors the existing standalone path. Phase 23 will likely promote these into `juce::HeapBlock` members or move them inside SrcChain — but the stack-allocated form is RT-safe today and matches the rest of the codebase's idiom.

### Fast-path counter for R4 verification

`SrcChain::srcCallbacksThisBlock_` is an `std::atomic<int>` incremented every time `src_callback_read` runs (i.e. in the non-fast-path branch only). The executor's verify step for Task 3:
1. Build a Debug variant.
2. Load the VST3 in a 44.1 kHz Linux Reaper session.
3. Play 1 second of audio.
4. Read `srcChain.getSrcCallbacksThisBlock()` immediately after a block (via a temporary debug printout, removed before commit).
5. Assert it returns 0.

Repeat at 48 kHz host SR; assert it returns a positive number (one or two callbacks per block depending on libsamplerate's internal pull pattern).

### libsamplerate quality preset

Both `srcIn_[ch]` and `srcOut_[ch]` are constructed via `src_callback_new(callback, SRC_SINC_MEDIUM_QUALITY, 1, &error, &userdata)`. One channel per SRC_STATE — libsamplerate's "channels" parameter interleaves; for low cache pressure and simple bookkeeping we run two mono SRC_STATEs per direction instead. This is the configuration `juce_libsamplerate` and most JUCE plugins use; SRC_STATE allocation cost is paid once in `prepare()` and is irrelevant on the audio thread.

## Test Coverage

| Test | What | Where |
|------|------|-------|
| All-OS build green | All 11 user-facing binaries from Phase 21 still build after libsamplerate is linked in | Task 1 (local Linux) + plugins.yml matrix on PR push |
| RT-safety probe | `pluginval --strictness-level 7 --validate-in-process` on Linux VST3 at 48 kHz host SR reports zero audio-thread allocations | Executor-local before Task 5 close-out; plugins.yml `pluginval-early-warning` advisory job catches regressions |
| Plugin-path audio | Loading the VST3 / CLAP in Reaper or Ardour at 48 kHz with full wet mix produces audible reverb. Plugin formats are no longer silent. | Task 3 manual verify |
| Fast-path bypass | At 44.1 kHz host SR, `srcChain.getSrcCallbacksThisBlock()` returns 0 after a block; at 48 kHz it returns > 0 | Task 3 manual verify (debug build) |
| Latency report | `getLatencySamples()` polled by Reaper matches the computed `measured_in + core + measured_out` formula at 48 kHz; equals just `core_latency_in_host_samples` at 44.1 kHz | Task 3 manual verify (Reaper plugin properties pane) |
| PDC null-test | Reaper at 48 kHz host SR: dry-only plugin against polarity-inverted dry copy sums to below -60 dBFS RMS | Task 4 manual UAT; recorded in SUMMARY |
| ScopedNoDenormals | Grep `src/plugin/PluginProcessor.cpp` confirms `juce::ScopedNoDenormals noDenormals;` is the first statement of processBlock | Task 3 verify + code review |
| Standalone back-compat | Standalone Load → Play → Stop round-trip still works; no audible change to v1.6 standalone behaviour | Task 3 manual verify |

## Task Detail

### Task 1: libsamplerate FetchContent shim

**Files:**
- `cmake/libsamplerate.cmake` (new)
- `src/plugin/CMakeLists.txt` (modified: `include(...)` + `target_link_libraries(... PRIVATE samplerate)`)

**Action:**
1. Create `cmake/libsamplerate.cmake` modelled on `cmake/clap_juce_extensions.cmake`:
   ```cmake
   # cmake/libsamplerate.cmake
   # Phase 22 / Task 1: FetchContent shim for libsndfile/libsamplerate.
   #
   # libsamplerate is the BSD-2-Clause SRC library used for v1.7's
   # bidirectional host_SR <-> 44100 conversion at the JUCE wrapper boundary.
   # Quality preset SRC_SINC_MEDIUM_QUALITY is selected at SRC_STATE construction
   # time inside src/plugin/SrcChain.cpp (NOT here).
   #
   # Pinned by SHA per project FetchContent discipline (matches
   # cmake/clap_juce_extensions.cmake and root CMakeLists.txt's JUCE pin).
   # SHA resolved 2026-05-11 via:
   #   git ls-remote https://github.com/libsndfile/libsamplerate.git HEAD
   #
   # Future swap path: if a future Linux runner ships libsamplerate via apt and
   # we want to prefer system packages, replace this FetchContent block with
   #   find_package(SampleRate REQUIRED)
   # and update src/plugin/CMakeLists.txt's link line to use SampleRate::samplerate.

   include(FetchContent)

   # Force libsamplerate's own build to skip tools/tests/examples so the
   # fetch is fast and produces only the libsamplerate static target.
   set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
   set(LIBSAMPLERATE_INSTALL  OFF CACHE BOOL "" FORCE)
   set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)

   FetchContent_Declare(
       libsamplerate
       GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
       GIT_TAG        2ccde9568cca73c7b32c97fefca2e418c16ae5e3
       GIT_SHALLOW    TRUE
       GIT_PROGRESS   TRUE
   )
   FetchContent_MakeAvailable(libsamplerate)
   ```
2. In `src/plugin/CMakeLists.txt`, add immediately after the existing `include(${CMAKE_SOURCE_DIR}/cmake/clap_juce_extensions.cmake)` block:
   ```cmake
   include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)
   ```
   Then add `samplerate` to the `PRIVATE` link list of `spu94_plugin`. The target name produced by libsamplerate's own CMake is `samplerate`; verify by inspecting `build/_deps/libsamplerate-build/` after configure.
3. Do NOT include `<samplerate.h>` from any source file in this commit — only the link is wired up. The header is consumed in Task 2.

**Verify (local Linux):**
- `cmake -S . -B build -G Ninja` succeeds; `ls build/_deps/libsamplerate-src/` shows the libsamplerate source tree at the pinned SHA (check `build/_deps/libsamplerate-src/COPYING.md` confirms BSD-2-Clause).
- `cmake --build build` builds all four Linux formats green (VST3, LV2, CLAP, Standalone). The standalone binary still launches and Load → Play → Stop round-trips a WAV identically to end-of-Phase-21.
- On a Phase-21-equivalent system, no behavioural change is observable — this commit only adds a library link; no source uses libsamplerate yet.

**Done:** libsamplerate is in the build graph at SHA `2ccde9568cca73c7b32c97fefca2e418c16ae5e3`, BSD-2-Clause, integrated through `cmake/libsamplerate.cmake` matching the project's parallel-shim pattern. All 11 binaries continue to build green. The pluginval `pluginval-early-warning` advisory job in `plugins.yml` still passes (no source change means no RT-safety regression possible at this commit).

**Commit:** `build(v1.7): FetchContent libsamplerate via cmake/libsamplerate.cmake (PLUG-09)`

---

### Task 2: SrcChain skeleton

**Files:**
- `src/plugin/SrcChain.h` (new)
- `src/plugin/SrcChain.cpp` (new)
- `src/plugin/CMakeLists.txt` (modified: add the two new source files to `target_sources`)

**Action:**
1. Create `src/plugin/SrcChain.h` per the class shape in the Design section above. Forward-declare `SRC_STATE` and pull `<samplerate.h>` only in the `.cpp` so that the header doesn't drag libsamplerate symbols into every other plugin source.
2. Create `src/plugin/SrcChain.cpp`:
   - `prepare(hostSR, maxHostBlock, numChans)`:
     - If already prepared with identical args, return.
     - If prepared with different args, `release()` first.
     - Cache `hostSampleRate_ = hostSR`, `maxHostBlockSize_ = maxHostBlock`, `numChannels_ = numChans`.
     - Determine `isFastPath_ = std::abs(hostSR - 44100.0) < 1.0e-9`.
     - Compute `coreScratchN_ = maxHostBlock + 32` and `hostScratchN_ = maxHostBlock * 5 + 32` (per the sizing formula in `src_decisions`).
     - Allocate `scratchInFloat_[ch]`, `scratchCoreFloatIn_[ch]`, `scratchCoreFloatOut_[ch]`, `scratchOutFloat_[ch]` (per channel) via `juce::HeapBlock::allocate(size, true)`.
     - If NOT fast-path: allocate `srcIn_[ch] = src_callback_new(input_pull_callback, SRC_SINC_MEDIUM_QUALITY, 1, &error, &userdata_in_[ch])` and `srcOut_[ch] = src_callback_new(output_pull_callback, SRC_SINC_MEDIUM_QUALITY, 1, &error, &userdata_out_[ch])`. Both directions allocate even if measuredLatency is later computed — measurement runs through these same handles.
     - Run `measuredLatencyHostSamples_ = measureGroupDelayInHostSamples()` (only in non-fast-path mode; fast-path keeps it at 0).
     - After measurement, call `src_reset` on each SRC_STATE to clear the impulse-response transient state so the first real `processIn` call starts from a clean filter state. (R2 mitigation: measurement and runtime use the SAME state, so any setting variation is captured.)
   - `release()`: `src_delete` each SRC_STATE; `free()` HeapBlocks via destructor or `.free()`; zero the pointers.
   - `reset()`: call `src_reset` on each non-null SRC_STATE. Idempotent. Safe to call from `prepareToPlay` (not from `processBlock`).
   - `processIn(hostIn, hostN, coreOutL, coreOutR, coreNOut)`:
     - If `isFastPath_`: for each sample i in [0..hostN), `coreOut{L,R}[i] = static_cast<int16_t>(std::clamp(hostIn[ch][i] * 32768.0f, -32768.0f, 32767.0f))`. Set `coreNOut = hostN`. Return.
     - Else: store the host pointer + remaining-count into `userdata_in_[ch]` so the libsamplerate pull-callback reads from it. Call `src_callback_read(srcIn_[ch], 44100.0 / hostSampleRate_, coreScratchN_, scratchCoreFloatIn_[ch])` for each channel. Convert the float core scratch to int16 with clamp+truncate into `coreOut{L,R}`. Increment `srcCallbacksThisBlock_`. Set `coreNOut` to the produced sample count (returned by `src_callback_read`; clamped to `coreScratchN_`).
     - `jassert(coreNOut <= coreScratchN_)` — R5 mitigation.
   - `processOut(coreInL, coreInR, coreN, hostOut, hostNOut)`:
     - If `isFastPath_`: for each sample i, `hostOut[ch][i] = static_cast<float>(coreIn{L,R}[i]) / 32768.0f`. Set `hostNOut = coreN`. Return.
     - Else: convert `coreIn{L,R}` to float into `scratchCoreFloatOut_[ch]`. Set up `userdata_out_[ch]` to point at that float scratch. Call `src_callback_read(srcOut_[ch], hostSampleRate_ / 44100.0, maxHostBlockSize_, scratchOutFloat_[ch])`. Copy the produced floats into `hostOut[ch][]`. Increment `srcCallbacksThisBlock_`. Set `hostNOut` to the produced count.
   - `measureGroupDelayInHostSamples()`:
     - Allocate (on the heap, this runs inside `prepare()` only) a `kMeasureLen = 4096` float buffer, zero it, set index 0 to 1.0.
     - For srcIn_[0] only (the channels are symmetric — Sinc-Medium tap layout doesn't depend on channel), run `src_callback_read` repeatedly until 4096 output samples accumulate.
     - Locate the centre-of-energy `t_in = sum(i * out[i]^2) / sum(out[i]^2)`. Convert to host samples: `t_in_host_samples = t_in * (hostSampleRate_ / 44100.0)` (because the input SRC outputs at core rate; the host-side delay corresponds to where the original sample sat at host SR).
     - Repeat for srcOut_[0]. Output is at host rate already so `t_out_host_samples = t_out`.
     - Return `static_cast<int>(std::ceil(t_in_host_samples + t_out_host_samples))`.
     - Then `src_reset` both srcIn_[0] AND srcIn_[1] AND srcOut_[0] AND srcOut_[1] to clear the impulse transient (channels [1] never saw the impulse but `src_reset` is cheap and ensures uniform state).
3. In `src/plugin/CMakeLists.txt`, add `SrcChain.cpp` to the `target_sources(spu94_plugin PRIVATE ...)` list.
4. Do NOT yet wire SrcChain into PluginProcessor — that is Task 3. Build green at this commit; the new class is dead code.

**Verify (local Linux):**
- `cmake --build build` succeeds on all four Linux formats. No warnings from `-Wall -Wextra -Wpedantic` (project default per `cmake/spu94_warnings.cmake`).
- Read SrcChain.cpp top to bottom: confirm `processIn` and `processOut` contain NO `operator new`, no `malloc`, no `std::vector::push_back/resize/reserve`, no `std::string`, no `juce::String`, no `Logger`/`DBG`, no `Mutex`/`CriticalSection`/`SpinLock`. The only audio-thread calls are `src_callback_read`, `std::clamp`, `static_cast`, simple loop arithmetic, and atomic ops.
- The standalone binary still launches and round-trips a WAV — SrcChain is not yet wired in, so this proves the new compilation unit didn't break the link.

**Done:** SrcChain compiles into all four Linux plugin formats (and equivalently into macOS/Windows formats — verified in CI on PR push). Class is RT-safe by construction. No production code uses it yet.

**Commit:** `feat(v1.7): add SrcChain bidirectional SRC wrapper (PLUG-10, PLUG-11, PLUG-12, PLUG-13)`

---

### Task 3: Wire SrcChain into PluginProcessor; SRC sandwich; ScopedNoDenormals; setLatencySamples

**Files:**
- `src/plugin/PluginProcessor.h` (modified: add `SrcChain srcChain_;` member, `double hostSampleRate_` cache)
- `src/plugin/PluginProcessor.cpp` (modified: `prepareToPlay`, `releaseResources`, `processBlock`)

**Action:**
1. In `PluginProcessor.h`, add `#include "SrcChain.h"`, then add `SrcChain srcChain_;` and `double hostSampleRate_ { 44100.0 };` as private members alongside the engines.
2. In `PluginProcessor.cpp::prepareToPlay`:
   - After the existing engine teardown + `spu94_init` + preset load + mixer-default block (PluginProcessor.cpp:48-92 today), add:
     ```cpp
     hostSampleRate_ = sampleRate;
     constexpr int kMaxBlock = 4096;
     const int maxBlock = juce::jmin(samplesPerBlock, kMaxBlock);
     srcChain_.prepare(sampleRate, maxBlock, /*numChannels=*/2);

     const auto coreLatency44k = spu94_get_total_latency_samples(engines[0]);
     const int  coreLatencyHostSamples = static_cast<int>(
         std::ceil(static_cast<double>(coreLatency44k) * (sampleRate / 44100.0)));
     setLatencySamples(srcChain_.getMeasuredLatencyHostSamples() + coreLatencyHostSamples);
     ```
   - The `setLatencySamples` call runs every `prepareToPlay`. Some hosts re-poll on transport start/stop; others ignore mid-stream changes. Per PITFALLS B6, calling it every time is the only correct policy.
3. In `PluginProcessor.cpp::releaseResources`:
   - After the existing engine teardown loop, add `srcChain_.release();`.
4. In `PluginProcessor.cpp::processBlock`:
   - Add `juce::ScopedNoDenormals noDenormals;` as the first statement (BEFORE any audio work, BEFORE the new-WAV-swap block at the current top of the function). This guarantees DAZ/FTZ stay set for the entire block including all SRC and core work. **PLUG-16 — non-negotiable.**
   - Add `srcChain_.resetSrcCallbacksCounter();` immediately after the ScopedNoDenormals line so R4 verification reads a clean counter for this block.
   - Leave the unconditional state-management block (lines ~109-308 today: new-WAV swap, preset queue drain, file-preset load, morph re-apply, shadow sync) UNCHANGED.
   - Replace the existing `if (!wavSource.loaded || !wavSource.playing) { buffer.clear(); return; }` gate at lines 315-320 plus the int16-WAV-source-fed `spu94_process` call at lines 322-381 with the standalone-vs-plugin branch shown in the Design section above.
   - In the standalone branch, keep the existing code byte-identical: WavSource gate, int16 WAV-source read into `tmpL_in/tmpR_in`, `spu94_process(engines[0], tmpL_in, tmpR_in, tmpL_out, tmpR_out, n)`, side-channel limiter on the int16-to-float output. **Standalone behaviour must be identical to end-of-Phase-21.**
   - In the plugin branch, run `srcChain_.processIn` → `spu94_process` → `srcChain_.processOut`, then apply the side-channel limiter on the host-rate float output buffer directly (the constants `kSideKnee = 0.125`, `kSideCeiling = 0.06` are unchanged). The side-channel limiter operates on the float buffer because the SRC output is already float at host rate — there is no int16 step on the output side in the plugin path other than what happens inside SrcChain.
5. Do NOT modify the existing WAV-source struct, double-buffered pendingSlots, `loadWavFile/startPlayback/stopPlayback` methods, or any of the morph/preset machinery. Those are orthogonal to Phase 22.
6. Do NOT add `isBusesLayoutSupported` — that is Phase 25 (scope creep). The plugin still uses JUCE's default `BusesProperties().withInput(stereo).withOutput(stereo)` from Phase 21. Assume stereo-in/stereo-out at the plugin entry; mono support is Phase 25's problem.

**Verify (local Linux):**
- Build all four Linux formats. Build green. No new warnings.
- Phase 21's `plugins.yml` PR build is green: all 11 binaries continue to build across the matrix, and the `pluginval-early-warning` advisory job (strictness-7) passes on the Linux VST3.
- **Manual smoke at 48 kHz host SR (Reaper or Ardour):**
  1. Open Reaper, set project SR to 48000 Hz.
  2. Insert the SPU-94 VST3 on a stereo track.
  3. Add a noise generator on the same track before SPU-94.
  4. Hit play. Confirm audible reverb (wet mix is full per the engine's default Hall preset, so this should be obvious).
  5. Open Reaper's plugin properties pane for SPU-94; note the reported latency in samples. Compare to the formula: `measured_in + ceil(spu94_get_total_latency_samples × 48000 / 44100) + measured_out`. Numbers should match within 1 sample.
- **Manual smoke at 44.1 kHz host SR (Reaper):**
  1. Project SR to 44100 Hz.
  2. Repeat: insert plugin, play noise through it, audible reverb.
  3. Plugin properties pane: latency should now equal exactly `spu94_get_total_latency_samples(engines[0])` (no SRC term, fast-path engaged).
  4. **Debug build only:** temporarily add a `juce::Logger::writeToLog(juce::String(srcChain_.getSrcCallbacksThisBlock()))` AFTER the `processBlock` body (NOT inside it — Logger is not RT-safe) on a once-per-second timer in the editor; verify it reads 0 at 44.1 kHz and > 0 at 48 kHz. Remove the debug log before commit.
- **Manual smoke at 96 kHz host SR (Reaper):** confirm audio plays; latency report uses a different (larger) SRC term but reverb is audible and clean.
- **Standalone back-compat:** run the standalone binary. Load WAV → Play → Stop round-trips with no audible difference from v1.6. The standalone never touches `srcChain_` because the `wrapperType_Standalone` branch bypasses it entirely; `srcChain_.prepare(...)` still runs and the measurement is computed (it's a small one-time cost that's invisible to the user), but no SRC processing happens in the audio loop.
- **pluginval strictness-7 local run:**
  ```
  pluginval --validate-in-process --strictness-level 7 \
            build/src/plugin/SPU-94_artefacts/Release/VST3/SPU-94.vst3
  ```
  Must exit 0 with zero RT-safety violations. If this fails, FIX before commit — do NOT push a known-RT-unsafe build even with the CI gate at advisory.

**Done:** Plugin formats produce audio at host SRs 44.1 / 48 / 96 kHz. setLatencySamples reports the measured group delay. ScopedNoDenormals is the first statement of processBlock. Standalone is byte-identical to v1.6. pluginval strictness-7 passes locally.

**Commit:** `feat(v1.7): SRC sandwich + setLatencySamples + ScopedNoDenormals in processBlock (PLUG-11..16)`

---

### Task 4: Reaper PDC null-test at 48 kHz host SR

**Files:** none modified (this is a manual UAT step; the result is captured in the phase SUMMARY).

**Action:**
1. Confirm Reaper is available on the executor's Linux host (`which reaper` or `flatpak info com.cockos.Reaper`). If not, use Ardour 8+ instead — both honour PDC accurately. Reaper is preferred because its PDC plumbing is the most extensively documented and its null-test result tooling (the "Track Delay" + "Bypass" controls) is the simplest.
2. Set Reaper's project sample rate to 48000 Hz (Project Settings → Audio → Sample Rate).
3. Build a null-test project:
   - **Track 1 (Dry):** insert a noise generator (Reaper's built-in `ReaSynth` set to white noise, or a pink-noise WAV imported as an item). Set track output to master.
   - **Track 2 (Plugin):** route the same noise source to this track. Insert SPU-94 VST3. Open the plugin GUI and set:
     - Dry Level: 1.0 (full)
     - Reverb Level: 0.0 (silent)
     - ADPCM Level: 0.0 (silent)
     - This makes the plugin a pure passthrough — output equals input, modulo the SRC roundtrip's tiny (~0.01-0.1%) coloration and its full reported latency.
   - **Track 3 (Sum):** receive from Track 1 (no invert) and from Track 2 (POLARITY INVERTED). Output to master. Mute the master output of tracks 1 and 2 so only the sum is audible.
4. Hit play. The summed audio is the residual of (dry − plugin_passthrough). If PDC is correctly reported, the residual is silence ± SRC coloration noise floor.
5. Measure the residual:
   - Insert Reaper's built-in JS spectrum/RMS meter on track 3, or `ReaJS` "LOSER's RMS Meter".
   - Read the steady-state RMS over a 5-10 second window of broadband noise.
6. **Pass criterion: residual ≤ -60 dBFS RMS.** A value between -60 and -90 dBFS is expected; libsamplerate Sinc-Medium gives ~120 dB SNR in best conditions but the conversion-and-back roundtrip is not perfectly transparent.
7. **Fail criterion: residual > -60 dBFS, or residual is non-flat (visible signal-shaped energy in the spectrum, e.g. a pre-echo or late echo).** This indicates PDC misalignment: `setLatencySamples` is reporting a wrong number. Diagnosis path:
   - Verify `srcChain_.getMeasuredLatencyHostSamples()` returns a positive integer at 48 kHz (printf in a debug build).
   - Verify the formula multiplies `coreLatency44k` by `(48000/44100)` and uses `ceil` (not `floor` or rounding).
   - Verify `setLatencySamples` is called BEFORE the first `processBlock` (it should be — JUCE guarantees prepareToPlay runs first).
   - As a fallback, try Ardour instead of Reaper — some Reaper versions have known PDC quirks on dynamic latency changes (ARCHITECTURE-v1.7.md §2.5).
8. Capture the measured residual RMS (e.g. `-71 dBFS`) in the phase SUMMARY's verification section. If the test failed and was fixed, capture both numbers (`pre-fix: -34 dBFS, post-fix: -71 dBFS`) plus the diagnostic note.
9. This step is NOT added to CI. Reaper / Ardour cannot be reliably scripted in CI on a headless runner for an audio-output-comparing test, and PDC verification is exactly the kind of thing that benefits from a human looking at the spectrum. It is, however, a hard gate for closing this phase — without the null-test residual recorded in the SUMMARY, Phase 22 is not done.

**Verify:** Steps 4-7 above.

**Done:** The null-test residual at 48 kHz host SR is captured in `.planning/phases/22-src-latency-reporting/22-PLAN-SUMMARY.md` (created by `/gsd-execute-phase` at phase close). PLUG-15 is satisfied.

**Commit:** No code commit. The SUMMARY commit at phase close records this step's result.

## Goal-Backward Verification

**Phase goal (from ROADMAP):** "Integrate libsamplerate (BSD-2) as the bidirectional SRC at the processBlock boundary. Use SRC_SINC_MEDIUM_QUALITY preset. All SRC state and scratch buffers allocated in prepareToPlay — zero allocation on the audio thread. Bypass SRC entirely when host SR == 44100. setLatencySamples() reports the measured SRC_in + core + SRC_out group delay (measured at integration time, not estimated from documentation). Null-test in Reaper at 48 kHz host SR to confirm PDC alignment. Wrap processBlock with ScopedNoDenormals."

For that goal to be true, all of the following must be observably true after this plan executes:

| Truth | Requirement | Delivered by |
|-------|------|--------------|
| libsamplerate (BSD-2) is in the build graph at a pinned SHA | PLUG-09 | Task 1 |
| Every SRC_STATE is constructed with SRC_SINC_MEDIUM_QUALITY | PLUG-10 | Task 2 |
| One SRC chain runs host_SR→44100; one runs 44100→host_SR; both exercised per non-fast-path block | PLUG-11 | Task 2 + Task 3 |
| All SRC state, scratch, and measurement buffers are allocated in prepareToPlay; processBlock has zero allocations, zero locks, zero syscalls | PLUG-12 | Task 2 (RT-safe construction) + Task 3 (RT-safe wiring) + Task 3 verify (pluginval strictness-7) |
| At hostSampleRate == 44100 (within 1e-9), srcChain.isFastPath() is true and no `src_callback_read` runs (counter == 0) | PLUG-13 | Task 2 (fast-path implementation) + Task 3 verify (counter check at 44.1 kHz) |
| setLatencySamples() is called from prepareToPlay with `measured_in + core_in_host + measured_out`; numbers are measured via impulse response, not from documentation | PLUG-14 | Task 2 (measurement) + Task 3 (wiring) |
| Reaper null-test at 48 kHz produces a residual below -60 dBFS RMS | PLUG-15 | Task 4 |
| juce::ScopedNoDenormals noDenormals; is the first statement of processBlock | PLUG-16 | Task 3 |

**Reachability check:** every must-have artifact has a creation path in the task list above. Every truth has a verifying task. Every PLUG-09..16 ID is mapped to at least one task. The plan is reachable within the four tasks defined.

**Scope-creep self-check:** This plan touches the CMake graph (libsamplerate), creates one new class (SrcChain), modifies one existing class (SPU94AudioProcessor's three lifecycle methods), and performs one manual UAT. It does NOT touch:
- Float↔int16 boundary refactor (Phase 23 — the int16 contract at the spu94_process call is preserved; conversions happen inline inside SrcChain for Phase 22 and move to a dedicated BoundaryConverter in Phase 23)
- `isBusesLayoutSupported` (Phase 25 — assumed stereo-in/stereo-out per Phase 21 default)
- Validator promotion from advisory to gate (Phase 25 — the pluginval-early-warning job already exists at strictness-7 from Phase 21 and stays advisory)
- `getStateInformation` / `setStateInformation` (Phase 24 — stubs at PluginProcessor.cpp:587-595 remain stubs)
- Host automation parameter surface (Phase 24)
- Installers (Phase 26), signing (Phase 27)
- README polish, GitHub remote setup, license picks (out of scope per the user's standing feedback)

## Deferred Ideas (NOT in this phase)

Captured while planning Phase 22; surfaced here so they aren't lost but are explicitly NOT executed here:

- **Float↔int16 conversion as a dedicated BoundaryConverter.** Phase 22 keeps the conversion inline inside SrcChain (`× 32768.0f / clamp` going in, `/ 32768.0f` coming out — mirroring the existing standalone idiom at PluginProcessor.cpp:357-377). Phase 23 will extract this into a `BoundaryConverter` class with `sat_s16` semantics matching the C core's ADR-0001. The SrcChain API is designed so this swap is a `processIn/processOut` signature change inside SrcChain.cpp only — PluginProcessor.cpp is unaffected.

- **isBusesLayoutSupported declaration.** Phase 25 territory. Phase 22 assumes stereo-in/stereo-out per the JUCE default `BusesProperties` from Phase 21. Mono-track Logic loading and mono/mono+mono/stereo+stereo/stereo support is PLUG-32..36, deferred.

- **pluginval --strictness-level 7 promoted from advisory to merge gate.** Phase 25 (PLUG-37, PLUG-41). Phase 22 confirms RT-safety locally and via the existing advisory CI job, but doesn't flip the gate.

- **pluginval matrix expansion to multiple host SRs.** Phase 22's pluginval invocation runs at pluginval's default SR (44.1 kHz on most platforms). A multi-SR matrix (44.1 / 48 / 96 / 192) would catch fast-path-only regressions where the SRC-engaged path has an allocation but the fast-path doesn't. Defer to Phase 25 — pluginval supports `--sample-rate <N>` and the matrix grows naturally as part of the validator-gate work.

- **Promote core scratch int16 buffers from stack to HeapBlock.** The four `int16_t coreInL/coreInR/coreOutL/coreOutR[4096]` stack arrays in the plugin path total 32 KiB. ARCHITECTURE-v1.7.md §4.3 flags this as a fragile pattern for hosts with small audio-thread stacks. Defer to Phase 23 — likely folded into BoundaryConverter's lifetime.

- **SrcChain unit test.** A standalone test (e.g., `tests/plugin/test_srcchain.cpp`) that drives an impulse through processIn → processOut and validates the round-trip null floor would catch the same things as Task 4's manual Reaper test but in CI. Defer because (a) it requires standing up a JUCE-test-utility build that doesn't exist yet, (b) the manual Reaper null-test is hard-gated for this phase and is the canonical PDC verification anyway, (c) Phase 25 is the right home for validator-style automated SRC testing.

- **SR-Quality user-facing knob.** ARCHITECTURE-v1.7.md §2.6 proposed exposing libsamplerate's quality preset (Best / Medium / Fastest / ZOH / Linear) as a user-visible Quality knob. Decision per CONTEXT.md: baked at Medium for v1.7. Not a Phase 22 task. Could be re-introduced as a hidden settings flag in a follow-up phase if CPU profiling reveals it dominates.

- **Side-channel limiter constants tuning.** The `kSideKnee = 0.125`, `kSideCeiling = 0.06` constants at PluginProcessor.cpp:357-377 are a v1.6 design choice. The plugin path uses them verbatim. Re-tuning is a creative-musical decision, not a Phase 22 deliverable, and the user's standing memo on "No naive implementations" applies in the other direction: don't change shipping behaviour without intent.

- **Reaper PDC null-test as a scriptable CI test.** Possible in principle with `reaper -nosplash` and a project file with rendered output, but headless audio-output diffing is a CI-infrastructure rabbit hole. Defer indefinitely; manual UAT in Reaper at the end of each SRC-touching phase is fine.

- **Dynamic latency reporting on host-SR change mid-stream.** PITFALLS B6 notes that some hosts only read getLatencySamples() once at instantiation. Phase 22's `setLatencySamples` is called every prepareToPlay (the only correct policy), but the hosts that ignore it after first read will produce mis-aligned PDC on a mid-stream SR change. There is nothing the plugin can do about that — it's a host bug. Document in beta README (Phase 26): "if you change the project sample rate, remove and re-insert the plugin to refresh PDC."

- **Replacement of libsamplerate by r8brain-free-src or libsoxr.** ARCHITECTURE-v1.7.md §2.2 names both as viable. Decision per CONTEXT.md: libsamplerate. Re-evaluation belongs in v1.8 if CPU or licensing friction emerges. The `cmake/libsamplerate.cmake` shim is designed so the swap is one file's worth of change.

- **Adding a non-blocking pluginval-rt-safety probe job to plugins.yml beyond what already exists.** The existing `pluginval-early-warning` job already runs `--strictness-level 7 --validate-in-process` on the Linux VST3 with `continue-on-error: true`. Phase 22 keeps this as-is (per `ci_decisions` in frontmatter). The CONTEXT.md flagged this as an optional planner discretion point; the planner's decision is "no additional job — the existing one is sufficient and Phase 25 promotes it to a gate."
