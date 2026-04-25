# Phase 8: SPU-94 Standalone GUI (product v1.0) — Research

**Researched:** 2026-04-26
**Domain:** JUCE 8 standalone audio application, CMake integration, realtime audio thread / GUI thread parameter handoff, file-WAV playback pipeline, SPU register surface wiring
**Confidence:** HIGH on JUCE module/version/CMake mechanics; HIGH on existing libspu94 integration; MEDIUM on subjective interpolator quality choice (no JUCE-published benchmarks); LOW on JUCE 8.x Linux-build edge cases on Ubuntu 25.10 specifically (current libwebkit2gtk-4.1-dev present, but not yet exercised in repo)

## Summary

Phase 8 wraps the already-shipped `libspu94` (M1, tag `m1-reverb-core`) inside a single-window JUCE-built standalone application. The C core stays untouched — the JUCE side is glue: a WAV loader, a one-shot resampler, an audio callback that feeds the SPU sample-by-sample (or by short blocks) and crossfades the wet output against a dry copy, plus 18 register sliders + 10-preset dropdown + Wet/Dry knob. Existing toolchain (CMake 3.31, GCC 15.2, libasound, libjack/PipeWire-JACK, libfreetype, libxrandr/xinerama/cursor) already covers ~half the JUCE Linux dependency set; six packages need installing. CONTEXT.md locks the macro shape; the open implementation questions all collapse to "use the standard JUCE pattern" once the JUCE 8 docs and `examples/CMake/AudioPlugin/` reference project are read.

**Primary recommendation:** Pin **JUCE 8.0.12** (latest stable, December 2024) via **CMake `FetchContent`** with a pinned commit SHA into `vendor/JUCE/` (mirroring the existing `vendor/dr_wav/` pattern). Use `juce_add_plugin(... FORMATS Standalone ...)` (NOT `juce_add_gui_app`) so the future plugin-formats phase is a one-line edit. Use **JUCE built-in `AudioFormatManager` + `AudioFormatReader`** for WAV loading (NOT vendored dr_wav — JUCE built-in covers any-SR / any-bit-depth / mono-stereo / float-PCM in one call; dr_wav stays scoped to the existing CLI). Use **`WindowedSincInterpolator`** for the one-shot load-time resample (high quality, 100-sample latency is irrelevant since the resample happens once at load, not per audio block). Use **`std::atomic<int16_t>`** per-register for the GUI→audio-thread handoff and a **lock-free single-producer single-consumer (SPSC) command queue** for preset switches.

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01-A — Plugin format scope:** v1.0 ships **standalone only**. No VST3 / LV2 / CLAP / AU. Plugin formats are a separate future phase.
- **D-01 — Lever surface:** v1.0 ships **all 18 viable SPU registers as raw labeled sliders** (12 free-class + 6 sample-quantized). Slider labels = raw register names, NOT musical aliases. Named-lever curation deferred. The 17 catastrophic / preset-fixed `m*` registers are NOT exposed as sliders.
- **D-02 — Plugin-layer additions:** **Wet/Dry mix only.** No Pre-Delay, Input HPF, Freeze, LFO. Wet/Dry is the only DSP added outside `libspu94`.
- **D-03 — Audio I/O scope:** **WAV file load + realtime playback only.** No file save/export. No live audio input.
- **D-04 — UI direction:** **JUCE stock look-and-feel.** No custom skin, no painted backgrounds, no bespoke widgets.
- **D-05 — Preset selector UX:** **Flat 10-item dropdown.** No categories, no advanced disclosure.
- **D-06 — Sample rate / bit depth handling:** **Light JUCE-side I/O wrapper handles any-SR / any-bit-depth / mono-stereo WAV input** — bit-depth conversion to int16, sample-rate conversion to 44.1 kHz, mono→stereo channel adaptation. SPU core stays unchanged and bit-faithful.

### Claude's Discretion

- Slider layout / grouping on the panel (by register class, by signal-flow position, or flat)
- Knob/slider widget choice (rotary vs vertical-strip)
- Specific JUCE interpolator (LagrangeInterpolator vs CatmullRomInterpolator vs WindowedSincInterpolator)
- WAV reader: JUCE built-in `AudioFormatReader` vs vendored `dr_wav`
- JUCE version pin (7.x vs 8.x — both viable per CONTEXT)
- Specific JUCE module set imported
- Whether playback auto-starts on file load or requires a button press
- Whether to show numeric value next to each slider (recommended yes for debug)
- File picker UX (JUCE `FileChooser` with default path)

This research section recommends specific picks for every Claude's-discretion item with evidence.

### Deferred Ideas (OUT OF SCOPE)

- Named-Lever Curation (Room Size / Pre Delay / Damping / etc.) — follow-up phase informed by listening evidence Anthony gathers from the v1.0 tool
- Plugin Format Support (VST3 / LV2 / CLAP) — separate post-v1.0 phase
- Plugin-Layer DSP Extensions (true Pre-Delay buffer, Input HPF, Freeze, Tail-modulation LFO)
- WAV File Save / Export
- Live Audio Input (mic / line-in via JACK / PipeWire / ALSA)
- Custom UI / Visual Identity
- macOS / Windows builds
- License pick (MIT vs Apache-2.0)

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| STANDALONE-01 | Single-window JUCE standalone on Linux; no DAW / plugin host | `juce_add_plugin(... FORMATS Standalone ...)` produces a directly-launchable Linux executable; JUCE Standalone wrapper bootstraps `JUCEApplication` + `AudioDeviceManager` + `AudioProcessorPlayer` (see "Architecture Patterns / Standalone Wrapper Mechanics") |
| STANDALONE-02 | WAV file load via picker; any SR / any bit depth / mono-stereo; I/O wrapper normalizes to 44.1 kHz int16 stereo before SPU | `juce::FileChooser::launchAsync` for picker; `AudioFormatManager::registerBasicFormats()` registers WAV+AIFF readers covering 8/16/24/32-int and 32-float; `AudioFormatReader::read()` produces float samples; `WindowedSincInterpolator` resamples once at load time; manual mono→stereo duplicate; manual float→int16 with rounding for the SPU input feed |
| STANDALONE-03 | Realtime playback via libspu94 at 44.1 kHz int16 internally; no crashes / underruns / distortion | `AudioAppComponent::getNextAudioBlock` is the audio-thread callback; SPU's `spu94_process` is rt-safe per M1 (Phase 5 verified, see "Don't Hand-Roll" + "Common Pitfalls"); buffer sizes 256-1024 are JUCE defaults |
| STANDALONE-04 | 10 PS1 factory presets selectable via flat dropdown; each audibly correct vs M1 CLI; preset switch during playback works (audible discontinuity OK per ADR-0006) | `juce::ComboBox` populated from `spu94_presets[i].name` for `i in [0, SPU94_PRESET__COUNT)`; preset switch enqueues an SPSC command for the audio thread to call `spu94_load_preset` between audio blocks; mBASE snap-on-write per ADR-0006 produces the audible discontinuity (accepted); after preset load the GUI thread reads back register values via `spu94_get_reg_*` and updates 18 slider positions |
| STANDALONE-05 | 18 raw labeled register sliders (12 free + 6 sample-quantized); raw register names; smooth on free / stepped on sample-quantized | C++ standalone iterates `for (int i = 0; i < SPU94_REG__COUNT; ++i)` and filters via the cost classification table (12 + 6 = 18); slider min/max from `spu94_reg_type(i)` (i16: -32768..32767, u16: 0..65535); slider label = `spu94_reg_name(i)`; per-slider `std::atomic<int16_t>` shadow holds the GUI-set value, audio thread reads atomically and calls `spu94_set_reg_*` once per audio block before `spu94_process` |
| STANDALONE-06 | Wet/Dry knob blends dry input + SPU wet output; 0% Wet = unprocessed, 100% Wet = reverb only | SPU produces wet-only output (verified, see "Architecture Patterns / Wet/Dry"); the JUCE layer keeps a dry copy of the resampled int16 stereo input and equal-power-crossfades against the SPU output before sending to the audio device |
| STANDALONE-07 | JUCE stock look-and-feel | Default `LookAndFeel_V4` — do nothing |
| STANDALONE-08 | Builds reproducibly via existing root CMakeLists.txt extended with JUCE; libspu94 (`spu94_shared`) linked unmodified | Add `vendor/JUCE/` via FetchContent or git submodule; `add_subdirectory(JUCE)` at root level; new `src/standalone/CMakeLists.txt` with `juce_add_plugin(spu94_standalone FORMATS Standalone ...)`; `target_link_libraries(spu94_standalone PRIVATE spu94_shared juce::juce_audio_utils ...)` |
| STANDALONE-09 | App name / version / vendor metadata = "SPU-94" (NOT "PSX Reverb") | `juce_add_plugin` arguments: `PRODUCT_NAME "SPU-94"`, `COMPANY_NAME "SPU-94 Project"` (or similar — planner picks vendor string), `PLUGIN_NAME "SPU-94"`, `BUNDLE_ID "com.spu94.spu94"` (mac-only but harmless on Linux) |

## Project Constraints (from CLAUDE.md)

No project-level `./CLAUDE.md` exists at the repository root. The user's global instructions (`~/.claude/CLAUDE.md`) apply: **hands-on guided walkthroughs for deployed-system work**. Phase 8 plans should treat the developer (Anthony) as hands-and-eyes — present commands one at a time with explanation, do NOT batch sweeping installer scripts. This shapes the *delivery* of the plan, not the *technical content*. The technical recommendations in this RESEARCH.md remain unchanged.

Persistent project posture (carried forward from PROJECT.md / prior phase CONTEXTs):

- Plain C99 core stays unmodified (`libspu94` is linked, not forked)
- Linux primary
- Trademark posture: product name = "SPU-94", NOT "PSX Reverb" — applies to all user-facing strings in the JUCE app
- Paraphrase-not-transcribe: do NOT read GPL emulator sources (Mednafen / lv2-psx-reverb / DuckStation / MiSTer) as primary references
- JUCE official tutorials, JUCE docs (juce.com), JUCE forum, JUCE GitHub `examples/` directory are all fine to read and paraphrase as primary research material
- License pick (MIT vs Apache-2.0) explicitly NOT a Phase 8 concern

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| **JUCE** | **8.0.12** (released 2024-12-16) | C++ audio app framework — Standalone wrapper, audio device management, GUI components, audio I/O, file format readers, interpolators, atomic parameter primitives | The only realistic option for a single-codebase "standalone today, plugin formats tomorrow" Linux audio app. The exact target shape Anthony's CONTEXT.md describes (`juce_add_plugin(... FORMATS Standalone ...)` today, add `VST3 LV2 CLAP` later) is JUCE's native pattern. CMake-first integration since JUCE 6. [VERIFIED: GitHub releases page] |
| **libspu94** (`spu94_shared`) | M1 (tag `m1-reverb-core`, shipped 2026-04-25) | Bit-faithful PS1 SPU reverb DSP — the entire reason this app exists | Already shipped, already tested (82/82 ctest green). Phase 8 is a pure consumer of its public C surface. [VERIFIED: existing repo, `nm -D build/src/spu94/libspu94.so` shows all required symbols] |
| **CMake** | ≥ 3.22 | Build system — JUCE 8 minimum | Existing project requires 3.20; bumping to 3.22 is one-line and required by JUCE 8. Local install is 3.31.6 (well above). [VERIFIED: JUCE CMake API doc + local `cmake --version`] |
| **g++ / Clang** | g++ ≥ 7.0 OR Clang ≥ 6.0, C++17 minimum | C++ compiler | Local g++ is 15.2.0; well above. JUCE 8 requires C++17. Existing project uses C11 for the C core, so the standalone is the first C++ target. [VERIFIED: JUCE Linux Dependencies doc + local `g++ --version`] |

