# Phase 21: Build Skeleton & CI Matrix — Context

**Gathered:** 2026-05-11
**Status:** Ready for planning
**Source:** Carried forward from v1.7 milestone-level discussion (locked decisions live in `.planning/PROJECT.md` Key Decisions table + `.planning/milestones/v1.7-REQUIREMENTS.md`). No separate discuss-phase session — all Phase-21-scope decisions were resolved at the milestone level.

<domain>
## Phase Boundary

**What this phase delivers:** the build-system + CI scaffolding that compiles the existing JUCE plugin code into the four production plugin formats (VST3 + AU + LV2 + CLAP) across the three target OSes (Linux + macOS + Windows), with the source tree reorganised to reflect the plugin's new role as the v1.7 product (standalone reframed as internal testbed).

**What this phase does NOT deliver:** any sample-rate conversion, any float↔int16 boundary work, any new state/automation surface, any channel-bus declarations beyond JUCE defaults, any validator-as-CI-gate work. Those are downstream phases (22, 23, 24, 25 respectively). Phase 21 produces *binaries that build and load* — no DSP-correctness gate yet.

## Inputs the planner must read

1. `.planning/milestones/v1.7-REQUIREMENTS.md` — PLUG-01..08 (build matrix), PLUG-49..51 (testbed reframe). These are the requirements this phase satisfies.
2. `.planning/milestones/v1.7-ROADMAP.md` — Phase 21 section (Goal, Files-provisional).
3. `.planning/research/ARCHITECTURE-v1.7.md` §8 (build layout proposals) and §0 (the "v1.7 is smaller than it looked — standalone is already a juce::AudioProcessor" finding).
4. `.planning/research/STACK-v1.7.md` §1 (JUCE 8 + clap-juce-extensions situation) and §2 (binary count enumeration).
5. `.planning/research/PITFALLS-v1.7.md` Phase-21-relevant items: B7 (per-OS bundle structure), M3 (VST3 SDK auto-download in CI), M4 (MSVC vs MinGW on Windows).
6. `CMakeLists.txt` + `src/CMakeLists.txt` + `src/standalone/CMakeLists.txt` — current build system entry points.
7. `src/standalone/PluginProcessor.cpp` + `src/standalone/PluginEditor.cpp` — confirm the architecture researcher's claim that the standalone is already a canonical `juce::AudioProcessor`.
</domain>

<decisions>
## Locked decisions (treat as inputs — do not re-decide)

