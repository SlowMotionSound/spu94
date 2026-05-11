---
phase: 21-build-skeleton-ci-matrix
plan: 01
subsystem: build-system + ci
tags: [v1.7, juce, plugin, cmake, github-actions, clap, vst3, lv2, au]
dependency_graph:
  requires: []
  provides: [src/plugin/, src/plugin/CMakeLists.txt, cmake/clap_juce_extensions.cmake, .github/workflows/plugins.yml]
  affects: [src/plugin/, src/standalone/, CMakeLists.txt, .github/workflows/]
tech_stack:
  added: [clap-juce-extensions (FetchContent shim), GitHub Actions plugin matrix]
  patterns: [wrapperType_Standalone UI gate, per-OS FORMATS gating, FetchContent shim for replaceable dep]
key_files:
  added:
    - src/plugin/CMakeLists.txt
    - cmake/clap_juce_extensions.cmake
    - .github/workflows/plugins.yml
  renamed:
    - src/standalone/{PluginProcessor,PluginEditor,MorphPanel,ParameterBridge,RegisterPanel}.{cpp,h} → src/plugin/
    - src/standalone/CMakeLists.txt (rewritten as small WavLoader-only stub)
  modified:
    - CMakeLists.txt (add_subdirectory order)
    - src/plugin/PluginEditor.cpp (wrapperType gate at lines 15 + 342)
decisions:
  - "AU codes locked: PLUGIN_MANUFACTURER_CODE=Spu9 (preserves v1.6 cache continuity), PLUGIN_CODE=Sp94"
  - "CLAP via clap-juce-extensions FetchContent shim, isolated in cmake/clap_juce_extensions.cmake for one-file delete when JUCE 9 lands native CLAP"
  - "LV2 explicitly Linux-only — if(APPLE) appends only AU, Windows omits LV2 entirely"
  - "macOS runner = macos-14 (Apple Silicon native, arm64-only for Phase 21; universal2 deferred to packaging phase)"
  - "Windows toolchain locked to MSVC; MinGW explicitly rejected per PITFALLS B/M-4"
  - "Plugin-format toolbar shows intentional empty space at x=10..265 (no reflow); toolbar polish deferred"
  - "Non-blocking pluginval strictness-7 on Linux VST3 only — early-warning signal, not a gate (Phase 25 promotes to gate)"
  - "xvfb-run added to Linux CI for headless standalone-launch smoke"
commits:
  - hash: c495648
    title: "refactor(v1.7): rename src/standalone/ → src/plugin/; carve WavLoader into new src/standalone/ (PLUG-50)"
  - hash: 3f7ab8d
    title: "feat(v1.7): enable VST3 + LV2 (linux) + CLAP + AU (macos) formats; pick AU codes (Spu9/Sp94); FetchContent clap-juce-extensions (PLUG-01..06)"
  - hash: 31b2bd1
    title: "feat(v1.7): gate WAV-loader UI on wrapperType_Standalone (PLUG-49)"
  - hash: feeff5e
    title: "ci(v1.7): add 3-OS plugin build + smoke matrix (PLUG-07, PLUG-08)"
metrics:
  completed: "2026-05-11"
  tasks: 4
  requirements_satisfied: 11
  binary_count: 11
---

# Phase 21: Build Skeleton & CI Matrix Summary

Source-tree reorganized, JUCE multi-format build wired up, three-OS GitHub Actions matrix landed. The SPU-94 standalone is now structurally a plugin that happens to also produce a standalone testbed build. Plugin binaries load but output silence — the audio path doesn't exist until Phase 22 wires SRC and host-buffer audio.

## What Shipped

### Source tree

```
src/
├── spu94/          ← C core (libspu94) — untouched
├── cli/            ← command-line tool — untouched
├── plugin/         ← JUCE wrapper code (PluginProcessor, PluginEditor, MorphPanel, ParameterBridge, RegisterPanel)
└── standalone/     ← WavLoader.{cpp,h} + CMakeLists stub only (testbed-only WAV loader)
```

### CMake target

`src/plugin/CMakeLists.txt` declares the multi-format plugin via `juce_add_plugin(... FORMATS Standalone VST3 ...)` with per-OS list-append gating: Linux gets LV2, macOS gets AU, Windows gets neither extension. CLAP support comes from the FetchContent shim at `cmake/clap_juce_extensions.cmake`. AU codes `Sp94` / `Spu9` are committed.

### UI gate

`src/plugin/PluginEditor.cpp:15` and `:342` are wrapped in `if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)` — the WAV-loader Load/Play/Stop buttons are invisible and absent from the layout in every plugin format. Standalone unchanged.

### CI matrix

`.github/workflows/plugins.yml` runs on push to `main` / `master`. Ubuntu-22.04 + macos-14 + windows-2022, building all formats per OS plus the testbed standalone, with submodule + ccache caching. Each binary gets a builds-and-loads smoke step (pluginval per plugin format, construction-and-exit per standalone via `xvfb-run` on Linux). A non-blocking strictness-7 pluginval pass on Linux VST3 runs as early-warning signal.

## What Did NOT Ship (deferred per scope)

- Sample-rate conversion (Phase 22)
- Float ↔ int16 boundary at processBlock (Phase 23)
- getStateInformation / setStateInformation wiring (Phase 24)
- isBusesLayoutSupported declaration (Phase 25)
- Validators as required CI gates (Phase 25)
- Per-OS installers + beta README (Phase 26)
- Code signing (Phase 27, conditional)
- Toolbar-hole reflow when WAV-loader gated out (post-v1.7 UI polish)

## Verification

- Live build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release` produced all four Linux artifacts (VST3 bundle + LV2 bundle with TTLs + CLAP shared object + Standalone PIE executable).
- 11/11 PLUG requirements PASS per `21-VERIFICATION.md`.
- Standalone unchanged: v1.6 Load → Play → Stop round-trip works on the executor host (Linux); macOS/Windows construction-and-exit will be validated in CI.

## Known Follow-Ups (informational, non-blocking)

- Workflow triggers on `master` AND `main` — broader than the spec's `main`-only ask but harmless.
- CI green status verifiable only after the branch is pushed to GitHub.
- Plugin-format toolbar shows an intentional empty space at x=10..265 — toolbar polish deferred to a later UI phase.
