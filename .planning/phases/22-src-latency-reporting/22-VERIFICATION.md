---
phase: 22-src-latency-reporting
verified: 2026-05-11T21:00:00Z
status: human_needed
score: 7/8 must-haves verified (PLUG-15 PENDING UAT)
overrides_applied: 0
human_verification:
  - test: "Ardour null-test at 48 kHz host SR (PLUG-15)"
    expected: "Residual RMS <= -60 dBFS over a 5-10 second pink-noise window; no signal-shaped energy visible in spectrum"
    why_human: "Executor cannot drive an interactive DAW session. Procedure requires routing tracks, polarity inversion, Play transport, and reading an RMS meter in Ardour."
---

# Phase 22: SRC & Latency Reporting — Verification Report

**Phase Goal:** Wrap a bidirectional libsamplerate SRC chain around `spu94_process` so plugin formats at any host sample rate feed the C core at 44.1 kHz, report measured group-delay via `setLatencySamples()`, and wrap `processBlock` in `juce::ScopedNoDenormals`. Standalone branch byte-identical to end-of-Phase-21.

**Verified:** 2026-05-11  
**Status:** PARTIAL — 7 of 8 PLUG requirements satisfied; PLUG-15 (PDC null-test) awaits manual UAT  
**Re-verification:** No — initial verification

---

## PLUG Requirement Verdicts (PLUG-09 .. PLUG-16)

| Req | Description | Verdict | Evidence |
|-----|-------------|---------|----------|
| PLUG-09 | libsamplerate (BSD-2-Clause) integrated via FetchContent pinned to SHA `2ccde9568cca73c7b32c97fefca2e418c16ae5e3` | PASS | `cmake/libsamplerate.cmake` line 30: `GIT_TAG 2ccde9568cca73c7b32c97fefca2e418c16ae5e3`; examples/install/tests disabled |
| PLUG-10 | All SRC_STATE handles constructed with `SRC_SINC_MEDIUM_QUALITY` | PASS | `SrcChain.cpp` lines 133 and 136: exactly 2 occurrences, one per direction |
| PLUG-11 | Bidirectional SRC, both directions exercised per non-fast-path block | PASS | `srcIn_[2]` and `srcOut_[2]` constructed in `prepare()`; `srcChain_.processIn` at `PluginProcessor.cpp:432`, `srcChain_.processOut` at `:446`; `srcCallbacksThisBlock_` incremented in both non-fast-path branches |
| PLUG-12 | Zero allocation/locks/syscalls in `processBlock` | PASS | Manual scan of `SrcChain::processIn/processOut` + `processBlock` body: only `memcpy`, `src_callback_read`, inline arithmetic, `atomic::fetch_add(relaxed)`, `jassert`. No `new`/`malloc`/`std::vector`/`std::string`/`juce::String`/`Logger`/`DBG`/`Mutex`/`CriticalSection` found. `SrcChain.cpp` grep confirms clean. |
| PLUG-13 | 44.1 kHz fast-path: no SRC calls, latency = core only | PASS | `SrcChain.cpp:104`: `isFastPath_ = (std::abs(hostSampleRate - 44100.0) < 1.0e-9)`; `processIn:261` and `processOut:316` skip libsamplerate in fast-path; `measuredLatencyHostSamples_ = 0` in fast-path branch |
| PLUG-14 | `setLatencySamples` = measured_in + core_in_host + measured_out, measured via impulse response | PASS | `measureGroupDelayInHostSamples()` uses centre-of-energy on 4096-sample impulse response (`SrcChain.cpp:191-249`); called from `prepare()`; `PluginProcessor.cpp:112`: `setLatencySamples(srcChain_.getMeasuredLatencyHostSamples() + coreLatencyHostSamples)` using `std::ceil()` |
| PLUG-15 | Reaper/Ardour null-test residual <= -60 dBFS at 48 kHz host SR | PENDING UAT | Cannot verify programmatically; see Human Verification section below |
| PLUG-16 | `juce::ScopedNoDenormals` is the first statement of `processBlock` | PASS | `PluginProcessor.cpp:136`: `juce::ScopedNoDenormals noDenormals;` — first executable line after the opening brace |

**Score: 7/8 requirements verified (PLUG-15 pending)**

---

## Must-Haves Checklist

