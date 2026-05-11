---
phase: 21-build-skeleton-ci-matrix
plan: 01
type: feature
wave: 1
depends_on: []
files_modified:
  - CMakeLists.txt
  - src/standalone/CMakeLists.txt              # rename target → src/plugin/CMakeLists.txt
  - src/standalone/PluginProcessor.cpp         # move (git mv) → src/plugin/
  - src/standalone/PluginProcessor.h           # move (git mv) → src/plugin/
  - src/standalone/PluginEditor.cpp            # move (git mv) → src/plugin/  + wrapperType gate
  - src/standalone/PluginEditor.h              # move (git mv) → src/plugin/
  - src/standalone/MorphPanel.cpp              # move (git mv) → src/plugin/
  - src/standalone/MorphPanel.h                # move (git mv) → src/plugin/
  - src/standalone/RegisterPanel.cpp           # move (git mv) → src/plugin/
  - src/standalone/RegisterPanel.h             # move (git mv) → src/plugin/
  - src/standalone/ParameterBridge.cpp         # move (git mv) → src/plugin/
  - src/standalone/ParameterBridge.h           # move (git mv) → src/plugin/
  - src/standalone/WavLoader.cpp               # stays in src/standalone/ (testbed-only)
  - src/standalone/WavLoader.h                 # stays in src/standalone/ (testbed-only)
  - src/plugin/CMakeLists.txt                  # new: drives the multi-format juce_add_plugin
  - src/plugin/PluginEditor.cpp                # wrapperType gate around WAV-loader UI
  - cmake/clap_juce_extensions.cmake           # new: FetchContent shim for CLAP
  - .github/workflows/plugins.yml              # new: 3-OS × multi-format build + smoke
autonomous: true
requirements:
  - PLUG-01
  - PLUG-02
  - PLUG-03
  - PLUG-04
  - PLUG-05
  - PLUG-06
  - PLUG-07
  - PLUG-08
  - PLUG-49
  - PLUG-50
  - PLUG-51

must_haves:
  truths:
    - "From a clean checkout, `cmake -S . -B build && cmake --build build` on Linux produces SPU-94.vst3, SPU-94.lv2, SPU-94.clap, and the SPU-94 standalone binary."
    - "From a clean checkout on macOS, the same build produces SPU-94.vst3, SPU-94.component (AU), SPU-94.clap, and SPU-94.app — no LV2 target is generated."
    - "From a clean checkout on Windows, the same build produces SPU-94.vst3, SPU-94.clap, and SPU-94.exe — no LV2 target is generated."
    - "Loading any built plugin binary into one host of its native format succeeds and the SPU-94 GUI appears."
    - "In every plugin format, the WAV-loader buttons (Load WAV / Play / Stop) and their bounds slots are absent from the editor. The toolbar row shows an empty space where the WAV-loader buttons were (x=10 through x=265). Other controls do NOT reflow into this space — this is intentional, toolbar polish is deferred to a later UI phase. Standalone toolbar layout is unchanged."
    - "In the standalone binary on every OS, the WAV-loader UI is present and behaves identically to v1.6 — Load → Play → Stop round-trips a WAV through the engine."
    - "GitHub Actions `plugins.yml` runs on every push to `main` and exercises all 11 user-facing build targets (Linux: VST3+LV2+CLAP+STA = 4; macOS: VST3+AU+CLAP+STA = 4; Windows: VST3+CLAP+STA = 3). A red light on any target fails the workflow."
    - "ccache and FetchContent caches are warmed between runs — a no-source-change re-run completes in under half the cold-build time."
    - "The chosen AU codes (PLUGIN_MANUFACTURER_CODE='Spu9', PLUGIN_CODE='Sp94') are present in CMakeLists.txt and recorded here for Phase 25's auval setup."

  artifacts:
    - path: "src/plugin/CMakeLists.txt"
      provides: "The single juce_add_plugin target with full FORMATS list (per-OS-gated; LV2 on Linux only) and clap-juce-extensions wiring"
      contains: "juce_add_plugin(spu94_plugin"
    - path: "src/plugin/PluginProcessor.cpp"
      provides: "AudioProcessor implementation, moved from src/standalone/ unchanged in behavior"
    - path: "src/plugin/PluginEditor.cpp"
      provides: "Editor + wrapperType gate around WAV-loader UI (load/play/stop buttons and their click handlers and bounds)"
      contains: "wrapperType == wrapperType_Standalone"
    - path: "src/standalone/CMakeLists.txt"
      provides: "Optional small target that adds WavLoader.cpp to the plugin's standalone build only (or, equivalently, an INTERFACE source list consumed by the plugin target when Standalone is in FORMATS)"
    - path: "src/standalone/WavLoader.cpp"
      provides: "Unchanged testbed-only WAV decoder"
    - path: "cmake/clap_juce_extensions.cmake"
      provides: "FetchContent_Declare for free-audio/clap-juce-extensions pinned to a SHA, plus the clap_juce_extensions_plugin() call helper"
      contains: "FetchContent_Declare"
    - path: ".github/workflows/plugins.yml"
      provides: "3-OS matrix workflow, per-OS dep install, configure+build, ccache + JUCE FetchContent cache, builds-and-loads smoke per binary"
      contains: "matrix:"

  key_links:
    - from: "CMakeLists.txt"
      to: "src/plugin/CMakeLists.txt"
      via: "add_subdirectory(src/plugin) — replaces add_subdirectory(src/standalone)"
      pattern: "add_subdirectory\\(src/plugin\\)"
    - from: "CMakeLists.txt"
      to: "src/standalone/CMakeLists.txt"
      via: "add_subdirectory(src/standalone) — kept; now contains the WavLoader source-list only"
      pattern: "add_subdirectory\\(src/standalone\\)"
    - from: "src/plugin/CMakeLists.txt"
      to: "cmake/clap_juce_extensions.cmake"
      via: "include() then clap_juce_extensions_plugin(TARGET spu94_plugin ...)"
      pattern: "clap_juce_extensions_plugin"
    - from: "src/plugin/PluginEditor.cpp"
      to: "processorRef.wrapperType"
      via: "if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone) { addAndMakeVisible(loadButton); ... }"
      pattern: "wrapperType_Standalone"
    - from: ".github/workflows/plugins.yml"
      to: "build artifacts"
      via: "per-OS cmake --build then a smoke step that loads each binary via pluginval --validate-in-process (or equivalent host-load probe per format)"
      pattern: "pluginval|smoke"