### Plugin formats and OS matrix
- VST3 on Linux + macOS + Windows
- AU on macOS only
- LV2 on Linux only (explicitly dropped on Windows; Logic / Live / Cubase / Reaper-on-Windows etc. don't scan LV2)
- CLAP on Linux + macOS + Windows
- Standalone on Linux + macOS + Windows (internal testbed; built but not shipped to users)
- **Total: 11 user-facing format/OS combinations** (counting Standalone-as-testbed separately for build matrix purposes brings it to 14 build targets, but the user-facing distribution is 11)

### CLAP integration
- JUCE 8 lacks native CLAP support; JUCE 9 will add it but there is no public release date.
- Use `clap-juce-extensions` (free-audio/clap-juce-extensions, MIT-licensed shim) wired in via CMake `FetchContent`.
- Track the JUCE 9 release as a known-unknown; the shim is replaceable when JUCE 9 ships and is verified for the project.

### Source folder reorganisation
- Rename `src/standalone/` → `src/plugin/`. All files except `WavLoader.{cpp,h}` move with the rename.
- Create new `src/standalone/` (small) containing only `WavLoader.{cpp,h}` plus its CMakeLists (testbed-only WAV loader).
- The plugin and the standalone testbed build from `src/plugin/`; the standalone testbed additionally links `WavLoader` from `src/standalone/`.
- WAV-loader UI is gated by `wrapperType == wrapperType_Standalone` in `PluginEditor.cpp` — invisible in every plugin format.

### CI matrix
- Use GitHub Actions (the project's existing CI provider, assuming `.github/workflows/` is the right home — planner verifies during research read).
- Runner OS choices: `ubuntu-22.04`, `macos-13` or `macos-14` (planner picks the most stable/cheap available), `windows-2022`. Planner may revise if there's a project-existing convention.
- Toolchains: GCC on Linux, Apple Clang on macOS, MSVC on Windows (explicitly NOT MinGW per PITFALLS-v1.7.md M4 — MinGW miscompiles JUCE's SIMD paths).
- Submodules and `FetchContent` deps must be cached (ccache where applicable) so CI doesn't redownload SDKs every push.
- Build matrix produces all 11 user-facing binaries + 3 testbed standalones on every push to `main`.
- Smoke test on each binary: "builds and loads in at least one host of its native format." DSP correctness is NOT gated yet (deferred to Phase 25's pluginval gate).

### AU plugin codes
- Apple's AU requires 4-character `PLUGIN_CODE` + `PLUGIN_MANUFACTURER_CODE`. Planner chooses concrete values that don't collide with existing community codes — recommended manufacturer code is "Spu9" or similar; plugin code is "Sp94". These are committed in `CMakeLists.txt` via `juce_add_plugin(... PLUGIN_CODE ... PLUGIN_MANUFACTURER_CODE ...)`. Planner may revise but must record the chosen codes in PLAN.md so Phase 25's auval setup uses them consistently.

### Validation scope for this phase
- ONLY a builds-and-loads smoke test. Per-format/per-OS validators (pluginval, auval, lv2lint, VST3-validator) are NOT a Phase 21 deliverable — they are Phase 25's gate.
- Optional: a non-blocking pluginval pass on Linux VST3 only, as an early-warning signal. Planner decides whether to include this; if so, it's advisory not blocking.
</decisions>

<canonical_refs>
- `.planning/milestones/v1.7-REQUIREMENTS.md` (LOCKED — required reading; this phase satisfies PLUG-01..08 + PLUG-49..51)
- `.planning/milestones/v1.7-ROADMAP.md` (Phase 21 section)
- `.planning/research/STACK-v1.7.md` (JUCE 8 + clap-juce-extensions; binary enumeration)
- `.planning/research/ARCHITECTURE-v1.7.md` (build layout proposal; verifies standalone-as-AudioProcessor claim)
- `.planning/research/PITFALLS-v1.7.md` (B7 bundle structure, M3 VST3 SDK in CI, M4 MSVC over MinGW)
- `.planning/PROJECT.md` (Key Decisions table — 8 new v1.7 entries)
- Existing `CMakeLists.txt` + `src/CMakeLists.txt` + `src/standalone/CMakeLists.txt`
</canonical_refs>

<code_context>
- `src/standalone/PluginProcessor.{h,cpp}` — the existing `juce::AudioProcessor` implementation. Already RT-safe per the architecture researcher's read of lines 587-595 (empty `getStateInformation`/`setStateInformation` stubs are filled in Phase 24, not Phase 21).
- `src/standalone/PluginEditor.{h,cpp}` — the GUI. Contains the WAV-loader visible component that must be gated by `wrapperType` in Phase 21.
- `src/standalone/WavLoader.{h,cpp}` — the file to extract into the new small `src/standalone/` folder.
- `src/cli/` — CLI tool, untouched by Phase 21.
- `src/spu94/` — C core (`libspu94`), untouched by Phase 21 and by v1.7 generally.
- `.github/workflows/` — likely current home of CI; planner verifies during research read.
</code_context>

<scope_creep_redirects>
The following are deferred to other phases and must NOT be folded into Phase 21:
- Sample-rate conversion (Phase 22)
- Float↔int16 boundary (Phase 23)
- State serialisation + automation surface (Phase 24)
- Channel-bus declarations + auval gates + pluginval/lv2lint CI gates (Phase 25)
- Per-OS installers + beta README (Phase 26)
- Code signing (Phase 27, conditional)

If the planner identifies work that "would be nice to do in 21 since we're already touching the build system," capture it as a deferred idea, not a Phase 21 task.
</scope_creep_redirects>