| Must-Have | Status | Evidence |
|-----------|--------|----------|
| `ScopedNoDenormals` is FIRST statement of `processBlock` | PASS | `PluginProcessor.cpp:136`; no other statement precedes it inside the function body |
| `SRC_SINC_MEDIUM_QUALITY` literal appears exactly twice in `SrcChain.cpp` (one per direction) | PASS | `grep -n SRC_SINC_MEDIUM_QUALITY SrcChain.cpp` returns lines 133 and 136 only |
| `setLatencySamples` called from `prepareToPlay` AFTER `srcChain_.prepare()` | PASS | `PluginProcessor.cpp:100`: `srcChain_.prepare(...)`; then `:112`: `setLatencySamples(...)` — strictly after |
| Standalone branch (`wrapperType == wrapperType_Standalone`) preserves WavSource gate; v1.6 testbed behavior unchanged | PASS | `PluginProcessor.cpp:357-410`: `isStandalone` check gates the exact same `wavSource.loaded`/`wavSource.playing` guard + int16 scratch + `spu94_process` call + side-channel limiter constants (`kSideKnee=0.125`, `kSideCeiling=0.06`) unchanged |
| C core unchanged — no edits to libspu94 sources | PASS | `git show --name-only 72660bd 496e7eb 976553b` shows only `cmake/libsamplerate.cmake`, `src/plugin/CMakeLists.txt`, `src/plugin/SrcChain.{h,cpp}`, `src/plugin/PluginProcessor.{h,cpp}` — no `src/spu94` files touched |
| Manual RT-safety scan of `SrcChain::processIn/processOut` + `processBlock` body — clean | PASS | Grep for `new`, `malloc`, `std::vector`, `std::string`, `juce::String`, `juce::Logger`, `DBG`, `Mutex`, `CriticalSection` in `SrcChain.cpp` returned no hits in processIn/processOut. `PluginProcessor.cpp` hits are all outside `processBlock` (message-thread functions). |

---

## Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `cmake/libsamplerate.cmake` | FetchContent shim pinned to SHA; tests/install/examples OFF | VERIFIED | 35-line file; SHA on line 30; all three `OFF` flags on lines 23-25 |
| `src/plugin/SrcChain.h` | Class surface: `prepare/release/reset/processIn/processOut/isFastPath/getMeasuredLatencyHostSamples`; SRC_STATE forward-declared | VERIFIED | 119-line header; all methods present; `struct SRC_STATE_tag` forward-decl at line 30 |
| `src/plugin/SrcChain.cpp` | Full implementation; RT-safe; fast-path; impulse measurement; `SRC_SINC_MEDIUM_QUALITY` x2 | VERIFIED | 368 lines; `measureGroupDelayInHostSamples()` at line 191; both quality literals at lines 133/136 |
| `src/plugin/PluginProcessor.h` | `SrcChain srcChain_` member; `double hostSampleRate_` | VERIFIED | Lines 240-241; `#include "SrcChain.h"` at line 5 |
| `src/plugin/PluginProcessor.cpp` | `ScopedNoDenormals` first; `srcChain_.prepare` then `setLatencySamples`; both `processIn/processOut` in plugin branch; standalone path unchanged | VERIFIED | Lines 100/112/125/136/357/432/446 |
| `src/plugin/CMakeLists.txt` | `include(libsamplerate.cmake)`; `samplerate` in link list; `SrcChain.cpp` in `target_sources` | VERIFIED | Line 52: `SrcChain.cpp`; line 78: `include(cmake/libsamplerate.cmake)`; line 83: `samplerate` in `PRIVATE` link |

---

## Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `SrcChain.cpp` | `<samplerate.h>` | `#include <samplerate.h>` | WIRED | `SrcChain.cpp:13` |
| `CMakeLists.txt` | `cmake/libsamplerate.cmake` | `include(...)` | WIRED | `src/plugin/CMakeLists.txt:78` |
| `CMakeLists.txt` | `samplerate` static target | `target_link_libraries` PRIVATE | WIRED | `src/plugin/CMakeLists.txt:83` |
| `PluginProcessor.h` | `SrcChain.h` | `#include "SrcChain.h"` | WIRED | `PluginProcessor.h:5` |
| `prepareToPlay` | `srcChain_.prepare()` | direct call | WIRED | `PluginProcessor.cpp:100` |
| `prepareToPlay` | `setLatencySamples()` | called after `prepare()` | WIRED | `PluginProcessor.cpp:112` |
| `releaseResources` | `srcChain_.release()` | direct call | WIRED | `PluginProcessor.cpp:125` |
| `processBlock` (plugin branch) | `srcChain_.processIn` | direct call | WIRED | `PluginProcessor.cpp:432` |
| `processBlock` (plugin branch) | `srcChain_.processOut` | direct call | WIRED | `PluginProcessor.cpp:446` |

---

## Build Verification

**Command:** `cmake -S . -B build_test -DCMAKE_BUILD_TYPE=Release && cmake --build build_test --target spu94_plugin_Standalone spu94_plugin_VST3 spu94_plugin_LV2 spu94_plugin_CLAP -j$(nproc)`