success_criteria:
  - "PLUG-01..05: Every required (format × OS) cell in the matrix produces its expected binary on a green CI run. Binary count: Linux 4 (VST3+LV2+CLAP+STA) + macOS 4 (VST3+AU+CLAP+STA) + Windows 3 (VST3+CLAP+STA) = 11."
  - "PLUG-06: `cmake -S . -B build` resolves clap-juce-extensions via FetchContent — no manual git submodule add required."
  - "PLUG-07: Pushing to `main` triggers `plugins.yml`; the workflow's matrix completes with all 11 user-facing build targets green."
  - "PLUG-08: Each binary passes a builds-and-loads smoke probe (pluginval `--validate-in-process` on Linux+Windows for VST3/CLAP, equivalent host-load smoke on macOS for AU/VST3/CLAP, standalone binary `--help` or `--version` exit 0). DSP correctness is NOT asserted at this phase."
  - "PLUG-49: In every plugin format, the WAV-loader UI is invisible (no Load WAV / Play / Stop buttons rendered). Verified visually by loading one plugin format per OS in CI's smoke step (or by an editor unit-snapshot test if added)."
  - "PLUG-50: `git log --follow` on `src/plugin/PluginProcessor.cpp` traces back to the v1.6 `src/standalone/PluginProcessor.cpp`. `src/standalone/` after the rename contains only WavLoader.{cpp,h} + CMakeLists.txt."
  - "PLUG-51: Standalone construction-and-exit smoke passes on all three OSes in CI. v1.6 Load → Play → Stop round-trip is verified manually on the executor's host OS (Linux); macOS/Windows standalone Load → Play → Stop round-trip is exercised opportunistically when later phases run on those runners (Phase 22+)."

au_codes:
  manufacturer_code: "Spu9"       # already used by the v1.6 standalone target — keep for continuity
  plugin_code: "Sp94"             # changed from "Spv1" (v1.6 placeholder) to "Sp94" — readable, project-specific, low collision risk
  rationale: "Spu9 was already chosen in the v1.6 standalone CMakeLists for PLUGIN_MANUFACTURER_CODE; reusing it avoids invalidating the standalone's AU cache. Sp94 replaces the v1.6 placeholder Spv1 with a code that telegraphs the product name. Both are 4 ASCII characters as Apple requires. Cross-checked informally against the audio-plugin community AU-code registries — no widely-distributed plugin owns (aufx, Sp94, Spu9). If Phase 25's auval setup discovers a collision in the wild, the planner-owned change here is one line in CMakeLists.txt."

ci_decisions:
  macos_runner: "macos-14"
  rationale: "macos-14 is Apple Silicon by default; produces native arm64 binaries with the option to cross-compile a universal2 via CMAKE_OSX_ARCHITECTURES=arm64;x86_64. macos-13 still available as an Intel-default fallback if needed but adds no value for v1.7 (we are not yet shipping universal2 in this phase — that lands in Phase 26 packaging). Conservative: ship arm64-only from macos-14 in Phase 21, add x86_64 slice in Phase 26."
  linux_runner: "ubuntu-22.04"
  windows_runner: "windows-2022"
  toolchains: "GCC (Linux apt default), Apple Clang (Xcode), MSVC (VS 2022). MinGW explicitly rejected per PITFALLS-v1.7.md M4."
  caching: "actions/cache keyed on (a) the JUCE FetchContent git SHA, (b) the clap-juce-extensions FetchContent git SHA, (c) ccache directory per OS. Cold builds expected at 10-15 min/OS; warm at 3-5 min/OS per STACK-v1.7.md §3 baselines."
  smoke_test: "pluginval --validate-in-process --strictness-level 1 against each VST3 + CLAP on Linux and Windows. macOS uses pluginval against VST3/AU/CLAP. Standalone smoke = run the binary with an env flag that triggers immediate exit after construction (or, if not available, launch with timeout 5s and confirm zero exit). NO strictness-level-7 here — that is Phase 25's gate."
  optional_early_warning: "Non-blocking pluginval --strictness-level 7 on the Linux VST3 only, marked continue-on-error. Surfaces validator regressions early but does not block merges. Decision: INCLUDED — costs ~30s/run and gives early signal on RT-safety regressions in Phase 22-24 work."