**Version verification (npm view equivalent — GitHub Releases API):**

```bash
gh api repos/juce-framework/JUCE/releases/latest --jq '.tag_name, .published_at'
```

Returns `8.0.12` published `2024-12-16` [VERIFIED: GitHub repo via WebFetch on 2026-04-26].

### Supporting (JUCE modules — these are JUCE-internal modules, all present in the JUCE 8.0.12 distribution; planner imports them via CMake `target_link_libraries`)

| Module | Purpose | When to Use |
|--------|---------|-------------|
| `juce::juce_audio_utils` | Convenience module that pulls in everything an audio app needs (audio_basics, audio_devices, audio_formats, audio_processors, gui_basics, gui_extra). The single-line shortcut for a plugin-or-standalone target. | Always. Default for `juce_add_plugin` audio targets. [VERIFIED: JUCE `examples/CMake/AudioPlugin/CMakeLists.txt` links exactly this] |
| `juce::juce_audio_basics` | `LinearInterpolator` / `LagrangeInterpolator` / `CatmullRomInterpolator` / `WindowedSincInterpolator`; basic AudioBuffer helpers; gain ramps for crossfade | Pulled transitively by `juce_audio_utils`. The interpolators live here. [VERIFIED: JUCE `modules/juce_audio_basics/utilities/juce_Interpolators.h`] |
| `juce::juce_audio_devices` | `AudioDeviceManager`, `AudioIODeviceType` (ALSA / JACK / CoreAudio / WASAPI), `AudioDeviceSelectorComponent` (the picker UI), `AudioProcessorPlayer` (the bridge that pumps an `AudioProcessor::processBlock` from device callbacks) | Pulled transitively. The audio I/O backbone. [VERIFIED: JUCE docs] |
| `juce::juce_audio_formats` | `AudioFormatManager`, `AudioFormatReader`, `WavAudioFormat`, `AiffAudioFormat`, `OggVorbisAudioFormat`, `FlacAudioFormat`. `registerBasicFormats()` registers WAV + AIFF; `registerFormat(new ...)` adds Ogg / FLAC if you want them. | Pulled transitively. WAV reading. [VERIFIED: JUCE docs / tutorial] |
| `juce::juce_gui_basics` | `Slider`, `ComboBox`, `Label`, `TextButton`, `LookAndFeel_V4` (the default JUCE 8 stock skin), `FileChooser` (file picker dialog), `Component` base class | Pulled transitively. The 18 sliders + 1 dropdown + Wet/Dry knob + Load button + transport buttons. [VERIFIED: JUCE docs] |
| `juce::juce_audio_plugin_client` | The Standalone wrapper (`StandalonePluginHolder`, `StandaloneFilterWindow`, `StandaloneFilterApp`) that turns an `AudioProcessor` into a runnable executable | Pulled in automatically when `juce_add_plugin(... FORMATS Standalone ...)` is used (NOT when `juce_add_gui_app` is used — different code path). [VERIFIED: JUCE `juce_audio_plugin_client/Standalone/juce_StandaloneFilterApp.cpp` + JUCE CMake API doc] |
| `juce::juce_recommended_config_flags` | Compiler flags JUCE recommends (NDEBUG management, etc.) | Always — `PUBLIC` link |
| `juce::juce_recommended_lto_flags` | Link-time optimization flags | Always — `PUBLIC` link |
| `juce::juce_recommended_warning_flags` | Warning flags JUCE recommends | Always — `PUBLIC` link. Note: Phase 6 CLI relaxed `-Wconversion` for `dr_wav.h`; planner may need similar relaxation if Phase 8 vendors any third-party headers (planning to vendor only JUCE itself, which JUCE's own warning flags already accommodate). [VERIFIED: JUCE example CMakeLists.txt patterns] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `juce_add_plugin(... FORMATS Standalone ...)` | `juce_add_gui_app` | `juce_add_gui_app` produces a smaller binary (no `juce_audio_plugin_client` wrapper layer; no `AudioProcessor` glue) and is structurally simpler — you write `JUCEApplication::initialise()` and own the lifecycle directly. **However**, it is NOT trivial to add VST3 / LV2 / CLAP later if you choose this path: those formats require an `AudioProcessor` shell, which means rewriting the audio path. **Pick `juce_add_plugin`** — the future plugin-formats phase becomes literally `FORMATS Standalone VST3 LV2 CLAP` (one line). The Standalone wrapper bootstraps `AudioDeviceManager` + `AudioProcessorPlayer` for free. [CITED: JUCE forum thread "Standalone Plugin Structure, APVTS, Parameter Attachments" + JUCE CMake API doc + `juce_StandaloneFilterApp.cpp`] |
| JUCE 8.0.12 | JUCE 7.0.12 | JUCE 7 is also viable per CONTEXT D-01 discretion. JUCE 8 brings: standardized cross-platform font metrics, faster Direct2D renderer (Windows-only — irrelevant here), Unicode improvements, animation framework, WebView foundation. **JUCE 8 also adds the `libwebkit2gtk-4.1-dev` dependency on Linux** (was `4.0` on JUCE 7) — but Ubuntu 25.10 (`questing`) ships `libwebkit2gtk-4.1-dev` natively, so this is not a blocker for Anthony. The web browser dep can be disabled with `JUCE_WEB_BROWSER=0` (planner SHOULD do this — Phase 8 doesn't render any web content). **Pick JUCE 8.0.12** — latest, actively maintained, CONTEXT explicitly leaves the choice open. [VERIFIED: JUCE BREAKING_CHANGES.md, GitHub releases, local `apt-cache madison libwebkit2gtk-4.1-dev`] |
| JUCE built-in `AudioFormatManager` for WAV | Vendored `dr_wav` (already in `vendor/dr_wav/` for the CLI) | dr_wav is excellent for the C-only CLI (zero deps). For the JUCE app, `AudioFormatManager` is the idiomatic JUCE pattern, handles WAV + AIFF + (optionally) Ogg / FLAC out of the box, returns float samples that compose with `WindowedSincInterpolator` and `AudioBuffer<float>` directly, and avoids two parallel WAV codepaths in the project. **Pick JUCE built-in.** Keep `dr_wav` scoped to CLI only (current state). [VERIFIED: JUCE tutorial "Build an audio player"] |
| `WindowedSincInterpolator` (load-time resample) | `LagrangeInterpolator` / `CatmullRomInterpolator` | All three are in `juce_audio_basics`. Latencies: WindowedSinc 100 samples, Lagrange 2 samples, CatmullRom 2 samples. **For Phase 8 the resample happens ONCE at file-load time, not per audio block** — latency is irrelevant, quality matters. WindowedSinc is the documented "high quality" pick. CPU cost is paid once on the GUI thread during file load (typically < 100ms for a multi-minute audio file at any source rate); CPU is also irrelevant. **Pick WindowedSincInterpolator.** [VERIFIED: JUCE `juce_Interpolators.h` documentation] |
| FetchContent for JUCE | git submodule into `vendor/JUCE/` | FetchContent is more modern (CMake ≥ 3.11), keeps the source tree clean, downloads on first configure. Submodule is more explicit and matches the existing `vendor/dr_wav/` pattern (single-file vendored). **Pick FetchContent** — JUCE is too large to vendor as source (~30MB), and FetchContent with `GIT_TAG <pinned SHA>` is the JUCE community's documented pattern. Pin the SHA, not the tag, for reproducibility. [CITED: CMake FetchContent docs + multiple JUCE forum threads] |
| Standard `AudioAppComponent` for audio | `AudioProcessor` + `AudioProcessorPlayer` (what the Standalone wrapper uses internally) | If you go `juce_add_plugin(... FORMATS Standalone ...)`, you write an `AudioProcessor`, NOT an `AudioAppComponent`. The Standalone wrapper bootstraps `AudioDeviceManager` and an `AudioProcessorPlayer` that calls your `AudioProcessor::processBlock` from the audio thread. **Pick `AudioProcessor`** — it's mandatory for the `juce_add_plugin` path and is the correct shape for the future plugin-formats phase. [VERIFIED: `juce_StandaloneFilterApp.cpp` source + JUCE CMake API doc] |

**Installation:**

The new `apt` packages required for JUCE 8 on top of what Anthony's Ubuntu 25.10 already has:

```bash
sudo apt update
sudo apt install \
    libasound2-dev \
    libjack-jackd2-dev \
    libcurl4-openssl-dev \
    libfontconfig1-dev \
    libwebkit2gtk-4.1-dev \
    libglu1-mesa-dev \
    mesa-common-dev
```

Already installed (verified via `dpkg-query`): `ladspa-sdk`, `libfreetype-dev`, `libx11-dev`, `libxcomposite-dev`, `libxcursor-dev`, `libxext-dev`, `libxinerama-dev`, `libxrandr-dev`, `libxrender-dev`. Plus the build toolchain (`cmake` 3.31.6, `g++` 15.2.0, `git` 2.51.0).

**Optional install:** `ninja-build` (currently absent — only `Makefiles` generator works without it; not blocking, but ninja is faster).

**JUCE dependency disablers** (`target_compile_definitions PUBLIC` on the standalone target):

```cmake
target_compile_definitions(spu94_standalone
    PUBLIC
        JUCE_WEB_BROWSER=0      # we don't render web content
        JUCE_USE_CURL=0         # we don't make network calls
        JUCE_DISPLAY_SPLASH_SCREEN=0  # AGPLv3 path requires no splash; commercial not pursued
)
```

`JUCE_WEB_BROWSER=0` is the recommended Linux workaround for the Phase-8-irrelevant `libwebkit2gtk` link [CITED: JUCE forum "Bug: CMake Linux tooling should not link plugins against libwebkit2gtk"]. The package still gets installed (JUCE module compilation needs the headers present), but the runtime link is dropped. This matches the JUCE `examples/CMake/AudioPlugin/CMakeLists.txt` pattern.

## Architecture Patterns

### Recommended Project Structure

```
PSX Reverb/
├── CMakeLists.txt                     # bump min version 3.20 → 3.22; add FetchContent for JUCE; add_subdirectory(src/standalone)
├── vendor/
│   ├── dr_wav/                        # unchanged — CLI only
│   └── JUCE/                          # NEW: FetchContent populates this on first configure
├── include/spu94/                     # unchanged — public C headers
├── src/
│   ├── spu94/                         # unchanged — C library
│   ├── cli/                           # unchanged — C CLI
│   └── standalone/                    # NEW
│       ├── CMakeLists.txt             # juce_add_plugin(spu94_standalone FORMATS Standalone ...)
│       ├── PluginProcessor.h/.cpp     # SPU94AudioProcessor : public juce::AudioProcessor
│       ├── PluginEditor.h/.cpp        # SPU94AudioProcessorEditor : public juce::AudioProcessorEditor
│       ├── WavLoader.h/.cpp           # I/O wrapper: WAV → 44.1 kHz int16 stereo
│       ├── RegisterPanel.h/.cpp       # 18 sliders, builds dynamically from spu94_reg_name iteration
│       ├── PresetSelector.h/.cpp      # ComboBox driven from spu94_presets[]
│       ├── WetDryMixer.h/.cpp         # equal-power crossfade dry vs wet
│       └── ParameterBridge.h/.cpp     # std::atomic<int16_t> per-register + SPSC preset queue
└── python/spu94/                      # unchanged — Python binding
```

The `src/standalone/` layout follows the existing one-concern-per-TU grain (Phase 2 onwards) — six small TUs instead of one monolithic `MainComponent.cpp`.

### Pattern 1: JUCE Standalone target via `juce_add_plugin`

**What:** `juce_add_plugin(... FORMATS Standalone ...)` produces a **standalone executable target** (NOT a library); the JUCE Standalone wrapper bootstraps `AudioDeviceManager` + `AudioProcessorPlayer`; you write `juce::AudioProcessor`-derived class and a `juce::AudioProcessorEditor`-derived class for the UI.

**When to use:** Always for Phase 8 (per recommendation above). Future phase changes `Standalone` to `Standalone VST3 LV2 CLAP` and the same `AudioProcessor` becomes the plugin shell.

**Example:**

```cmake
# Source: paraphrased from JUCE examples/CMake/AudioPlugin/CMakeLists.txt and JUCE CMake API doc
juce_add_plugin(spu94_standalone
    PRODUCT_NAME       "SPU-94"
    COMPANY_NAME       "SPU-94 Project"
    PLUGIN_NAME        "SPU-94"
    PLUGIN_MANUFACTURER_CODE  Spu9    # 4-char manufacturer code (planner picks; convention is camel-case)
    PLUGIN_CODE        Spv1            # 4-char plugin code (planner picks)
    FORMATS            Standalone      # one line — future phase appends VST3 LV2 CLAP
    BUNDLE_ID          "com.spu94.spu94"  # macOS-only field; harmless on Linux
    NEEDS_MIDI_INPUT   FALSE
    NEEDS_MIDI_OUTPUT  FALSE
)

target_sources(spu94_standalone
    PRIVATE
        src/standalone/PluginProcessor.cpp
        src/standalone/PluginEditor.cpp
        src/standalone/WavLoader.cpp
        src/standalone/RegisterPanel.cpp
        src/standalone/PresetSelector.cpp
        src/standalone/WetDryMixer.cpp
        src/standalone/ParameterBridge.cpp
)

target_compile_definitions(spu94_standalone
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(spu94_standalone
    PRIVATE
        spu94_shared                    # the existing M1 library, untouched
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(spu94_standalone PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
```

### Pattern 2: FetchContent integration of JUCE at root level

**What:** Pull JUCE source into the build tree at configure time, pinned to a specific SHA for reproducibility.

**When to use:** Once at the top of root `CMakeLists.txt` (before `add_subdirectory(src/standalone)`).

**Example:**

```cmake
# Source: paraphrased from CMake FetchContent docs + JUCE community FetchContent patterns

# JUCE 8.0.12 release SHA — pin the commit, not the tag, for byte-reproducibility.
# Resolve the SHA at planner time via:
#   gh api repos/juce-framework/JUCE/git/refs/tags/8.0.12 --jq '.object.sha'
# Then paste the 40-char SHA below.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        <40-char SHA — planner resolves at plan time>
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# JUCE is now available as juce::juce_audio_utils etc. for downstream targets.
```

### Pattern 3: AudioProcessor structure for the standalone shell

**What:** `juce::AudioProcessor`-derived class owns the SPU state; `prepareToPlay` initializes `spu94_init` + `spu94_load_preset`; `processBlock` reads atomic register shadows, calls `spu94_set_reg_*` for any changed registers, then `spu94_process` for the block, then crossfades wet vs dry.

**When to use:** This is the audio-thread entry point. Realtime-critical.

**Example:**

```cpp
// Source: paraphrased from JUCE AudioProcessor docs + libspu94 public API
class SPU94AudioProcessor : public juce::AudioProcessor {
public:
    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // Allocate SPU state and work buffer ONCE here, NOT in processBlock.
        // The buffers are caller-owned per libspu94's API contract.
        // SPU runs at 44.1 kHz internally; if the device sampleRate differs, that
        // is a load-time-resample concern (handled in WavLoader, not here — the
        // SPU output is always 44.1 kHz int16, and we resample THAT to device rate
        // if needed via a JUCE LinearInterpolator on the device callback path).
        // [Note: planner may simplify by requiring the device be at 44.1 kHz, or
        //  by adding a 44.1 → device-rate resampler at the output. Discretion.]

        state_buf.setSize(SPU94_STATE_SIZE_MAX);
        work_buf.setSize(SPU94_WORK_BUF_MAX_BYTES);
        spu = spu94_init(state_buf.getData(), SPU94_STATE_SIZE_MAX,
                         work_buf.getData(), SPU94_WORK_BUF_MAX_BYTES);
        spu94_reset(spu);
        spu94_load_preset(spu, SPU94_PRESET_HALL);   // sane default

        // Sync the 18 atomic shadows with the just-loaded preset values so the
        // GUI sliders show the right starting positions.
        bridge.syncShadowsFromSPU(spu);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        // 1. Drain SPSC command queue (preset-switch requests from GUI thread).
        bridge.drainPresetCommands(spu);  // calls spu94_load_preset if a switch is queued

        // 2. Push any GUI-changed register values to the SPU before processing.
        bridge.pushPendingRegisterWrites(spu);  // iterates 18 atomics; calls spu94_set_reg_*

        // 3. Pull the next int16 stereo block from the loaded WAV (already resampled
        //    to 44.1 kHz int16 stereo at file-load time).
        const int n = buffer.getNumSamples();
        int16_t L_in[N], R_in[N], L_out[N], R_out[N];   // small stack buffers, sized per JUCE max block
        wav_source.fillBlock(L_in, R_in, n);            // also handles end-of-file / loop / silence

        // 4. Run the SPU.
        spu94_process(spu, L_in, R_in, L_out, R_out, (uint32_t)n);

        // 5. Equal-power crossfade dry vs wet, write float to JUCE buffer.
        const float wet = wet_dry.load(std::memory_order_relaxed);    // [0..1]
        const float dry_gain = std::sqrt(1.0f - wet);
        const float wet_gain = std::sqrt(wet);
        auto* outL = buffer.getWritePointer(0);
        auto* outR = buffer.getWritePointer(1);
        for (int i = 0; i < n; ++i) {
            outL[i] = (L_in[i]  * dry_gain + L_out[i] * wet_gain) / 32768.0f;
            outR[i] = (R_in[i]  * dry_gain + R_out[i] * wet_gain) / 32768.0f;
        }
        // No allocations, no locks, no syscalls in the callback. (See Pitfalls.)
    }

    void releaseResources() override {
        spu94_destroy(spu);
        spu = nullptr;
    }

    // ... boilerplate AudioProcessor overrides: getName(), acceptsMidi(),
    //     producesMidi(), getTailLengthSeconds(), getNumPrograms(), etc.

private:
    juce::HeapBlock<unsigned char> state_buf;
    juce::HeapBlock<unsigned char> work_buf;
    spu94_state* spu = nullptr;
    ParameterBridge bridge;          // std::atomic<int16_t>[18] + SPSC preset queue
    std::atomic<float> wet_dry{0.5f};
    WavSource wav_source;
};
```

### Pattern 4: Engine-layer register iteration (mirrors Phase 6 Python binding)

**What:** Build the 18-slider list at runtime by iterating the SPU register enum, NOT by hardcoding 18 names in C++.

**When to use:** Once on UI construction. Mirrors the Phase 6 D-06 reflection pattern.

**Example:**

```cpp
// Source: paraphrased from libspu94 public API + Phase 6 06-CONTEXT.md D-06 pattern

// Cost classification of the 18 viable registers (CONTEXT D-01).
// This table is the single source of truth for "which registers are sliders".
// Could also be derived at runtime from a name-prefix check (v* + the six d* in
// the slider list), but explicit table is clearer for the planner to inspect.
constexpr std::array<spu94_reg_t, 18> kSliderRegisters = {
    // 12 free-class (v*-prefix gain registers)
    SPU94_REG_vLOUT, SPU94_REG_vROUT, SPU94_REG_vLIN, SPU94_REG_vRIN,
    SPU94_REG_vIIR, SPU94_REG_vWALL,
    SPU94_REG_vCOMB1, SPU94_REG_vCOMB2, SPU94_REG_vCOMB3, SPU94_REG_vCOMB4,
    SPU94_REG_vAPF1, SPU94_REG_vAPF2,
    // 6 sample-quantized (specific d*-prefix delay registers — NOT all 8 d*)
    SPU94_REG_dLSAME, SPU94_REG_dRSAME,
    SPU94_REG_dLDIFF, SPU94_REG_dRDIFF,
    SPU94_REG_dAPF1,  SPU94_REG_dAPF2,
};
static_assert(kSliderRegisters.size() == 18);

void RegisterPanel::buildSliders() {
    for (size_t i = 0; i < kSliderRegisters.size(); ++i) {
        const spu94_reg_t reg = kSliderRegisters[i];
        const char* name = spu94_reg_name(reg);   // e.g., "vIIR", "dCOMB1"
        const spu94_reg_type_t type = spu94_reg_type(reg);

        auto& s = sliders[i];
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        if (type == SPU94_REG_TYPE_I16) {
            s.setRange(-32768.0, 32767.0, 1.0);   // signed
        } else {
            s.setRange(0.0, 65535.0, 1.0);        // unsigned
        }
        s.setName(name);
        s.onValueChange = [this, i] {
            // GUI thread → atomic shadow → audio thread reads next block
            bridge.setRegisterShadow(i, (int16_t)sliders[i].getValue());
        };

        labels[i].setText(name, juce::dontSendNotification);
        addAndMakeVisible(s);
        addAndMakeVisible(labels[i]);
    }
}
```

### Pattern 5: GUI-thread → audio-thread parameter handoff (lock-free)

**What:** GUI slider write → `std::atomic<int16_t>::store(value, std::memory_order_release)`. Audio thread reads `std::atomic<int16_t>::load(std::memory_order_acquire)` once per audio block, compares against the last-applied value, calls `spu94_set_reg_*` only if changed.

**When to use:** Every register slider. Per-instance shadow.

**Example:**

```cpp
// Source: idiomatic JUCE pattern; std::atomic on int16_t is lock-free on x86_64
// (verifiable via std::atomic<int16_t>::is_always_lock_free).
// JUCE forum thread "AudioParameter thread safety" + JUCE docs confirm this is
// the correct approach when not using APVTS. APVTS itself uses std::atomic<float>
// internally but adds value-tree overhead we don't need for raw int16 register knobs.

class ParameterBridge {
public:
    void setRegisterShadow(size_t i, int16_t value) {
        // Called on the GUI/message thread when a slider moves.
        shadows[i].store(value, std::memory_order_release);
    }

    void pushPendingRegisterWrites(spu94_state* spu) {
        // Called on the audio thread once per block.
        // Compares each shadow to the last-applied value; calls set_reg_* only on change.
        // No lock, no allocation, no syscall.
        for (size_t i = 0; i < kSliderRegisters.size(); ++i) {
            const int16_t v = shadows[i].load(std::memory_order_acquire);
            if (v != last_applied[i]) {
                const spu94_reg_t reg = kSliderRegisters[i];
                if (spu94_reg_type(reg) == SPU94_REG_TYPE_I16) {
                    spu94_set_reg_i16(spu, reg, v);
                } else {
                    spu94_set_reg_u16(spu, reg, (uint16_t)v);
                }
                last_applied[i] = v;
            }
        }
    }

    void syncShadowsFromSPU(const spu94_state* spu) {
        // Called from prepareToPlay AND after a preset switch — pulls the SPU's
        // current register values back into the shadows so the GUI sliders display
        // the new positions. Non-realtime context; safe.
        for (size_t i = 0; i < kSliderRegisters.size(); ++i) {
            const spu94_reg_t reg = kSliderRegisters[i];
            int16_t v = (spu94_reg_type(reg) == SPU94_REG_TYPE_I16)
                ? spu94_get_reg_i16(spu, reg)
                : (int16_t)spu94_get_reg_u16(spu, reg);
            shadows[i].store(v, std::memory_order_release);
            last_applied[i] = v;
        }
    }

private:
    std::array<std::atomic<int16_t>, 18> shadows{};
    std::array<int16_t, 18> last_applied{};   // audio-thread-only
};
```

### Pattern 6: Preset switch SPSC command queue

**What:** GUI thread enqueues a single-int preset-id request; audio thread polls between blocks and calls `spu94_load_preset` if a request is pending; GUI thread later observes the load happened (e.g., via a back-channel atomic flag) and re-syncs the 18 slider positions.

**When to use:** Preset dropdown changes during playback.

**Example:**

```cpp
// Source: idiomatic SPSC pattern; juce::AbstractFifo is the standard JUCE building
// block, but for a single-int request a pair of atomics is enough.

class PresetCommandQueue {
public:
    void requestPreset(spu94_preset_id_t id) {
        // GUI thread.
        requested.store((int)id, std::memory_order_release);
        request_pending.store(true, std::memory_order_release);
    }

    bool drain(spu94_state* spu) {
        // Audio thread, called at the top of processBlock.
        if (!request_pending.load(std::memory_order_acquire)) return false;
        const auto id = (spu94_preset_id_t)requested.load(std::memory_order_acquire);
        const auto rc = spu94_load_preset(spu, id);
        request_pending.store(false, std::memory_order_release);
        if (rc == SPU94_OK) {
            applied_id.store((int)id, std::memory_order_release);
            applied_count.fetch_add(1, std::memory_order_release);
        }
        return rc == SPU94_OK;
    }

    int getAppliedCount() const {
        // GUI thread polls this on a Timer; when it changes, re-sync sliders.
        return applied_count.load(std::memory_order_acquire);
    }

    int getAppliedId() const {
        return applied_id.load(std::memory_order_acquire);
    }

private:
    std::atomic<int> requested{0};
    std::atomic<bool> request_pending{false};
    std::atomic<int> applied_id{0};
    std::atomic<int> applied_count{0};
};
```

GUI thread runs a `juce::Timer` (e.g., 30Hz) that checks `applied_count` for changes and calls `bridge.syncShadowsFromSPU(spu)` when one is observed; this updates the visible slider positions.

### Pattern 7: WAV load → 44.1 kHz int16 stereo I/O wrapper

**What:** Use `AudioFormatManager` to read any-SR / any-bit-depth / mono-stereo WAV (and AIFF, free) into a `juce::AudioBuffer<float>`. Then resample to 44.1 kHz with `WindowedSincInterpolator`. Then convert float → int16 with rounding-to-nearest. Then duplicate mono → stereo if needed. Final form: two `std::vector<int16_t>` (L, R) at 44.1 kHz, ready for `spu94_process` to chew on in blocks.

**When to use:** Once per file load, on the message thread (NOT the audio thread).

**Example:**

```cpp
// Source: paraphrased from JUCE AudioFormatReader / AudioFormatManager docs
// + JUCE Interpolators docs + JUCE tutorial "Build an audio player"

struct LoadedWav {
    std::vector<int16_t> L, R;   // 44.1 kHz int16 stereo, ready for SPU
    uint64_t num_frames{0};
};

std::optional<LoadedWav> WavLoader::load(const juce::File& f) {
    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();   // WAV + AIFF coverage on Linux

    auto reader = std::unique_ptr<juce::AudioFormatReader>(fmt.createReaderFor(f));
    if (!reader) return std::nullopt;

    // Read everything into a float buffer. Source SR + channels could be anything.
    const auto src_sr     = reader->sampleRate;
    const auto src_chans  = (int)reader->numChannels;
    const auto src_frames = (int64_t)reader->lengthInSamples;

    juce::AudioBuffer<float> src_buf(src_chans, (int)src_frames);
    reader->read(&src_buf, 0, (int)src_frames, 0, true, src_chans > 1);

    // Resample to 44.1 kHz if needed. WindowedSincInterpolator is the highest-
    // quality choice; latency 100 samples is irrelevant since we resample once.
    constexpr double kTargetSr = 44100.0;
    const double ratio = src_sr / kTargetSr;   // > 1 = downsample, < 1 = upsample
    const int64_t dst_frames = (int64_t)std::ceil(src_frames / ratio);

    juce::AudioBuffer<float> dst_buf(2, (int)dst_frames);   // always stereo
    dst_buf.clear();

    if (std::abs(ratio - 1.0) < 1e-9) {
        // No resample needed.
        if (src_chans == 1) {
            dst_buf.copyFrom(0, 0, src_buf, 0, 0, (int)src_frames);
            dst_buf.copyFrom(1, 0, src_buf, 0, 0, (int)src_frames);  // mono → duplicate
        } else {
            dst_buf.copyFrom(0, 0, src_buf, 0, 0, (int)src_frames);
            dst_buf.copyFrom(1, 0, src_buf, 1, 0, (int)src_frames);
        }
    } else {
        // Resample each channel independently (interpolators are stateful per channel).
        juce::WindowedSincInterpolator interpL, interpR;
        interpL.process(ratio, src_buf.getReadPointer(0),
                        dst_buf.getWritePointer(0), (int)dst_frames);
        if (src_chans == 1) {
            interpR.process(ratio, src_buf.getReadPointer(0),
                            dst_buf.getWritePointer(1), (int)dst_frames);
        } else {
            interpR.process(ratio, src_buf.getReadPointer(1),
                            dst_buf.getWritePointer(1), (int)dst_frames);
        }
    }

    // Convert float [-1, 1] to int16 [-32768, 32767] with rounding-to-nearest
    // and saturation (matches the SPU's input contract).
    LoadedWav out;
    out.num_frames = (uint64_t)dst_frames;
    out.L.resize((size_t)dst_frames);
    out.R.resize((size_t)dst_frames);
    auto floatToInt16 = [](float x) -> int16_t {
        const float scaled = std::clamp(x, -1.0f, 1.0f) * 32767.0f;
        const int v = (int)std::lround(scaled);
        return (int16_t)std::clamp(v, -32768, 32767);
    };
    const auto* srcL = dst_buf.getReadPointer(0);
    const auto* srcR = dst_buf.getReadPointer(1);
    for (int64_t i = 0; i < dst_frames; ++i) {
        out.L[(size_t)i] = floatToInt16(srcL[i]);
        out.R[(size_t)i] = floatToInt16(srcR[i]);
    }
    return out;
}
```

### Pattern 8: Equal-power Wet/Dry crossfade

**What:** Constant-power crossfade between dry input and wet output. Equal-power preserves perceived loudness across the sweep; linear loses ~3 dB at midpoint.

**When to use:** In `processBlock` after `spu94_process` produces wet samples (already shown in Pattern 3).

**Source confirmation:** SPU output is wet-only — verified via existing repo. The SPU's mix-bus produces only the reverb wet signal; there is no built-in dry path. Confirmed by reading `spu94_process` declaration and the existing CLI `main.c` which writes the SPU output directly to file as the "processed" output. The Wet/Dry mixer in the standalone is the **only** place the dry input ever survives to the speakers.

### Anti-Patterns to Avoid

- **Reading slider values directly from `juce::Slider::getValue()` inside `processBlock`.** Slider reads are NOT thread-safe from the audio thread. Always read from the atomic shadow. [CITED: JUCE forum "AudioParameter thread safety"]
- **Calling `spu94_load_preset` directly from the GUI thread.** It mutates 35 register slots — concurrent with `processBlock` reading them, this is a data race. Always go through the SPSC command queue. [Established by libspu94 thread-safety doc in `spu94.h` line 22-25: "A spu94_state is NOT thread-safe."]
- **Allocating in `processBlock` (JUCE buffer growth, std::vector push_back, std::string concat, FILE I/O, anything calling `malloc`).** Phase 5 of M1 wired up the `rt_safety/test_no_syscalls.sh` strace harness specifically to catch this. The Phase 8 standalone is a NEW callsite; the strace harness needs to be extended (or a parallel one stood up) to gate the JUCE app's audio thread the same way. [VERIFIED: existing repo `tests/rt_safety/`]
- **Using `juce::AudioProcessorValueTreeState` (APVTS) for the 18 register sliders.** APVTS is a heavyweight value-tree that internally uses `std::atomic<float>`; adapting raw int16 register knobs to it adds rounding error and complexity. Plain `std::atomic<int16_t>` per register is simpler, faster, and avoids float-int round-tripping. APVTS adds value when DAW automation / preset save-load via state-tree is needed; v1.0 has neither. (When the post-v1.0 plugin-formats phase lands, APVTS becomes valuable for DAW automation and that phase can introduce it.)
- **Calling `juce::FileChooser::browseForFileToOpen()` (modal) from a plugin context.** Modal file dialogs interfere with plugin hosts; even in standalone, the JUCE recommended pattern is `launchAsync()` with a callback. [CITED: JUCE FileChooser docs] For a v1.0 standalone-only build this is less critical, but `launchAsync` is also the only API that survives the future plugin-formats phase, so use it now.
- **Hardcoding the 18 register names in C++ as a parallel string list.** This duplicates Phase 2's `spu94_reg_name` table. Drift will silently happen. Iterate the SPU enum and call `spu94_reg_name(reg)` instead — same pattern Phase 6's Python binding uses for the IntEnum. [Established by Phase 6 06-CONTEXT.md D-06]
- **Resampling per audio block in the audio thread.** `WindowedSincInterpolator` allocates internal state and is too expensive for per-block use; even `LagrangeInterpolator` is wasteful here because the source WAV doesn't change after load. Resample ONCE at file-load time on the GUI thread; the audio thread sees only pre-resampled int16 stereo at 44.1 kHz. (Edge case: if the audio device runs at a non-44.1 kHz sample rate, the SPU's int16 output needs a 44.1 → device-rate convert at the end of `processBlock`. Planner's discretion: either constrain the device to 44.1 kHz via `AudioDeviceSetup`, OR add a `LinearInterpolator` (cheap, 1-sample latency) at the output. The latter is more flexible; the former is simpler. Recommend the **constrain-device** path for v1.0 — most audio interfaces do 44.1 kHz natively, and the v1.0 framing is "debug tool, not production playback".)

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV file decoding (any-SR / any-BD / mono-stereo) | A second WAV reader in the JUCE app, parallel to `dr_wav` | `juce::AudioFormatManager::registerBasicFormats()` + `AudioFormatReader` | Built into JUCE; covers WAV + AIFF + chunk variants + 8/16/24/32-int + 32-float in one call. No code to write, no edge cases to chase, no second codepath to maintain. [CITED: JUCE docs] |
| Sample-rate conversion | A custom resampler | `juce::WindowedSincInterpolator` (or `LagrangeInterpolator` if quality-vs-CPU preference shifts) | The interpolators in `juce_audio_basics` are battle-tested. Implementing windowed-sinc resampling yourself is a multi-week trap; even the Phase 4 39-tap half-band FIR took dedicated discussion + 2 plans. [VERIFIED: JUCE `juce_Interpolators.h`] |
| Audio device enumeration / picking (ALSA vs JACK on Linux) | Calling ALSA / JACK APIs directly | `juce::AudioDeviceManager` + `AudioDeviceSelectorComponent` | JUCE auto-detects ALSA + JACK on Linux. The picker UI is a single component you `addAndMakeVisible`. [CITED: JUCE docs] |
| Audio callback bootstrapping (`processBlock` running on the rt-priority audio thread) | A custom audio thread + ringbuffer | `juce::AudioProcessor::processBlock` (auto-pumped by `AudioProcessorPlayer` from the Standalone wrapper) | The Standalone wrapper does this for you. You only write `processBlock`. [VERIFIED: `juce_StandaloneFilterApp.cpp`] |
| File picker dialog | A custom dialog | `juce::FileChooser::launchAsync` | Native dialogs on each platform. [CITED: JUCE docs] |
| Preset dropdown widget | A custom combo box | `juce::ComboBox` | Standard JUCE 8 stock widget. |
| Numeric value display next to slider | A separate label updated on `onValueChange` | `juce::Slider::setTextBoxStyle(Slider::TextBoxRight, ...)` | JUCE `Slider` has a built-in text box that shows the current numeric value and accepts typed input. One line. |
| Application lifecycle (window creation, message loop, quit handling) | A custom `main()` | The Standalone wrapper's `juce::JUCEApplication`-derived `StandaloneFilterApp` | Generated automatically by `juce_add_plugin(... FORMATS Standalone ...)`. You don't write `main()`. [VERIFIED: `juce_StandaloneFilterApp.cpp`] |
| Sample-format conversion (8/16/24/32-int / 32-float → int16 with rounding + saturation) | Per-bit-depth code paths in the JUCE app | `AudioFormatReader::read()` returns float regardless of source bit depth — collapse to int16 once at the end with `std::lround` + `std::clamp` | The AudioFormatReader hides the bit-depth zoo. |

**Key insight:** This phase is almost entirely glue. The DSP (libspu94) is done. The framework (JUCE) is done. The plan is to wire them together in idiomatic JUCE patterns and resist the urge to be clever. Anywhere the code looks like it might benefit from a hand-rolled solution is almost certainly a place where JUCE's stock pattern is better.

## Runtime State Inventory

> Phase 8 is a greenfield phase (new code added; no rename / refactor / migration). However, several "what runtime state survives a code change?" categories are still relevant for the JUCE / libspu94 integration. Reporting per the standard format:

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — Phase 8 has no persistent stores. The standalone may write `~/.config/SPU-94/SPU-94.settings` (the JUCE `ApplicationProperties` default location for `AudioDeviceManager` XML state) once the app first runs and the user picks an audio device. This file is a JUCE-managed convenience; deletable / regeneratable; not a Phase 8 concern. | None — JUCE handles it |
| Live service config | None — Phase 8 has no external services | None |
| OS-registered state | None — Phase 8 does not register a systemd unit, a launchd plist, a Windows Task, or a desktop entry. (A `.desktop` file for the launcher menu is OUT OF SCOPE per CONTEXT D-04; v1.0 launches from a terminal or directly via the binary path.) | None |
| Secrets / env vars | None — Phase 8 reads no secrets, no env vars, no credentials. The audio device is picked via `AudioDeviceManager`, not env. | None |
| Build artifacts / installed packages | **`vendor/JUCE/`** is created on first `cmake -B build` via FetchContent. Subsequent rebuilds reuse it (~30MB on disk). To force re-fetch (e.g., after pinning a new SHA), planner removes `build/_deps/juce-src/` (not `vendor/JUCE/`; `vendor/JUCE/` is a tree-relative pattern; under FetchContent the actual location is `build/_deps/juce-src/`). | None during Phase 8 — clean rebuilds work. After SHA bump in a future phase, `rm -rf build/_deps/juce-*` |

**Nothing found in any other category:** None — verified by inspection of the existing project tree and Phase 8's pure-glue scope (no databases, no DBs, no external services, no OS daemons, no env-var secrets, no installed-system-state).

## Common Pitfalls

### Pitfall 1: Audio-thread allocations from JUCE buffer growth

**What goes wrong:** `juce::AudioBuffer<float>::setSize(numChannels, newSize)` allocates if `newSize > current capacity`. Calling this from `processBlock` is a hot-path heap call.

**Why it happens:** Easy to write `temp_buf.setSize(2, n)` at the top of `processBlock` for scratch space and not realize it allocates.

**How to avoid:** Allocate ALL buffers in `prepareToPlay`, sized to the largest block JUCE will hand you (the second arg of `prepareToPlay` is `samplesPerBlock` — that's the upper bound). In `processBlock`, only re-USE existing capacity; never resize. Stack arrays sized to a `constexpr kMaxBlock = 4096` work for the small per-block scratch buffers in Pattern 3.

**Warning signs:** `tests/rt_safety/test_no_syscalls.sh` extension runs the standalone with strace and any `mmap` / `brk` call between two `processBlock` invocations is a fail.

### Pitfall 2: Slider `onValueChange` callback firing on the wrong thread

**What goes wrong:** JUCE `Slider::onValueChange` callbacks fire on the **message thread** (GUI thread), not the audio thread. If you call `spu94_set_reg_*` directly from the callback, you race against `spu94_process` running on the audio thread. libspu94 explicitly states "A spu94_state is NOT thread-safe."

**Why it happens:** Looks innocent — slider moved, push the new value, done.

**How to avoid:** Always store to a `std::atomic<int16_t>` shadow in the callback (Pattern 5). Audio thread reads the atomic ONCE per block and pushes to the SPU.

**Warning signs:** Audible glitches / pops when twisting a slider during playback that don't appear in the same audio rendered offline via the CLI with the same parameter sequence.

### Pitfall 3: Preset switch races with audio thread

**What goes wrong:** GUI thread calls `spu94_load_preset(spu, id)` directly while audio thread is mid-`spu94_process`. `load_preset` mutates 35 registers; `process` is in the middle of reading them.

**Why it happens:** The "I'll just call the C function from the dropdown's `onChange` lambda" temptation.

**How to avoid:** SPSC command queue (Pattern 6). Drain on audio thread between blocks. JUCE forum threads explicitly warn that even `AudioProcessorPlayer::audioDeviceIOCallbackWithContext` acquires a lock; doing your own synchronization is the safe path.

**Warning signs:** Crashes during preset switching (UB from torn read of `pending_mask` or similar). Nondeterministic behavior across runs.

### Pitfall 4: JUCE 8 `libwebkit2gtk-4.1-dev` link error on Linux

**What goes wrong:** Out of the box, JUCE 8 links plugins (and `juce_add_plugin` Standalone targets) against `libwebkit2gtk-4.1.so` even when no web browser is used. On older Ubuntu / Debian (anything before Ubuntu 25.04 / Debian 13), this package was named `libwebkit2gtk-4.0-dev`, causing build failures or missing-library link errors.

**Why it happens:** JUCE's `juce_gui_extra` module compiles in WebBrowserComponent support unconditionally; the link line picks up the lib regardless of `NEEDS_WEB_BROWSER FALSE` on `juce_add_plugin` (which is a separate flag for plugin-host browser support).

**How to avoid:** `target_compile_definitions(spu94_standalone PUBLIC JUCE_WEB_BROWSER=0)` disables the link. Anthony's Ubuntu 25.10 has `libwebkit2gtk-4.1-dev` available natively (verified via `apt-cache madison`), so even without the disabler it should build — but the disabler is best practice and avoids dragging in an irrelevant dep.

**Warning signs:** Linker error mentioning `WebKitWebView` symbols, or apt-install failure for `libwebkit2gtk-4.1-dev`.

### Pitfall 5: `juce_add_plugin` Standalone target needs `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE`

**What goes wrong:** `juce_add_plugin` requires 4-character codes for `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE` — even when only Standalone format is being built. CMake configure fails without them.

**Why it happens:** These are AU plugin metadata fields. They're irrelevant for Linux Standalone but the CMake macro requires them.

**How to avoid:** Set them anyway: `PLUGIN_MANUFACTURER_CODE Spu9` and `PLUGIN_CODE Spv1` (or any 4-char strings — first letter usually uppercase per convention). They become AU metadata if/when AU is added later; for Standalone-only they're inert.

**Warning signs:** CMake error "JUCE: PLUGIN_MANUFACTURER_CODE must be set" at configure time.

### Pitfall 6: AGPLv3 splash screen on JUCE 8 Standalone

**What goes wrong:** By default, AGPLv3-licensed JUCE 8 builds display a splash screen at app startup ("Made with JUCE"). For the v1.0 personal-use Anthony tool this is harmless but cluttery. The splash CAN be disabled, but the disabler differs by license tier.

**Why it happens:** JUCE's "freemium" pattern — if you're under AGPLv3 you may legally remove the splash; if you're under the commercial license you may also; if you're under the older "GPLv3" personal-use exception (no longer offered for JUCE 8) you couldn't.

**How to avoid:** `JUCE_DISPLAY_SPLASH_SCREEN=0` in `target_compile_definitions`. AGPLv3 permits this. (Anthony's posture is AGPLv3 by default since he's not pursuing a commercial JUCE license; AGPLv3 obligations do not bind on personal-use-not-distributed code per the JUCE FAQ and AGPLv3 itself.)

**Warning signs:** "Made with JUCE" splash flashes at app startup.

### Pitfall 7: `WindowedSincInterpolator` requires per-channel state

**What goes wrong:** Sharing one `WindowedSincInterpolator` instance across L and R channels mixes their internal filter state, producing artifacts at channel boundaries.

**Why it happens:** All four JUCE interpolators are stateful (they keep an internal sample history for the filter delay line).

**How to avoid:** Construct one interpolator per channel (Pattern 7 shows two: `interpL`, `interpR`). Same applies to `LinearInterpolator` and the others.

**Warning signs:** Subtle stereo image drift, or audible artifacts only when the source WAV has different content per channel.

### Pitfall 8: Hardcoded device sample rate vs runtime device negotiation

**What goes wrong:** App assumes audio device is at 44.1 kHz, but the device picks 48 kHz at startup (most modern Linux audio interfaces default to 48 kHz). The SPU produces 44.1 kHz int16 stereo; if you write that directly to a 48 kHz output buffer, playback runs slow / pitch-shifted.

**Why it happens:** Linux audio device defaults vary. PipeWire often defaults to 48 kHz; ALSA hardware varies; JACK uses whatever the JACK server is configured for.

**How to avoid:** Two options (planner picks):
- **(A) Constrain device to 44.1 kHz** via `juce::AudioDeviceManager::AudioDeviceSetup setup; setup.sampleRate = 44100.0; deviceManager.setAudioDeviceSetup(setup, true);`. Simplest. If the device can't do 44.1 kHz, JUCE falls back gracefully and the user can pick another device via `AudioDeviceSelectorComponent`. Recommended for v1.0.
- **(B) Resample SPU output to device rate** via a `LinearInterpolator` (cheap, 1-sample latency) at the bottom of `processBlock`. More flexible; works with any device rate. More code.

Recommend (A) for v1.0 — debug-tool framing means a failed device negotiation is a "pick another device" prompt, not a robustness regression.

**Warning signs:** Pitch-shifted / wrong-tempo playback. `getSampleRate()` returns something other than 44100.0 in `prepareToPlay`.

### Pitfall 9: `processBlock` block size != 44.1 kHz alignment with the SPU's 22.05 kHz internal tick

**What goes wrong:** The SPU's `spu94_process` is documented to accept any block size ≥ 1 (per Phase 5 D-03). However, the SPU runs internally at 22.05 kHz with a 39-tap half-band FIR doing 44.1 ↔ 22.05 conversion at both boundaries. Odd-sample-count blocks that don't align with the FIR phase MAY produce subtly different output than even-count blocks — but per ADR-Phase-5-* + the existing `tests/unit/process/test_process_block_size.c` block-size invariance test, the API contract guarantees identical output regardless of block size. So this is NOT a real pitfall in M1 — but it's worth confirming the test still passes after Phase 8 wiring.

**Why it happens:** Theoretical concern based on FIR boundary behavior; the M1 tests verified it doesn't manifest.

**How to avoid:** Existing tests already cover this. Phase 8 inherits the guarantee for free.

**Warning signs:** N/A — this is documented as a non-issue. Listed here for completeness.

### Pitfall 10: Integer overflow in float-to-int16 conversion

**What goes wrong:** `(int)(f * 32768.0f)` on `f == 1.0f` produces `32768`, which overflows `int16_t`. Even a few samples of overflow produce loud clicks.

**Why it happens:** Off-by-one between the int16 positive max (32767) and the conventional float "full scale" of 1.0.

**How to avoid:** Pattern 7 shows `std::clamp(v, -32768, 32767)` after `std::lround(scaled)`. Multiply by 32767, not 32768, for the positive side; clamp on the negative side. Matches the existing CLI's approach.

**Warning signs:** Random clicks at sample boundaries on full-scale input. Particularly audible on impulse / square-wave inputs.

## Code Examples

(Verified patterns shown in "Architecture Patterns" section above. The eight patterns there cover all the common operations — `juce_add_plugin` setup, FetchContent, AudioProcessor lifecycle, register iteration, parameter handoff, preset SPSC, WAV load, and Wet/Dry crossfade. No additional snippets needed — the planner has a complete code-pattern library to draw from.)

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Projucer (`.jucer` XML) as primary project format | CMake-first integration via `juce_add_plugin` | JUCE 6 (2020), refined through JUCE 8 | CMake integration is now the documented "main" path. Projucer still exists for those who prefer it but is no longer the recommended starting point. JUCE 8 examples in `examples/CMake/` are the canonical reference. |
| `juce_add_plugin(... FORMATS VST3 Standalone ...)` was the same target type | Same — unchanged across JUCE 7 and 8 | n/a | The CMake API is stable on this front |
| `AudioFormat::createWriterFor()` had multiple overloads | Single virtual function takes `AudioFormatWriterOptions` | JUCE 8.0.9 (Sep 2024) | Phase 8 doesn't write WAV (no save/export per D-03) so this breaking change does NOT affect Phase 8. But if the deferred WAV-save phase ever lands, this is the API to use. |
| FRUT (jucer-to-cmake conversion tool) | Native JUCE CMake support | JUCE 6+ | FRUT is no longer needed; ignore it |
| GPLv3 personal-use exception for JUCE | AGPLv3 (since JUCE 8) | JUCE 8 release | AGPLv3 still permits personal-use-not-distributed builds without commercial license. Anthony's v1.0 posture matches this. The "personal use" exception language changed; the practical permission for hobbyist personal tools did not. |
| ALSA-only on Linux | ALSA + JACK auto-detected; PipeWire works through PipeWire-JACK shim (transparent to JUCE) | Long-stable; JACK support in JUCE since ~v3 | Anthony's system has both libjack (PipeWire shim) AND raw ALSA available; JUCE will detect both and let the user pick via `AudioDeviceSelectorComponent`. |

**Deprecated/outdated:**
- **Projucer for new CMake-driven projects** — Projucer still works and is fine for tweaking module configurations; it's just not the recommended starting point for this kind of project anymore.
- **JUCE 6 / 5 / 4 examples online** — pattern names match but specific class APIs have evolved; prefer `juce-framework/JUCE` `master` branch examples over older blog posts.
- **`AudioAppComponent` for new audio apps that may grow plugin formats** — works for "today only standalone, never plugin", but for "today standalone, plugin formats later" the `AudioProcessor` path is correct. (CONTEXT explicitly anticipates plugin formats later.)
- **`drwav` as a direct dependency of new JUCE code** — the existing CLI uses it correctly, but the JUCE app should use the built-in `AudioFormatManager`.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The Phase 8 plan does not need a `.desktop` file or system-tray launcher; the user launches the app via the built binary path. | Runtime State Inventory | LOW — adding a `.desktop` is trivial and can be a follow-up task if needed; not a re-architecture |
| A2 | Anthony's audio interface can run at 44.1 kHz natively. (Most can. PipeWire on his Ubuntu Studio install can be configured for any common rate.) | Pitfall 8 | MEDIUM — if false, planner picks resampling option (B) instead of constrain-device option (A). Both are documented; the choice is visible. |
| A3 | The `WindowedSincInterpolator`'s 100-sample latency is acceptable for one-shot file-load resampling. | Standard Stack alternatives | LOW — the resample happens on the GUI thread during file load, not in the audio path; latency does not contribute to playback latency |
| A4 | Wet/Dry equal-power crossfade (square-root-of-pan-law) is preferred over linear crossfade. | Pattern 8 | LOW — equal-power is the broadcast-engineer-default; linear is acceptable but loses ~3dB at midpoint. Anthony as a recording/broadcast engineer will recognize equal-power instinctively. If he prefers linear after using v1.0, it's a one-line change. |
| A5 | The post-v1.0 plugin-formats phase justifies picking `juce_add_plugin` over `juce_add_gui_app` today. | Standard Stack alternatives | LOW — even if plugin formats never land, `juce_add_plugin(... FORMATS Standalone ...)` produces a working standalone app. The cost of the choice is ~5% larger binary and a slightly more elaborate code structure (AudioProcessor + Editor instead of MainComponent). |
| A6 | The 18-register cost classification matches CONTEXT D-01 verbatim (12 free + 6 sample-quantized). | Pattern 4 | NONE — verified directly via `grep` against `docs/LEVERS-CATALOG.md`: 12 free, 6 sample-quantized, 17 catastrophic = 35 total; matches |
| A7 | Anthony's Ubuntu 25.10 install can `apt install` the missing JUCE deps without external repos. | Standard Stack installation | NONE — verified via `apt-cache madison` for `libwebkit2gtk-4.1-dev` (available in `questing/main`); other missing packages (`libasound2-dev`, `libjack-jackd2-dev`, `libcurl4-openssl-dev`, `libfontconfig1-dev`, `libglu1-mesa-dev`, `mesa-common-dev`) are all in standard Ubuntu main |
| A8 | JUCE 8.0.12 is the latest stable as of Phase 8 planning (Apr 2026). | Standard Stack | NONE — verified via `gh api repos/juce-framework/JUCE/releases/latest` yields tag `8.0.12` published `2024-12-16`; no newer stable released between Dec 2024 and Apr 2026 per GitHub releases page |
| A9 | The Standalone wrapper's `AudioDeviceManager` will auto-pick a sane default device (system default output) without user intervention if no settings file exists. | Pattern 3 | LOW — JUCE behavior is documented as "attempt to use the default audio device unless overridden"; verified in JUCE docs. If false, planner adds an `AudioDeviceSelectorComponent` to the UI as the load-screen fallback. |
| A10 | The strace-based `tests/rt_safety/test_no_syscalls.sh` from Phase 5 can be extended to validate the JUCE app's audio thread doesn't allocate. | Validation Architecture | MEDIUM — strace works on any Linux process; the technique transfers. The mechanics are: launch the standalone with strace, send playback for N seconds, parse strace output for any `mmap`/`brk` between two audio-callback markers. Planner may need to add a marker mechanism (e.g., a `SPU94_PRINTF_MARKER` env var that triggers a single `write()` per block, easily filterable). Absolute mechanism is planner discretion; the principle is sound. |

**If this table is empty:** N/A — there are 10 assumptions, mostly LOW risk; A2 is MEDIUM and worth flagging at planning time.

## Open Questions

1. **Should the standalone auto-start playback on file load, or require an explicit Play button press?**
   - What we know: CONTEXT marks this as Claude's discretion; user has not stated a preference.
   - What's unclear: A "drag-and-hear" workflow is faster for A/B testing; an explicit Play button is more conventional and predictable.
   - Recommendation: **Require explicit Play button**. Conventional audio-engineer expectation. Auto-start can be added later as a Preferences toggle if Anthony asks for it.

2. **How does the standalone handle the audio device's sample rate when it's not 44.1 kHz?**
   - What we know: Most Linux interfaces default to 48 kHz; SPU produces 44.1 kHz int16 stereo.
   - What's unclear: Per Pitfall 8, two options: constrain device to 44.1 kHz, or resample at output. Planner picks.
   - Recommendation: **Constrain device to 44.1 kHz** for v1.0. Simpler; matches debug-tool framing.

3. **What's the file picker's default starting directory?**
   - What we know: `juce::FileChooser` accepts an initial location.
   - What's unclear: User's Music folder? Last-used location? Project root?
   - Recommendation: **`~/Music` if it exists, else `~`**. Last-used location requires persisting state to a settings file — fine to add but not blocking.

4. **Do the 18 sliders show numeric values next to them?**
   - What we know: CONTEXT recommends yes for debug clarity; planner's call.
   - Recommendation: **Yes, via `juce::Slider::setTextBoxStyle(TextBoxRight, false, 60, 20)`**. Free with JUCE Slider; matches debug-tool framing.

5. **Does the Wet/Dry knob default to 100% Wet, 50/50, or something else?**
   - What we know: Anthony explicitly wants Wet/Dry to A/B against dry.
   - Recommendation: **Default 50/50** (equal-power midpoint). This puts the user "in the mix" immediately on first play — they can twist toward fully-wet or fully-dry as needed.

6. **Should the standalone loop the WAV when playback reaches the end, or stop?**
   - What we know: CONTEXT does not specify; this is Claude's discretion.
   - Recommendation: **Loop by default**. A/B testing with sliders requires continuous audio; manual restart between every twist is friction. Add a Loop toggle later if Anthony wants one-shot playback.

7. **Tail-time handling at end-of-loop / end-of-file: does the SPU keep its state across loop boundaries, or reset?**
   - What we know: `spu94_flush(spu, ..., n)` drains the tail by feeding internal silence (M1 Phase 5 contract). Loop = no flush; reset between loops would lose the running reverb state.
   - Recommendation: **Don't flush between loops; let the reverb state run continuously as if the WAV repeats forever.** This is musically natural — reverb tail from the end of the WAV blends into the start of the next iteration. If Anthony wants a clean break between loops, he can press Stop and Play again.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build system (≥ 3.22 for JUCE 8) | YES | 3.31.6 (well above min) | — |
| g++ | C++17 compilation | YES | 15.2.0 (well above min g++ 7.0) | — |
| git | FetchContent of JUCE | YES | 2.51.0 | — |
| libasound2-dev | JUCE ALSA backend | NO | — | install via `apt` |
| libjack-jackd2-dev | JUCE JACK backend (PipeWire-compatible) | NO | — | install via `apt` |
| ladspa-sdk | JUCE optional plugin host (off here) | YES | (installed) | — / could disable |
| libcurl4-openssl-dev | JUCE optional curl (we disable via `JUCE_USE_CURL=0` but headers still need to be present at compile time) | NO | — | install via `apt` |
| libfreetype-dev | JUCE font rendering | YES | (installed) | — |
| libfontconfig1-dev | JUCE font config | NO | — | install via `apt` |
| libx11-dev | JUCE X11 windowing | YES | (installed) | — |
| libxcomposite-dev | JUCE compositing | YES | (installed) | — |
| libxcursor-dev | JUCE cursor mgmt | YES | (installed) | — |
| libxext-dev | JUCE X11 ext | YES | (installed) | — |
| libxinerama-dev | JUCE multi-monitor | YES | (installed) | — |
| libxrandr-dev | JUCE display res | YES | (installed) | — |
| libxrender-dev | JUCE rendering | YES | (installed) | — |
| libwebkit2gtk-4.1-dev | JUCE web browser (we disable via `JUCE_WEB_BROWSER=0` but headers still need to be present at compile time on JUCE 8) | NO | — | install via `apt` (Ubuntu 25.10 has 2.50.4 in `questing-updates`) |
| libglu1-mesa-dev | JUCE OpenGL utility | NO | — | install via `apt` |
| mesa-common-dev | JUCE OpenGL common | NO | — | install via `apt` |
| ALSA runtime | Audio output backend | YES | (system) | — |
| JACK / PipeWire-JACK runtime | Alternative audio output | YES | PipeWire-JACK (system) | — |
| ninja-build | Faster CMake generator | NO | — | Falls back to Makefiles (slower but works) |
| `gh` CLI | Resolving JUCE 8.0.12 SHA at plan time | UNKNOWN — not checked | — | Manual `git ls-remote https://github.com/juce-framework/JUCE.git refs/tags/8.0.12` |

**Missing dependencies with no fallback:** None — all missing packages are in `apt` standard repos.

**Missing dependencies with fallback:** Six `-dev` packages must be `apt install`-ed before the first build. Single command:

```bash
sudo apt install libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
                 libfontconfig1-dev libwebkit2gtk-4.1-dev libglu1-mesa-dev \
                 mesa-common-dev
```

This is the planner's first concrete task: install JUCE Linux build deps (one terminal command, hands-on walkthrough per Anthony's preference per global CLAUDE.md).

`ninja-build` install is optional but recommended. Ninja produces faster builds and clearer parallel-job output.

## Validation Architecture

> Phase 8 includes the Validation Architecture section because `workflow.nyquist_validation` is `true` in `.planning/config.json`.

### Test Framework

| Property | Value |
|----------|-------|
| Framework | (Pre-existing) **Unity** for C unit tests + **pytest** for Python tests + **ctest** as the umbrella runner; Phase 8 also introduces the JUCE-app testing surface. Recommended approach: lightweight **JUCE `UnitTest` framework** (built into `juce_core`, requires no extra deps, integrates with ctest via a tiny test runner binary) for the JUCE-side glue; **strace-extension** (`tests/rt_safety/`-style shell scripts) for the audio-thread allocation gate. |
| Config file | Existing root `tests/CMakeLists.txt` adds a new `tests/standalone/` subdirectory |
| Quick run command | `ctest --test-dir build -R "^standalone_" --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` (existing 82+ tests + new Phase 8 tests) |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| STANDALONE-01 | Standalone binary launches, single-window, no DAW required | smoke (manual launch confirmation) | `./build/src/standalone/SPU-94_artefacts/Standalone/SPU-94 --version` (assuming JUCE Standalone produces a `--version`-aware binary; if not, simply running the binary and observing window appearance) | ❌ Wave 0 |
| STANDALONE-02 | WAV load: any-SR / any-BD / mono-stereo → 44.1 kHz int16 stereo | unit (golden-file round-trip) | `ctest -R "^standalone_wav_loader_unit$"` | ❌ Wave 0 |
| STANDALONE-03a | Audio callback rt-safety (no allocations in `processBlock`) | rt-safety (strace) | `tests/rt_safety/test_no_syscalls_standalone.sh` | ❌ Wave 0 — extends existing `tests/rt_safety/` pattern |
| STANDALONE-03b | Real-time playback bit-exactness vs CLI | golden (audio compare) | `pytest tests/standalone/test_playback_matches_cli.py` — render N WAVs through both standalone (offline mode) and CLI; SHA-256 compare outputs | ❌ Wave 0 — new |
| STANDALONE-04a | All 10 presets selectable from dropdown | unit (combo-box population check) | JUCE UnitTest: assert `combo.getNumItems() == SPU94_PRESET__COUNT && combo.getItemText(i) == spu94_presets[i].name` | ❌ Wave 0 |
| STANDALONE-04b | Each preset audibly correct vs CLI | golden (subset of STANDALONE-03b) | covered by STANDALONE-03b | ❌ Wave 0 |
| STANDALONE-04c | Preset switch during playback works without crash | smoke (driven test — programmatically switch preset 100x while audio thread runs) | `pytest tests/standalone/test_preset_switch_robustness.py` | ❌ Wave 0 |
| STANDALONE-05a | 18 sliders present with raw register names | unit (UI structure assertion) | JUCE UnitTest: assert slider count == 18 and labels match `spu94_reg_name(reg)` for each `reg` in cost table | ❌ Wave 0 |
| STANDALONE-05b | Free-class smoothness vs sample-quantized stepping | manual UAT (subjective listening) | manual-only — deferred to Phase 8 verify-work UAT checklist | manual-only |
| STANDALONE-06a | Wet/Dry knob blends correctly: 0% wet = identity, 100% wet = SPU output | unit (audio compare with knob at extremes) | `pytest tests/standalone/test_wet_dry_extremes.py` — render with knob at 0.0, expect output bit-equal to dry input; with knob at 1.0, expect output bit-equal to SPU output (less ~equal-power scaling) | ❌ Wave 0 |
| STANDALONE-06b | Smooth equal-power transition (subjective) | manual UAT | manual-only | manual-only |
| STANDALONE-07 | JUCE stock look-and-feel | manual UAT (visual inspection) | manual-only | manual-only |
| STANDALONE-08 | Builds reproducibly via root CMake | build (CI step) | `cmake -B build -G "Unix Makefiles" && cmake --build build --target spu94_standalone` (return code = pass) | ❌ Wave 0 — extends existing root CMake test |
| STANDALONE-09 | App metadata reads "SPU-94" not "PSX Reverb" | unit (binary-string check) | `strings build/src/standalone/SPU-94_artefacts/Standalone/SPU-94 \| grep -E "SPU-94" && ! strings ... \| grep -i "psx reverb"` | ❌ Wave 0 — small shell test |

**Per task commit:** `ctest --test-dir build -R "^standalone_" --output-on-failure` — runs only the new Phase 8 tests. Fast iteration during development.

**Per wave merge:** `ctest --test-dir build --output-on-failure` — runs all 82+ existing tests plus the new Phase 8 ones. Catches regressions in libspu94 caused by build-system changes.

**Phase gate:** Full ctest green AND manual UAT checklist (sample listening test of 5 of the 10 presets through a known WAV; subjective slider-twist confirmation; visual inspection of JUCE stock look-and-feel; preset switch during playback) before `/gsd-verify-work`.

### Wave 0 Gaps

- [ ] `tests/standalone/CMakeLists.txt` — wires the new test targets into ctest
- [ ] `tests/standalone/test_wav_loader_unit.cpp` — JUCE UnitTest: load each of N synthesized fixture WAVs (8/16/24/32-int and 32-float; mono and stereo; 8/22.05/44.1/48/96 kHz), assert output length is `ceil(src_frames * 44100 / src_sr)`, output is int16 stereo, peak amplitude is preserved within 1 LSB
- [ ] `tests/standalone/test_register_panel_structure.cpp` — JUCE UnitTest: instantiate RegisterPanel (without GUI shown, just construction), assert 18 sliders present, each with raw register-name label, each with correct min/max for its signedness
- [ ] `tests/standalone/test_playback_matches_cli.py` — pytest: for each (WAV, preset), render via Standalone (in offline / non-realtime mode if JUCE permits, else via headless playback through a virtual audio device) and via CLI; SHA-256 compare. Asserts the standalone is a faithful realtime presentation of the CLI's offline render.
- [ ] `tests/standalone/test_preset_switch_robustness.py` — pytest: drive preset switches 100x rapidly while audio thread is producing; assert no crash, no NaN, no out-of-bound output
- [ ] `tests/standalone/test_wet_dry_extremes.py` — pytest: render WAV at Wet/Dry = 0.0 and 1.0; assert 0.0 output ≈ input (within equal-power scaling), 1.0 output = SPU output
- [ ] `tests/rt_safety/test_no_syscalls_standalone.sh` — extends existing strace pattern: launch standalone for N seconds with a fixture WAV, capture strace, assert no `mmap` / `brk` between audio-callback markers
- [ ] `tests/standalone/test_metadata_strings.sh` — small shell script: assert `strings` of binary contains "SPU-94" and does NOT contain "PSX Reverb" (case-insensitive)

**Headless / offline-render testing note:** Several of the above tests (playback bit-exactness, wet/dry extremes) work best if the standalone has an "offline render" mode — i.e., a CLI flag `--render input.wav output.wav --preset hall` that bypasses the audio device and JUCE's realtime callback, runs the same `processBlock` path through a synthetic audio loop, and writes WAV. This is **NOT** a STANDALONE-* requirement (D-03 explicitly excludes file save/export from the user-facing v1.0). However, an internal-only `--test-render` flag that is hidden from the user-facing `--help` makes the validation tests dramatically simpler. Planner discretion.

If planner chooses NOT to add a test-render mode, fall back to: validate the underlying `WavLoader` + `ParameterBridge` + Wet/Dry mixer in isolation via JUCE UnitTests, and do the end-to-end playback bit-exactness check manually as part of the UAT checklist.

## Security Domain

> Required because `workflow.security_enforcement` is not set to `false` in `.planning/config.json` (treated as enabled by default).

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | NO | Standalone tool with no users / accounts / login |
| V3 Session Management | NO | No sessions |
| V4 Access Control | NO | Local-only single-user tool; no auth boundaries |
| V5 Input Validation | YES | WAV file input is the only untrusted input. JUCE's `AudioFormatReader` has battle-tested header parsing; malicious / malformed WAVs produce a NULL reader, not memory corruption. JUCE built-in is the standard control. |
| V6 Cryptography | NO | No crypto operations; no secrets |
| V7 Error Handling | YES | Single-line user-facing error messages on file load failure (mirrors the CLI's D-05 contract). NO stack traces shown to users. |
| V8 Data Protection | NO | No sensitive data; output goes to local audio device |
| V9 Communication Security | NO | No network |
| V10 Malicious Code | NO | No external code execution |
| V11 Business Logic | NO | n/a |
| V12 File and Resource | YES | File picker bounded to user-readable WAV files. JUCE `FileChooser` handles platform-native security (sandbox prompts on macOS, etc.). On Linux it just opens whatever the user has read-permission for. No symlink / path-traversal concerns since the app does not write files. |
| V13 API and Web Service | NO | n/a |
| V14 Configuration | YES | JUCE settings file (`~/.config/SPU-94/SPU-94.settings`) stores audio device choice as XML. Read by JUCE only. Tampering with it could potentially crash the app at startup; mitigation = JUCE's existing settings-file parsing has hardening. |

### Known Threat Patterns for {JUCE C++ standalone audio app}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed WAV header (oversized chunk lengths, integer overflows, recursive list chunks) | Tampering / DoS | Use JUCE `AudioFormatReader` — its WAV parser is hardened. Do NOT roll a custom WAV parser for the JUCE app. |
| Path traversal via crafted file picker selection | n/a | `juce::FileChooser` returns file paths; we only call `AudioFormatReader::createReaderFor()` on them; no path-mangling, no exec |
| Audio buffer overflow (writing past the JUCE-supplied output buffer) | Tampering | JUCE's `AudioBuffer<float>` getWritePointer() + getNumSamples() defines the bound; hot-path code asserts `i < n`. Static analysis (existing `cppcheck`/`clang-tidy` CI jobs) covers this. |
| Format-string injection via user-controlled metadata strings | Tampering | None of the user-displayed strings come from the WAV file's metadata; only the file path and a "loaded N frames at SR Hz" status line. No `printf` of WAV-supplied content. |
| Race conditions on `spu94_state` between audio and GUI threads | Tampering / DoS | The atomic-shadow + SPSC-queue patterns (Patterns 5 + 6) are the mitigation. Verified via the rt-safety test suite. |
| Heap exhaustion via huge WAV file | DoS | `WavLoader::load()` performs a `vector::resize(num_frames)` on the GUI thread. If a malicious user picks a 100GB WAV, this OOMs the process. Acceptable for a local single-user tool — same risk profile as the CLI, which already has the same exposure via `dr_wav`. |

No additional security hardening beyond JUCE's built-in patterns is required for v1.0. The threat model is "user picks a file from their own disk" — same as any audio app.

## Sources

### Primary (HIGH confidence)

- **JUCE GitHub master branch** — `https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md` — full CMake API reference; verified `juce_add_plugin` Standalone format produces an executable target
- **JUCE Linux Dependencies doc** — `https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md` — exact `apt install` command for Linux deps
- **JUCE BREAKING_CHANGES.md** — `https://github.com/juce-framework/JUCE/blob/master/BREAKING_CHANGES.md` — JUCE 7 → 8 migration notes; confirms no breaking change affecting Phase 8's CMake / AudioProcessor / Slider / ComboBox / AudioFormatManager paths
- **JUCE example AudioPlugin CMakeLists.txt** — `https://github.com/juce-framework/JUCE/blob/master/examples/CMake/AudioPlugin/CMakeLists.txt` — verbatim canonical pattern for `juce_add_plugin`
- **JUCE example GuiApp CMakeLists.txt** — `https://github.com/juce-framework/JUCE/blob/master/examples/CMake/GuiApp/CMakeLists.txt` — alternative `juce_add_gui_app` pattern (not chosen)
- **JUCE Interpolators source** — `https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_basics/utilities/juce_Interpolators.h` — documented latencies: WindowedSinc 100, Lagrange 2, CatmullRom 2, Linear 1; verified module location
- **JUCE Standalone Filter App source** — `https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterApp.cpp` — confirms what `juce_add_plugin(... FORMATS Standalone ...)` produces internally (a `JUCEApplication` + `StandalonePluginHolder` + `AudioDeviceManager` + `AudioProcessorPlayer` stack)
- **JUCE GitHub Releases page** — `https://github.com/juce-framework/JUCE/releases` — JUCE 8.0.12 published 2024-12-16, latest stable
- **JUCE tutorial: Build an audio player** — `https://juce.com/tutorials/tutorial_playing_sound_files` — `AudioFormatManager` + `AudioFormatReader` + `AudioFormatReaderSource` + `AudioTransportSource` chain
- **Existing libspu94 source** — `include/spu94/spu94.h`, `include/spu94/spu94_registers.h`, `include/spu94/spu94_register_facade.h` — the C public surface this phase wraps; verified directly
- **Existing root `CMakeLists.txt` + `src/cli/CMakeLists.txt` + `src/spu94/CMakeLists.txt`** — established patterns for build integration
- **Existing `docs/LEVERS-CATALOG.md`** — modulation cost classifications: 12 free + 6 sample-quantized + 17 catastrophic = 35; verified count
- **CONTEXT.md** — `.planning/phases/08-m4-juce-plugin-product-v1-0/08-CONTEXT.md` — locked decisions D-01..D-06, D-01-A
- **Phase 6 CONTEXT.md** — `.planning/phases/06-python-binding-cli/06-CONTEXT.md` — D-06 runtime reflection pattern; engine-layer iteration approach Phase 8 mirrors C++-side
- **Phase 7 CONTEXT.md** — `.planning/phases/07-verification-golden-files-witness-diff-modulation/07-CONTEXT.md` — D-16/D-17 modulation cost classification origin

### Secondary (MEDIUM confidence — verified against primary sources)

- JUCE forum thread "Standalone Plugin Structure, APVTS, Parameter Attachments" — `https://forum.juce.com/t/standalone-plugin-structure-apvts-parameter-attachments/56993` — discusses `juce_add_plugin` vs `juce_add_gui_app` tradeoffs; cross-checked against CMake API doc
- JUCE forum thread "Bug: CMake Linux tooling should not link plugins against libwebkit2gtk" — `https://forum.juce.com/t/bug-cmake-linux-tooling-should-not-link-plugins-against-libwebkit2gtk/63870` — `JUCE_WEB_BROWSER=0` workaround for the libwebkit2gtk dep
- JUCE forum thread "AudioParameter thread safety" — `https://forum.juce.com/t/audioparameter-thread-safety/21097` — `std::atomic` from message thread to audio thread is the canonical pattern
- JUCE forum thread "Understanding how JUCE handles JACK on Linux" — `https://forum.juce.com/t/understanding-how-juce-handles-jack-on-linux/65998` — JACK + ALSA both auto-detected; PipeWire transparent through PipeWire-JACK
- JUCE FAQ on `juce.com/get-juce/` — license tiers (Starter free / Indie $40/mo / Pro $175/mo / Educational free / AGPLv3 free for personal use); verified no commercial license needed for Anthony's personal-use v1.0

### Tertiary (LOW confidence — informational only)

- WolfSound blog "How To Build An Audio Plugin With JUCE" — pattern reference for unit-tests-via-Catch2 alternative; not chosen (JUCE built-in `UnitTest` is simpler)
- KVR Audio forum thread on interpolation quality — generic DSP wisdom that informs the WindowedSinc-vs-Lagrange-vs-Catmull-Rom choice; specific JUCE benchmarks not published

## Metadata

**Confidence breakdown:**

- Standard stack (JUCE 8.0.12, modules, FetchContent, juce_add_plugin Standalone): **HIGH** — multiple primary sources confirm; tested patterns in JUCE example projects
- Architecture (AudioProcessor + Editor + ParameterBridge + WavLoader + WetDryMixer): **HIGH** — idiomatic JUCE patterns; matches `examples/CMake/AudioPlugin/` reference; thread-safety pattern explicit in JUCE docs and forum guidance
- Pitfalls (rt-safety allocations, slider thread-safety, preset race, libwebkit2gtk on Linux, splash screen, interpolator state, sample rate negotiation): **HIGH** — all documented in JUCE forum / docs / breaking-changes; pitfall-3 (preset race) is enforced by libspu94's own thread-safety doc
- Validation Architecture: **MEDIUM** — JUCE UnitTest framework choice is sound but not extensively used in the existing project; planner may discover it doesn't integrate cleanly with the existing Unity + pytest + ctest umbrella and need to fall back to plain Catch2 or a simple JUCEApplication-derived test runner
- License posture (AGPLv3 for personal-use v1.0): **HIGH** — JUCE FAQ + AGPLv3 text + multiple forum confirmations; Anthony's posture matches; license pick deferred per CONTEXT
- Interpolator choice (WindowedSinc for one-shot load): **MEDIUM** — JUCE-published latency numbers are clear; "highest quality" attribution is from JUCE's own header documentation; subjective audio comparison not benchmarked in this research

**Research date:** 2026-04-26

**Valid until:** 2026-07-26 (90 days for a stable JUCE 8.x release; sooner if JUCE 9 announced or major Linux audio stack changes — re-verify the `apt` package list and JUCE module list before that horizon)