**Configure result:** PASS — libsamplerate fetched at pinned SHA; all 4 formats configured; `Configuring done (17.6s)`.

**Build result: ALL 4 TARGETS GREEN**

| Target | Artifact | Size |
|--------|----------|------|
| `spu94_plugin_Standalone` | `Release/Standalone/SPU-94` | present |
| `spu94_plugin_VST3` | `Release/VST3/SPU-94.vst3/Contents/x86_64-linux/SPU-94.so` | present |
| `spu94_plugin_LV2` | `Release/LV2/SPU-94.lv2/libSPU-94.so` | present |
| `spu94_plugin_CLAP` | `Release/CLAP/SPU-94.clap` | present |

No new warnings introduced. Pre-existing `RegisterPanel.cpp` (`-Wshadow`, `-Wfloat-equal`) and `MorphPanel.cpp` (`-Wunused-variable`) warnings are carried forward from Phase 21 and are out of scope.

---

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | — | — | No blockers. No TBD/FIXME/XXX debt markers in phase-22 files. No unreferenced stubs. |

---

## Human Verification Required

### 1. PLUG-15: Ardour PDC Null-Test at 48 kHz

**Status: PENDING UAT — not FAIL**

This requirement is structurally correct (impulse measurement, `setLatencySamples` wired, `ceil()` formula, `src_reset` after measurement) but correctness of the reported latency number cannot be verified without running a DAW session.

**Test procedure (from 22-PLAN-SUMMARY.md):**

1. Open Ardour. New Session — Sample Rate 48000 Hz, Stereo Master out.
2. Create three audio tracks:
   - **Track 1 (Dry):** pink-noise source (WAV import or built-in generator). Output to Master.
   - **Track 2 (Plugin):** same audio source as Track 1. Insert SPU-94 VST3 (`build_test/src/plugin/spu94_plugin_artefacts/Release/VST3/SPU-94.vst3`) or LV2/CLAP. Set Dry Level = 1.0, ADPCM Level = 0.0, Reverb Level = 0.0 (passthrough mode).
   - **Track 3 (Sum):** receives Track 1 (normal polarity) and Track 2 (POLARITY INVERTED via Ardour's polarity-flip). Mute Master out of Tracks 1 and 2; monitor the sum only.
3. Hit Play. Record or measure the sum RMS over 5-10 seconds of broadband noise.
4. **Pass criterion: residual <= -60 dBFS RMS with no signal-shaped energy (pre/late echo) in the spectrum.**
5. **Fail criterion: residual > -60 dBFS OR visible echo artifact.** Indicates PDC misalignment — diagnose via `getMeasuredLatencyHostSamples()` debug log in `prepareToPlay`.

**After running this test:** update 22-PLAN-SUMMARY.md with the residual RMS and PASS/FAIL verdict.

---

## Commit Chain

| Commit | Description |
|--------|-------------|
| `72660bd` | `build(22-01)`: FetchContent libsamplerate via cmake/libsamplerate.cmake (PLUG-09) |
| `496e7eb` | `feat(22-01)`: SrcChain bidirectional SRC wrapper (PLUG-10..13) |
| `976553b` | `feat(22-01)`: SRC sandwich + setLatencySamples + ScopedNoDenormals (PLUG-11..16) |
| `93f2663` | `docs(22-01)`: SUMMARY for SRC + latency reporting plan |

Files changed by the phase: `cmake/libsamplerate.cmake` (new), `src/plugin/SrcChain.h` (new), `src/plugin/SrcChain.cpp` (new), `src/plugin/CMakeLists.txt` (modified), `src/plugin/PluginProcessor.h` (modified), `src/plugin/PluginProcessor.cpp` (modified). No `src/spu94` files touched.

---

## Gaps Summary

No blocking gaps. The only open item is PLUG-15 (Ardour null-test), which is a manual UAT that the executor cannot drive from the CLI. All other requirements are satisfied by the implemented code.

---

## Final Verdict

**PARTIAL** — all 7 automated requirements (PLUG-09, -10, -11, -12, -13, -14, -16) PASS; build green across all 4 Linux plugin formats; must-haves clean; no debt markers.

**Recommendation: Proceed to PLUG-15 UAT before advancing to Phase 23.** The UAT is low-effort (one Ardour session, ~10 minutes) and is the only remaining gate. If PLUG-15 passes, this phase is complete and Phase 23 (bit-depth conversion / BoundaryConverter) may begin. If PLUG-15 fails, the most likely cause is a sign/scale error in the latency formula — diagnostic path is documented in 22-PLAN-SUMMARY.md.

---

_Verified: 2026-05-11_  
_Verifier: Claude (gsd-verifier)_