tasks:
  - id: 1
    name: "Rename src/standalone/ → src/plugin/; carve out new small src/standalone/ for WavLoader.{cpp,h}; fix CMake wiring so the standalone build is still green at the end of this commit."
  - id: 2
    name: "Expand the plugin's CMakeLists to declare all formats (VST3 + LV2 + CLAP + AU + Standalone, per-OS-gated; LV2 on Linux only); choose and commit AU codes (PLUGIN_MANUFACTURER_CODE=Spu9, PLUGIN_CODE=Sp94); integrate clap-juce-extensions via cmake/clap_juce_extensions.cmake FetchContent shim."
  - id: 3
    name: "Gate the WAV-loader UI in PluginEditor.cpp on `processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone`; verify the standalone still loads/plays a WAV and that a locally-built VST3 (loaded in a Linux host of choice, e.g. Carla or Ardour) shows the GUI without the Load/Play/Stop buttons."
  - id: 4
    name: "Add .github/workflows/plugins.yml: 3-OS matrix, per-OS apt/brew/choco dep install, FetchContent + ccache caching, configure+build all formats, builds-and-loads smoke per binary (pluginval `--validate-in-process` for plugin formats; binary-launch-with-timeout for standalones). Include the non-blocking Linux-VST3 pluginval --strictness-level 7 early-warning job (continue-on-error)."
---

# Phase 21 Plan 01: Build Skeleton & CI Matrix

Expand the existing JUCE `FORMATS Standalone` setup into the full v1.7 plugin distribution surface (VST3 + AU + LV2 + CLAP + Standalone, per-OS-gated; LV2 on Linux only), reorganize the source tree to reflect the plugin's new role as the v1.7 product, and stand up the 3-OS GitHub Actions matrix that builds and smoke-tests all 11 user-facing binaries on every push to `main`.

This phase produces **binaries that build and load**. It does not change any DSP behavior, does not introduce sample-rate conversion, does not touch the float↔int16 boundary, and does not declare bus configurations beyond JUCE defaults — all of that is downstream (Phases 22, 23, 25). The plugin formats produced here will, when loaded into a 48 kHz host, technically run the engine at the wrong sample rate; that is acceptable for this phase because Phase 22 ships immediately after and lands the SRC chain. The point of Phase 21 is to make the CI matrix exist so Phases 22–26 have something to extend.

## Rationale

Per ARCHITECTURE-v1.7.md §0 (the "v1.7 is smaller than it looked" finding), the existing standalone is already a canonical `juce::AudioProcessor` + `AudioProcessorEditor` pair driven by `juce_add_plugin(... FORMATS Standalone)`. The work to add VST3/AU/LV2/CLAP is almost entirely CMake: extend the `FORMATS` list, add the per-OS guards, wire the clap-juce-extensions shim, and pick AU codes. The only source-code change is one `if (wrapperType == wrapperType_Standalone)` gate in `PluginEditor.cpp` around the WAV-loader UI. The folder rename (`src/standalone/` → `src/plugin/`, with a new small `src/standalone/` holding only `WavLoader.{cpp,h}`) is a tracking-clarity move so the directory name reflects each file's audience.

The CI matrix is the load-bearing deliverable. Without it, Phases 22–26 are working blind on Linux only — and the per-OS surprises (B7 bundle structure, M3 VST3 SDK auto-download, M4 MSVC vs MinGW, M2 Xcode drift) compound late in the milestone. Standing up CI first means every subsequent phase ships green-or-not-merged.

## Design

### Folder reorganization (Task 1)

```
Before:                              After:
src/                                 src/
├── spu94/                           ├── spu94/                  (unchanged)
├── cli/                             ├── cli/                    (unchanged)
└── standalone/                      ├── plugin/                 (renamed; everything except WavLoader)
    ├── PluginProcessor.{cpp,h}      │   ├── PluginProcessor.{cpp,h}
    ├── PluginEditor.{cpp,h}         │   ├── PluginEditor.{cpp,h}
    ├── MorphPanel.{cpp,h}           │   ├── MorphPanel.{cpp,h}
    ├── RegisterPanel.{cpp,h}        │   ├── RegisterPanel.{cpp,h}
    ├── ParameterBridge.{cpp,h}      │   ├── ParameterBridge.{cpp,h}
    ├── WavLoader.{cpp,h}            │   └── CMakeLists.txt      (drives juce_add_plugin)
    └── CMakeLists.txt               └── standalone/             (new, small)
                                         ├── WavLoader.{cpp,h}
                                         └── CMakeLists.txt      (exposes wavloader source list)
```

The plugin's `juce_add_plugin` target (renamed `spu94_standalone` → `spu94_plugin`) gets its sources from `src/plugin/`. When `Standalone` is in `FORMATS`, the plugin target additionally pulls in `src/standalone/WavLoader.cpp`. The cleanest CMake shape is an `INTERFACE` source-list target in `src/standalone/CMakeLists.txt` that the plugin links — but a plain `target_sources(spu94_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)` from the plugin's CMakeLists is equally valid and one fewer indirection. Executor picks; the result is the same: WavLoader compiles into every JUCE wrapper, but is gated at runtime by wrapperType.

The wavSource bookkeeping inside `PluginProcessor.cpp` (the WavSource struct, pendingSlots double-buffer, processBlock WAV-reading path, `loadWavFile`/`startPlayback`/`stopPlayback` methods) STAYS IN THE PROCESSOR for Phase 21 — moving it out is a behavior-change refactor that belongs in Phase 22+ when the SRC chain replaces the standalone-WAV-only audio path with host-buffer audio. For this phase the wavSource path is inert in plugin wrappers because (a) the editor's load/play/stop buttons are gated out by wrapperType, so `wavSource.loaded` stays false, and (b) `processBlock` already early-returns with `buffer.clear()` when `wavSource.loaded || wavSource.playing` is false (`PluginProcessor.cpp:315-320`). The plugin formats will therefore output silence in this phase. That is acceptable and explicit: **Phase 21 binaries load but do not pass audio**. Phase 22's SRC + host-buffer integration is what makes them produce sound.

### CMake structure (Task 2)

```cmake
# src/plugin/CMakeLists.txt
set(SPU94_FORMATS Standalone VST3)
if(UNIX AND NOT APPLE)
    list(APPEND SPU94_FORMATS LV2)
endif()
if(APPLE)
    list(APPEND SPU94_FORMATS AU)
endif()
# CLAP is added via clap-juce-extensions on all three OSes (no JUCE-native FORMAT).
# LV2 is Linux-only per PLUG-03 — macOS and Windows do not generate an LV2 target.

juce_add_plugin(spu94_plugin
    PRODUCT_NAME             "SPU-94"
    COMPANY_NAME             "SPU-94 Project"
    BUNDLE_ID                "com.spu94project.spu94"
    PLUGIN_NAME              "SPU-94"
    PLUGIN_MANUFACTURER_CODE Spu9        # locked here for AU-cache continuity with v1.6 standalone
    PLUGIN_CODE              Sp94        # changed from v1.6 placeholder "Spv1"
    FORMATS                  ${SPU94_FORMATS}
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Reverb
    LV2URI                   "https://spu94project.org/spu94"
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_SYNTH                 FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

# CLAP shim — included from a separate file so the FetchContent boilerplate
# isn't sprinkled across the plugin CMake.
include(${CMAKE_SOURCE_DIR}/cmake/clap_juce_extensions.cmake)
clap_juce_extensions_plugin(TARGET spu94_plugin
    CLAP_ID "com.spu94project.spu94"
    CLAP_FEATURES audio-effect stereo reverb)

# Standalone-only source bolt-on
if("Standalone" IN_LIST SPU94_FORMATS)
    target_sources(spu94_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_include_directories(spu94_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/standalone)
endif()
```

`cmake/clap_juce_extensions.cmake` is a thin shim that `FetchContent_Declare`s `https://github.com/free-audio/clap-juce-extensions` pinned to a verified-good SHA (MIT-licensed; STACK-v1.7.md §1 confirms it's the production-standard path for JUCE-8-plus-CLAP), calls `FetchContent_MakeAvailable`, and exposes the `clap_juce_extensions_plugin()` macro. Pinning by SHA matches the project's existing JUCE FetchContent discipline.

### wrapperType gate (Task 3)

Single `if`-block around the WAV-loader UI in `PluginEditor.cpp`. The current unconditional `addAndMakeVisible(loadButton)` / `addAndMakeVisible(playButton)` / `addAndMakeVisible(stopButton)` becomes:

```cpp
if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [...];
    addAndMakeVisible(playButton);
    playButton.onClick = [...];
    addAndMakeVisible(stopButton);
    stopButton.onClick = [...];
}
```

The `setBounds` calls at lines 331-333 are similarly wrapped. In every non-standalone wrapper (`wrapperType_VST3`, `wrapperType_AudioUnit`, `wrapperType_LV2`, `wrapperType_CLAP` via the shim), the three buttons are never instantiated as visible children, never positioned, and never participate in the editor's layout. This matches ARCHITECTURE-v1.7.md §7.3's "one small if-statement, not a separate processor class" pattern.

When the WAV-loader buttons are gated out of the toolbar in plugin formats, the toolbar row contains an empty space between x=10 and x=265 (before `savePresetButton` at x=270). **This hole is accepted as intentional for Phase 21.** Phase 21 is build-skeleton scaffolding, not GUI polish — reflowing the toolbar to fill the gap is editor-layout work that belongs in a later UI phase (potentially in v1.7 packaging, or post-v1.7). The standalone toolbar layout is unchanged.

### CI matrix (Task 4)

A new workflow file `.github/workflows/plugins.yml` (separate from the existing `ci.yml`, which handles the C core's static analysis / UBSan / grep-guard / etc. and stays untouched). Structure:

```yaml
jobs:
  build-plugins:
    strategy:
      fail-fast: false
      matrix:
        include:
          - { os: ubuntu-22.04, name: linux }
          - { os: macos-14,     name: macos }
          - { os: windows-2022, name: windows }
    runs-on: ${{ matrix.os }}
    steps:
      - checkout (with submodules: recursive)
      - cache FetchContent (key: JUCE-SHA + clap-juce-extensions-SHA)
      - cache ccache (per-OS)
      - install per-OS deps (apt: libasound2-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libglu1-mesa-dev libx11-dev | brew: ninja | choco: ninja)
      - configure: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - build:     cmake --build build
      - install pluginval (download + extract platform binary)
      - smoke (per format): pluginval --validate-in-process --strictness-level 1 <artifact>
      - smoke (standalone): timeout 5 <standalone-binary> || true; assert binary file exists and is executable
      - upload-artifact: per-OS plugin bundles for downstream phase use

  pluginval-early-warning:
    needs: build-plugins
    runs-on: ubuntu-22.04
    continue-on-error: true                     # advisory only — NOT a merge gate
    steps:
      - download Linux VST3 artifact
      - pluginval --validate-in-process --strictness-level 7 SPU-94.vst3
```

The `continue-on-error: true` on the strictness-7 job means a regression there shows as a yellow warning in the PR check list but does NOT block merge. Phase 25 promotes it to a hard gate.

## Test Coverage

| Test | What | Where |
|------|------|-------|
| Cold-build matrix | Every OS × every supported format produces its binary from a clean checkout | `plugins.yml` `build-plugins` job |
| Smoke-load per format | `pluginval --validate-in-process` on every VST3/AU/CLAP/LV2 binary returns 0 | `plugins.yml` per-format smoke step |
| Standalone launch | Standalone binary runs to construction without crashing (5s timeout) | `plugins.yml` standalone smoke step |
| WAV-loader gating (standalone) | Manual local check by the executor: run the Linux standalone, verify Load/Play/Stop buttons appear and behave as v1.6 | Executor confirmation in Task 3 |
| WAV-loader gating (plugin) | Manual local check by the executor: load the Linux VST3 (or CLAP) into Carla/Ardour, verify Load/Play/Stop buttons are absent and the toolbar shows an empty space at x=10..265 (other controls do not reflow) | Executor confirmation in Task 3 |
| Early-warning regression | pluginval --strictness-level 7 on Linux VST3 — advisory | `plugins.yml` `pluginval-early-warning` job |

## Task Detail

### Task 1: Folder reorganization

**Files:** entire `src/standalone/` directory move; `CMakeLists.txt` root `add_subdirectory` line; `src/standalone/CMakeLists.txt` rewrite.

**Action:**
1. `git mv src/standalone src/plugin` — moves all 13 files (6 .cpp + 6 .h + CMakeLists.txt).
2. `git mv src/plugin/WavLoader.cpp src/standalone/WavLoader.cpp` and `git mv src/plugin/WavLoader.h src/standalone/WavLoader.h` — extract the two testbed-only files back into the new small `src/standalone/`. Create `src/standalone/CMakeLists.txt` with no targets of its own (it exposes the WavLoader source-list for the plugin target to pick up); see CMake design above.
3. Update root `CMakeLists.txt`: replace `add_subdirectory(src/standalone)` with `add_subdirectory(src/plugin)` and re-add `add_subdirectory(src/standalone)` after it (Phase 21's `src/standalone/` is now a sibling, not a replacement). Order matters: `src/plugin` first so its target exists when `src/standalone` is processed, OR the WavLoader bolt-on lives entirely inside `src/plugin/CMakeLists.txt` (simpler — see Task 2's CMake).
4. Rewrite `src/plugin/CMakeLists.txt`: rename target `spu94_standalone` → `spu94_plugin`; remove `WavLoader.cpp` from `target_sources` (it comes in via the standalone bolt-on); leave everything else identical to v1.6 for this commit. Standalone still builds because FORMATS is still just `Standalone` at the end of Task 1.
5. Fix any `#include "WavLoader.h"` in `src/plugin/PluginProcessor.cpp` to `#include "../standalone/WavLoader.h"` (or, cleaner, add `${CMAKE_SOURCE_DIR}/src/standalone` to the plugin target's include path and keep the bare `#include "WavLoader.h"` — preferred).

**Verify:** `cmake -S . -B build && cmake --build build --target spu94_plugin_Standalone` builds; running the resulting binary launches the v1.6 standalone GUI; Load/Play/Stop round-trips a WAV. `git log --follow src/plugin/PluginProcessor.cpp` shows the file's history reaches back to its v1.6 commits in `src/standalone/`.

**Done:** Folder layout matches the diagram above; standalone behavior is byte-identical to v1.6; no test regressions.

**Commit:** `refactor(v1.7): rename src/standalone/ → src/plugin/; carve WavLoader into new src/standalone/ (PLUG-50)`

---

### Task 2: Multi-format CMake + AU codes + CLAP shim

**Files:** `src/plugin/CMakeLists.txt`, `cmake/clap_juce_extensions.cmake` (new), `CMakeLists.txt` (root, only if needed for the CLAP shim include path).

**Action:**
1. In `src/plugin/CMakeLists.txt`: replace `FORMATS Standalone` with the per-OS-gated `${SPU94_FORMATS}` list (see CMake design above; LV2 appended only when `UNIX AND NOT APPLE` — Linux only per PLUG-03). Add `VST3_CATEGORIES "Fx Reverb"`, `AU_MAIN_TYPE kAudioUnitType_Effect`, `LV2URI "https://spu94project.org/spu94"`. Change `PLUGIN_CODE Spv1` → `PLUGIN_CODE Sp94`. Leave `PLUGIN_MANUFACTURER_CODE Spu9` as-is. Record the chosen codes in a header comment for Phase 25.
2. Create `cmake/clap_juce_extensions.cmake`: `FetchContent_Declare(clap_juce_extensions GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git GIT_TAG <SHA>)` + `FetchContent_MakeAvailable`. The executor resolves the current `main`-branch SHA at commit time via `git ls-remote https://github.com/free-audio/clap-juce-extensions.git HEAD` and pins it (matching the project's JUCE FetchContent SHA-pin discipline at root CMakeLists.txt:30).
3. In `src/plugin/CMakeLists.txt` after the `juce_add_plugin` block: `include(${CMAKE_SOURCE_DIR}/cmake/clap_juce_extensions.cmake)` then `clap_juce_extensions_plugin(TARGET spu94_plugin CLAP_ID "com.spu94project.spu94" CLAP_FEATURES audio-effect stereo reverb)`.
4. Add the Standalone-only WavLoader bolt-on (see CMake design's `if("Standalone" IN_LIST SPU94_FORMATS)` block).
5. Bump `target_compile_definitions` to add `JUCE_VST3_CAN_REPLACE_VST2=0` (avoids a JUCE-default that we don't want and that pluginval flags as a warning at higher strictness levels).

**Verify (local, Linux):**
- `cmake -S . -B build -G Ninja` configures without error; FetchContent pulls clap-juce-extensions into `build/_deps/`.
- `cmake --build build` produces (under `build/src/plugin/SPU-94_artefacts/Release/`):
  - `VST3/SPU-94.vst3/` (bundle)
  - `LV2/SPU-94.lv2/` (bundle)
  - `CLAP/SPU-94.clap` (single file)
  - `Standalone/SPU-94` (binary)
- File extensions/bundle structures match B7 expectations.
- **macOS verify (when CI exercises macos-14):** produces VST3 bundle, `SPU-94.component` (AU) bundle, `SPU-94.clap`, and `SPU-94.app` standalone. **No `SPU-94.lv2` is generated on macOS** — LV2 is Linux-only.
- **Windows verify (when CI exercises windows-2022):** produces VST3 bundle, `SPU-94.clap`, and `SPU-94.exe` standalone. No AU, no LV2.

**Done:** All four Linux formats build from a single CMake invocation; macOS produces VST3+AU+CLAP+Standalone (no LV2); Windows produces VST3+CLAP+Standalone (no AU, no LV2); AU codes are locked and documented; CLAP shim is wired via cmake/clap_juce_extensions.cmake with a pinned SHA. Standalone still launches and behaves as v1.6.

**Commit:** `feat(v1.7): enable VST3 + LV2 (linux) + CLAP + AU (macos) formats; pick AU codes (Spu9/Sp94); FetchContent clap-juce-extensions (PLUG-01..06)`

---

### Task 3: wrapperType gate around WAV-loader UI

**Files:** `src/plugin/PluginEditor.cpp` (and `.h` if needed for forward decls — likely not).

**Action:**
1. Wrap the WAV-loader UI block (current lines 10-43 of the v1.6 PluginEditor.cpp: `addAndMakeVisible(loadButton)` through the `stopButton.onClick` lambda body) in `if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone) { ... }`.
2. Wrap the corresponding `setBounds` calls at lines 331-333 in the same `wrapperType_Standalone` guard. Do NOT add reflow logic for the freed toolbar slots — in plugin formats the toolbar row will show an empty space between x=10 and x=265 (before `savePresetButton` at x=270). This hole is intentional for Phase 21; toolbar polish is deferred to a later UI phase. Standalone toolbar layout is unchanged.
3. Do NOT change the underlying `loadButton` / `playButton` / `stopButton` member declarations in `PluginEditor.h` — they are cheap default-constructed `juce::TextButton` instances. Leaving them as members keeps the editor class layout stable for the standalone path and means the only change is the runtime visibility gate.
4. Do NOT touch the WavSource bookkeeping inside `PluginProcessor.cpp`. The processor already early-returns at lines 315-320 when `wavSource.loaded || wavSource.playing` is false. In non-standalone wrappers the editor never calls `loadWavFile` / `startPlayback`, so the gate is effective without processor-side changes.

**Verify:**
- Build `cmake --build build --target spu94_plugin_Standalone && ./build/src/plugin/.../Standalone/SPU-94` — Load/Play/Stop buttons appear and behave as v1.6.
- Build `cmake --build build --target spu94_plugin_VST3`, then load `SPU-94.vst3` into Carla or Ardour (Linux executor's choice). Confirm the editor opens, the morph knob and register panel are present, the Load/Play/Stop buttons are absent, and the toolbar shows an empty space at x=10..265 (other controls remain in their original positions; no reflow). The plugin will output silence (Phase 22 lands the audio path) — this is expected.

**Done:** Standalone WAV path unchanged; plugin formats present a WAV-loader-free editor with an intentional empty-space hole in the toolbar (x=10..265); the gate is a single visible `if`-block in PluginEditor.cpp.

**Commit:** `feat(v1.7): gate WAV-loader UI on wrapperType_Standalone (PLUG-49)`

---

### Task 4: GitHub Actions plugins.yml matrix

**Files:** `.github/workflows/plugins.yml` (new). Existing `.github/workflows/ci.yml` is untouched.

**Action:**
1. Create `.github/workflows/plugins.yml` per the structure outlined in the Design section above. Triggers: `push` and `pull_request` on `main`. Concurrency group as in existing `ci.yml`.
2. Pin every third-party action to a full commit SHA per the project's existing convention (see `.github/workflows/ci.yml` lines 1-12). At minimum: `actions/checkout`, `actions/cache`, `actions/upload-artifact`. Reuse SHAs already pinned in `ci.yml` for actions used in both workflows.
3. Per-OS dep install:
   - **Linux:** apt-get install ninja-build libasound2-dev libjack-jackd2-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libglu1-mesa-dev libx11-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev (the last two are needed by JUCE's modules even though we disable them via `JUCE_WEB_BROWSER=0`/`JUCE_USE_CURL=0` — Linux Dependencies.md still lists them as link-time requirements unless the JUCE module is excluded entirely).
   - **macOS:** brew install ninja (Xcode is preinstalled).
   - **Windows:** choco install ninja (VS 2022 is preinstalled).
4. Caching:
   - `actions/cache` on `${{ runner.os }}-fetchcontent` keyed on the concatenation of the JUCE SHA (from root CMakeLists.txt) + the clap-juce-extensions SHA (from cmake/clap_juce_extensions.cmake). Path: `build/_deps/`.
   - `actions/cache` on `${{ runner.os }}-ccache` keyed on `${{ github.run_id }}` with a restore-key fallback. Path: `~/.ccache` (Linux/macOS), `%LOCALAPPDATA%\ccache` (Windows).
   - Install `ccache` on Linux (apt) and macOS (brew); skip on Windows (msvc + ccache integration is more friction than the speedup warrants — STACK-v1.7.md §3 baseline of 10-15min cold / 3-5min warm holds without it on macos/linux).
5. Configure step: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` (drop the compiler-launcher flags on Windows).
6. Build step: `cmake --build build --config Release`.
7. pluginval install: download the platform-appropriate prebuilt from `Tracktion/pluginval` releases (pinned to a specific release tag); extract and add to PATH.
8. Smoke per format (per-OS branched in shell):
   - **Linux:** `pluginval --validate-in-process --strictness-level 1 build/src/plugin/SPU-94_artefacts/Release/VST3/SPU-94.vst3` ; same for the LV2 bundle and CLAP file. (Linux smoke targets: VST3, LV2, CLAP.)
   - **macOS:** same against VST3 bundle, the AU `.component` bundle, and the CLAP file. (macOS smoke targets: VST3, AU, CLAP — **no LV2**.)
   - **Windows:** same against VST3 bundle and CLAP file. (Windows smoke targets: VST3, CLAP — no AU, no LV2.)
   - All OSes: standalone smoke via `timeout 5 <standalone-binary> || true` then `test -x <standalone-binary>` (Windows: PowerShell equivalent — `Start-Process -FilePath ... -PassThru | Stop-Process` after 5s; assert exit 0). This is a construction-and-exit smoke only; v1.6's Load → Play → Stop round-trip is NOT asserted in CI (executor verifies locally on Linux during Task 3; macOS/Windows round-trip is exercised opportunistically in later phases).
9. `actions/upload-artifact` per OS, uploading the built bundles under unique names (`spu94-linux`, `spu94-macos`, `spu94-windows`) for downstream phase smoke tests and packaging (Phase 26).
10. Separate `pluginval-early-warning` job: needs `build-plugins`, runs only on `ubuntu-22.04`, `continue-on-error: true`. Downloads the Linux artifact and runs `pluginval --validate-in-process --strictness-level 7 SPU-94.vst3`. Failure shows as a yellow check, not a red one; does not block merge.

**Verify:**
- Push the branch; the `plugins.yml` workflow appears in Actions tab.
- On a clean cache run, all three matrix jobs complete (expected wall time ~10-15 min/OS per STACK-v1.7.md §3).
- On a no-source-change re-run, all three complete in under half that time (cache hits warm).
- Every per-format smoke step exits 0. Total user-facing binaries exercised: 4 (Linux: VST3+LV2+CLAP+STA) + 4 (macOS: VST3+AU+CLAP+STA) + 3 (Windows: VST3+CLAP+STA) = 11.
- The advisory `pluginval-early-warning` job reports its result without affecting the overall workflow conclusion.

**Done:** `plugins.yml` is green on a clean push to `main`. All 11 user-facing build targets build and pass the builds-and-loads smoke gate. Caching is functional.

**Commit:** `ci(v1.7): add 3-OS plugin build + smoke matrix (PLUG-07, PLUG-08)`

## Goal-Backward Verification

**Phase goal (from ROADMAP):** "Expand JUCE FORMATS… Integrate clap-juce-extensions… Rename src/standalone/ → src/plugin/ and move WavLoader… Gate the WAV-loader UI on wrapperType_Standalone… Establish a 3-OS GitHub Actions matrix that builds all 11 user-facing binaries on every push to main. Cache submodules + ccache. Smoke test: each binary builds-and-loads."

For that goal to be true, all of the following must be observably true after this plan executes:

| Truth | Delivered by |
|-------|--------------|
| FORMATS is expanded from `Standalone` to all four production formats + Standalone, per-OS-gated (LV2 Linux-only) | Task 2 |
| clap-juce-extensions is wired via CMake FetchContent (no submodule fiddling) | Task 2 |
| `src/standalone/` → `src/plugin/` rename has happened; new small `src/standalone/` holds only WavLoader.{cpp,h} | Task 1 |
| WAV-loader UI is gated on `wrapperType_Standalone` in PluginEditor.cpp; toolbar hole in plugin formats is accepted | Task 3 |
| GitHub Actions matrix exists and exercises all 3 OSes | Task 4 |
| All 11 user-facing build targets (Linux 4 + macOS 4 + Windows 3) build on every push to main | Task 4 |
| Submodule + FetchContent + ccache caching is wired | Task 4 |
| Each binary passes a builds-and-loads smoke test | Task 4 |

**Reachability check:** every must-have artifact has a creation path in the task list above. Every truth has a verifying task. The plan is reachable.

**Scope-creep self-check:** This plan touches the build system, the editor's one gating `if`-block, and CI. It does NOT touch SRC, float↔int16, state serialization, bus declarations, automation surface, validators-as-gates, installers, signing, or toolbar reflow polish — all of those are deferred to their own phases (22-27, or a later UI polish phase) per CONTEXT.md.

## Deferred Ideas (NOT in this phase)

Captured while planning Phase 21; surfaced here so they aren't lost but are explicitly NOT executed here:

- **Plugin-format toolbar layout polish.** When the WAV-loader buttons are gated out, the toolbar row has an empty space between x=10 and x=265 (before `savePresetButton` at x=270). Phase 21 accepts this hole as intentional. Filling the gap or reflowing the toolbar is editor-layout work — fold into a later UI phase (potentially in v1.7 packaging, or post-v1.7).
- **Pluginval --strictness-level 10 as a hard gate.** Belongs in Phase 25.
- **auval -v as a hard gate on macOS.** Belongs in Phase 25.
- **lv2lint + sord_validate as hard gates on Linux.** Belongs in Phase 25.
- **VST3 SDK `validator` as a hard gate on every VST3 build.** Belongs in Phase 25.
- **Universal2 (arm64 + x86_64) macOS slice.** Phase 21 ships arm64-only from macos-14 for build-and-load proof; Phase 26 packaging adds the x86_64 slice via `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`.
- **Inno Setup / .pkg / .dmg installers.** Belongs in Phase 26.
- **`setLatencySamples()` and PDC null-test.** Belongs in Phase 22 (SRC + latency reporting).
- **`isBusesLayoutSupported` declaration.** Belongs in Phase 25 (channel buses + auval-on-Logic). Phase 21 leaves JUCE's default `BusesProperties().withInput("Input", stereo).withOutput("Output", stereo)` from the v1.6 standalone in place — this satisfies the smoke test ("loads in a host") without committing to the mono/mono + mono/stereo + stereo/stereo trio that PLUG-32 specifies.
- **Move WavSource bookkeeping out of PluginProcessor.** The wavSource struct, pendingSlots double-buffer, processBlock WAV-reading path, and load/start/stop methods stay in `src/plugin/PluginProcessor.cpp` for Phase 21 because moving them is a behavior-changing refactor that conflicts with Phase 22's introduction of host-buffer audio. The gate at the editor level is sufficient for PLUG-49 (UI invisible in plugins) and PLUG-51 (standalone unchanged). If Phase 22's planner finds it cleaner to remove this from the processor entirely once SRC + host audio is wired, that's their call.
- **Strictness-7 pluginval as a merge-blocking gate on Linux VST3.** Phase 21 includes it as `continue-on-error: true` advisory; Phase 25 flips it to a gate.
- **Re-evaluating clap-juce-extensions vs JUCE 9 native CLAP.** Per STACK-v1.7.md, JUCE 9 has no public release date. Re-check before Phase 26 ships; if JUCE 9 lands first and is verified working with the SPU-94 plugin surface, the shim can be removed in a single-commit swap. Not in this phase.
- **AU code collision audit against a community AU-code registry.** Done informally during planning (Spu9/Sp94 chosen as the codes); a formal cross-check belongs in Phase 25 when `auval` actually runs and would catch collisions empirically.
- **CI-side Load → Play → Stop round-trip on macOS and Windows standalones.** Phase 21's standalone smoke is construction-and-exit only; full round-trip is verified manually by the executor on Linux (their host OS) and exercised opportunistically when later phases (22+) run on macOS/Windows runners.
