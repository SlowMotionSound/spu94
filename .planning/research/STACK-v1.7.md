# Stack Research — SPU-94 v1.7 DAW Plugin Port

**Domain:** Multi-format audio plugin packaging (VST3 + AU + LV2 + CLAP) across Linux + macOS + Windows, wrapping an existing untouched C99 DSP core in a JUCE-based plugin host glue layer.
**Researched:** 2026-05-10
**Confidence:** MEDIUM-HIGH on JUCE / CMake / GitHub Actions specifics; MEDIUM on signing-chain pricing/availability (the policies shifted in 2024-2025 and the picture is still moving); LOW on what specific Logic / Live / FL versions ship today (author should re-check at requirements time).

**Scope note:** This document covers only the *plugin port wrapper layer*. The bit-faithful C core (`libspu94`) is unchanged and untouchable, and the standalone JUCE GUI from v1.6 is reused inside the plugin window. Previous milestones' stack research (`STACK.md`, `STACK-v1.2.md`) covers the DSP-core tooling and is unaffected.

**Reading order for non-plugin-devs:** Sections 1 (JUCE), 2 (build system), 4 (packaging) are the "do this" parts. Section 3 (CI) is "how it actually runs". Section 5 (validation) is "how you prove it works". Sections on alternatives and pitfalls are surfaced rather than recommended — Anthony picks.

---

## 1. JUCE — Version, Plugin-Format Coverage, Licensing

### Current version

JUCE 8 (initial release **2024-06-13**) is the active major line. The latest tagged release I could verify is **8.0.12** (made VS 2026 the default in the Projucer). 8.0.x point releases shipped roughly monthly through 2025 (8.0.5 in January, 8.0.9 in September, etc.). **Confidence: HIGH** — release tags are visible on the [JUCE GitHub releases page](https://github.com/juce-framework/JUCE/releases) and the [JUCE 8.0.9 announcement](https://forum.juce.com/t/juce-8-0-9-is-out/67096).

### Format support matrix

| Format | JUCE 8 status | Notes |
|--------|---------------|-------|
| VST3 | First-class, stable | Standard target since JUCE 5 |
| AU (Audio Unit v2) | First-class, stable; macOS only | Logic / GarageBand format |
| AUv3 | First-class | Newer App-Extension style; not what classic Logic loads |
| LV2 | First-class since JUCE 7 (2022) | Confirmed in JUCE README and Wikipedia; export wrapper is `juce::LV2PluginFormat` |
| AAX | First-class (commercial license required) | Pro Tools; not in v1.7 scope |
| CLAP | **Not in JUCE 8 directly** | JUCE roadmap states CLAP support lands in JUCE **9**. For JUCE 8, you bolt on [`free-audio/clap-juce-extensions`](https://github.com/free-audio/clap-juce-extensions) — an MIT-licensed shim that emits a `.clap` from the same JUCE plugin source. |

The CLAP situation is the single most important "expectation vs reality" item in this milestone. Source: [JUCE Roadmap Update Q3 2024](https://juce.com/blog/juce-roadmap-update-q3-2024/) and [JUCE forum FR thread](https://forum.juce.com/t/fr-support-clap-for-plugins-host-client/51860).

**Implication:** v1.7 either (a) builds on JUCE 8 + `clap-juce-extensions` today (proven path, used by hundreds of shipping plugins including Surge XT, ChowDSP, u-he), or (b) waits for JUCE 9's native CLAP. (b) has no public release date as of the cutoff of my searches — author should re-check. Most working plugin developers in 2025 went with (a).

### Licensing tiers (JUCE 8) — surfaced, not recommended

JUCE 8 collapsed the old multi-tier structure. Current tiers per the [Get JUCE page](https://juce.com/get-juce/) and forum confirmations:

| Tier | Cost | Revenue cap | Notes |
|------|------|-------------|-------|
| GPLv3 | Free | None | Your plugin source must be GPLv3 too |
| Starter | Free | $20k/yr gross | New in JUCE 8; replaces the old Personal tier |
| Subscription | ~$50/month | None above tier | Closed-source distribution allowed |
| Perpetual | ~$1,000 one-time | None above tier | 30% discount for JUCE 4-7 holders |
| Educational | Free | Academic use | Separate flow |

The Personal tier (JUCE 7's $50k/year option) was removed in JUCE 8. Sources: [JUCE 8 license forum thread](https://forum.juce.com/t/juce8-license-and-amateur-programmers/61017), [LICENSE.md in master](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md).

This is purely surfaced — Anthony has explicitly deferred the open-source license pick (MIT vs Apache-2.0 for SPU-94 itself, and which JUCE tier sits underneath). It does not gate v1.7 implementation. The GPLv3 tier and the Starter tier both work for a free public beta; the choice between them is governed by what SPU-94's eventual public license is.

### Alternatives briefly considered, dismissed

| Framework | Why not |
|-----------|---------|
| [iPlug2](https://github.com/iPlug2/iPlug2) | More liberal license, smaller binaries (~1-2 MB vs JUCE's 5+ MB), GPU-accelerated vector graphics. But: smaller community, less DAW-by-DAW polish, and we already have a working JUCE GUI from v1.6 — porting it to iPlug2 would be a rewrite, which is explicitly out of scope. |
| [DPF (DISTRHO Plugin Framework)](https://github.com/DISTRHO/DPF) | Excellent Linux coverage (LADSPA, DSSI, LV2, VST2, VST3, CLAP), small footprint. But: AU support is limited/absent and the Linux-first emphasis matters less now that the std-alone JUCE GUI exists. Same rewrite problem. |
| `juce_emscripten` (mentioned in the brief) | This is JUCE-to-WASM for browsers, not a desktop plugin format. Not relevant to a 3-OS × 4-format desktop beta. |

The single overriding factor is "we already have a JUCE GUI". v1.6 ships a working JUCE standalone today; v1.7 puts the same GUI in a plugin shell. Switching frameworks would be a UI rewrite — explicitly out of scope per the locked decisions.

---

## 2. Build System

### Projucer vs CMake — the modern path is CMake

JUCE supports both:

- **Projucer** — JUCE's own GUI project generator. Outputs Xcode / Visual Studio / Make / Linux Makefile projects. Originally the recommended path.
- **CMake** — Native, first-class JUCE support since 2020. Documented in [JUCE/docs/CMake API.md](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md).

For a 3-OS × 4-format CI matrix in 2026, **CMake is the path everyone uses**. The Projucer requires a GUI to maintain the project, which is hostile to headless CI. Every contemporary template I found (Pamplejuce, JUCE-Plugin-Starter, Melatonin's how-to) is CMake-based. The [Melatonin "How to use CMake with JUCE" guide](https://melatonin.dev/blog/how-to-use-cmake-with-juce/) is the de facto reference. Source: [JUCE CMake support thread](https://forum.juce.com/t/native-built-in-cmake-support-in-juce/38700).

### One source tree -> ten binaries

The JUCE CMake `juce_add_plugin` function takes a `FORMATS` list:

```cmake
juce_add_plugin(SPU94
    FORMATS VST3 AU LV2 Standalone    # CLAP added separately via clap-juce-extensions
    ...
)
```

This generates *one CMake target per format*, all sharing the same `PluginProcessor.cpp`/`PluginEditor.cpp` source. Building all targets on one machine produces all formats that platform supports:

| OS | Targets built | Notes |
|----|---------------|-------|
| Linux | VST3, LV2, CLAP, Standalone | No AU |
| macOS | VST3, AU, LV2, CLAP, Standalone | All formats |
| Windows | VST3, LV2, CLAP, Standalone | No AU |

So the 10-unique-binary count = (VST3 + LV2 + CLAP + Standalone) × 3 OSes + AU × 1 OS = 12 + 1 = 13 actually, if you count the Standalone build. The brief says 10; the difference is whether Standalone is counted, and whether AU is counted separately or rolled into the macOS bundle. Either way the matrix is clear.

### Cross-compilation: no, you can't build a Mac binary on Linux

This is the realism point. Native macOS binaries (universal2 = arm64 + x86_64) **require building on macOS** with Xcode's toolchain. Apple's signing infrastructure, code-signing utilities, and the macOS SDK are only available on Mac. This is a hard wall.

Workflow:

| Target | Build host | Why |
|--------|-----------|-----|
| Linux binaries | Linux runner (GitHub Actions `ubuntu-latest`) | Native; clang or gcc |
| Windows binaries | Windows runner (`windows-latest`) | Native MSVC required for proper VST3 ABI; MinGW *can* build VST3 but pluginval and some DAWs complain |
| macOS binaries (universal2) | macOS runner (`macos-latest` or `macos-14`) | Xcode-only; `CMAKE_OSX_ARCHITECTURES=arm64;x86_64` produces the fat binary |

GitHub-hosted `macos-latest` runners are now Apple Silicon by default (transition completed during 2024). To produce a universal2 binary you don't need both M-series and Intel hardware — `clang` on Apple Silicon cross-compiles to Intel transparently when you pass both architectures. Source: [Apple's universal binary docs](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary), [JUCE CMake universal binary thread](https://forum.juce.com/t/cmake-plugin-and-os-11-universal-binary/41997).

---

## 3. CI / Build Infrastructure

### GitHub Actions matrix

The well-trodden pattern (used by Pamplejuce, Surge XT, ChowDSP family):

```yaml
strategy:
  matrix:
    os: [ubuntu-latest, macos-latest, windows-latest]
```

On each OS you build all formats that OS supports. Artifacts are uploaded per-OS with OS-tagged names to avoid path collisions. Reference templates:

- [Pamplejuce](https://github.com/sudara/pamplejuce) — JUCE 8 + Catch2 + pluginval + macOS notarization + Azure Trusted Signing on Windows + GitHub Actions. The de facto "what does a serious 2025 JUCE CI look like" answer. Single most useful reference for v1.7.
- [JUCE-Plugin-Starter](https://github.com/danielraffel/JUCE-Plugin-Starter) — smaller, simpler, also CMake-based, also matrix CI.

### Linux runner: build dependencies

The official list is [JUCE/docs/Linux Dependencies.md](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md). For a plugin (not a host that needs WebKit), the minimum apt packages on Ubuntu are:

```
libx11-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev
libfreetype6-dev libfontconfig1-dev libasound2-dev libglu1-mesa-dev
```

Optional (skippable with JUCE preprocessor flags):

- `libwebkit2gtk-4.0-dev` — only if you embed WebView UI. SPU-94 doesn't; disable with `JUCE_WEB_BROWSER=0`.
- `libcurl4-openssl-dev` — only if you make HTTP calls. SPU-94 doesn't; disable with `JUCE_USE_CURL=0`.

Note: `libwebkit2gtk-4.0-dev` was removed from recent Ubuntu / installable only via `4.1` on Ubuntu 24.04+ — flag-disabling it dodges the whole problem.

### macOS runner: Apple Silicon by default

`macos-latest` is now `macos-14` (Apple Silicon, M-series). To produce universal2 with CMake:

```
cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
```

A deployment target of 11.0 (Big Sur) is a common floor — earlier targets can't reliably do arm64. 10.13 / 10.15 floors are common but force you to drop arm64.

### Windows runner: MSVC vs MinGW

**Use MSVC.** The VST3 SDK is officially Windows-on-MSVC, several DAWs (FL Studio especially) have historical compatibility quirks with MinGW-built VST3s, and Microsoft's signing tooling assumes MSVC PE binaries. GitHub Actions `windows-latest` ships Visual Studio 2022 — JUCE 8.0.12 made VS 2026 the Projucer default but VS 2022 is still fully supported. Source: [JUCE 8.0.12 release notes](https://github.com/juce-framework/JUCE/releases/tag/8.0.12).

### Caching

Most templates cache the JUCE submodule build and (on macOS) ccache output across runs. Pamplejuce demonstrates the pattern. Without caching, full cold builds take ~10-15 minutes per OS; with caching, 3-5 minutes per OS. Not a blocker, but worth wiring in early.

---

## 4. Packaging and Distribution

### Plugin install paths the hosts actually scan

This is one of the highest-friction "you don't know what you don't know" parts of plugin shipping. Each format has rigid, host-enforced search paths:

| Format | Linux | macOS | Windows |
|--------|-------|-------|---------|
| VST3 | `~/.vst3` (user) and `/usr/lib/vst3` (system) | `~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3` | `C:\Program Files\Common Files\VST3` — Steinberg-mandated; *no other path is reliably scanned* by compliant DAWs |
| AU | n/a | `~/Library/Audio/Plug-Ins/Components` and `/Library/Audio/Plug-Ins/Components` | n/a |
| LV2 | `~/.lv2` and `/usr/lib/lv2` (and `/usr/local/lib/lv2`) | `~/Library/Audio/Plug-Ins/LV2` and `/Library/Audio/Plug-Ins/LV2` | `%COMMONPROGRAMFILES%\LV2` or `%APPDATA%\LV2` (less standardized) |
| CLAP | `~/.clap` and `/usr/lib/clap` | `~/Library/Audio/Plug-Ins/CLAP` and `/Library/Audio/Plug-Ins/CLAP` | `%COMMONPROGRAMFILES%\CLAP` and `%LOCALAPPDATA%\Programs\Common\CLAP` |

Plus all four formats honour the host-defined `CLAP_PATH` / `VST3_PATH` / `LV2_PATH` / etc. environment variables. Sources: [Steinberg VST plug-in locations Windows](https://helpcenter.steinberg.de/hc/en-us/articles/115000177084-VST-plug-in-locations-on-Windows), [Steinberg VST plug-in locations Mac](https://helpcenter.steinberg.de/hc/en-us/articles/115000171310-VST-plug-in-locations-on-Mac-OS-X-and-macOS), [CLAP search path issue](https://github.com/free-audio/clap/issues/46).

The **Windows VST3 path is unusually rigid** — many DAWs only look at `C:\Program Files\Common Files\VST3` and ignore configured custom paths. This matters for installer design.

### Installer chains per OS

#### macOS

Two common patterns:

1. **`.pkg` installer** — built with `productbuild` / `pkgbuild`, optionally wrapped in a `.dmg`. Notarized once and stapled. Anthony will need a Developer ID Installer certificate for `.pkg` and a Developer ID Application certificate for the plugin binaries. Source: [Melatonin "How to code sign and notarize macOS audio plugins in CI"](https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/).
2. **Drag-install `.dmg`** — user drags the `.vst3` / `.component` etc. into the right folder themselves. Simpler, but harder for non-technical testers.

Locked decision (D-007): notarization may be deferred for beta. Testers can right-click → Open to bypass Gatekeeper for unsigned plugins. The friction is real but tolerable for a closed beta circle.

#### Windows

Common choices:

- **[Inno Setup](https://jrsoftware.org/isinfo.php)** — free, scriptable, the audio plugin community's default. Pamplejuce uses it.
- **NSIS** — also free, more flexible, slightly clunkier syntax.
- **MSI** — heavyweight, enterprise-flavoured, overkill here.
- **Plain ZIP** — works for technical users but very awkward because the install path is `C:\Program Files\Common Files\VST3` which requires admin to write.

For the beta phase, **Inno Setup** is the path of least resistance.

On signing: the SmartScreen situation changed in 2024. Per [Moonbase's "Code signing audio plugins in 2025" round-up](https://moonbase.sh/articles/code-signing-audio-plugins-in-2025-a-round-up/), the EV-cert-bypasses-SmartScreen advantage was removed; all signed binaries now build reputation through download volume. Three current options:

| Option | Cost | Notes |
|--------|------|-------|
| OV cert on USB dongle (Sectigo etc.) | ~$300/yr | Painful in CI — can't sign in cloud |
| OV cert in cloud KMS (AWS / Azure KMS) | $300 cert + KMS fees | Works in CI |
| [Azure Trusted Signing](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options) | $9.99/month | US/Canada only, business >3yrs old or individual. Reputation built into the service. |

Locked decision (D-007): also deferrable for beta. Testers click through SmartScreen.

#### Linux

There is no equivalent of AppImage for plugins. Plugins are not standalone executables, they're shared libraries / bundles that the host dlopens — AppImage's "single relocatable file" model doesn't apply.

Practical options:

1. **Tarball** with a tiny `install.sh` that copies to `~/.vst3`, `~/.clap`, `~/.lv2`. This is what every Linux audio plugin author actually ships.
2. **Distro packages** (`.deb` / `.rpm` / Arch PKGBUILD) — more polished but per-distro maintenance overhead.
3. **Flatpak** — exists but plugin-host interop is fragile; not worth it for beta.

For v1.7 beta: tarball + install.sh.

---

## 5. Validation and QA Tooling

### [pluginval](https://github.com/Tracktion/pluginval) — the de facto cross-format validator

Cross-platform, headless, scriptable. Validates VST3, AU, LV2 (and CLAP, with recent builds). The community default. Strictness levels 1-10; **level 5 is the recommended baseline** for "this won't crash a host"; level 10 includes parameter fuzzing and state-restoration stress.

Pluginval is added as a CMake target (git submodule) and run automatically in CI after every build. Sources: [Tracktion/pluginval repo](https://github.com/Tracktion/pluginval), [juce-cookbook pluginval chapter](https://github.com/tobanteAudio/juce-cookbook/blob/master/chapters/testing/pluginval.md).

This is the single most valuable QA tool in the chain. The Pamplejuce template wires it in by default.

### [`auval`](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/) — Apple's AU-specific validator

macOS-only, ships with the OS. Logic *will not load* an AU that doesn't pass `auval`. There is no substitute for this check.

```
auval -v aufx <plugin-subtype> <plugin-manufacturer>
```

Run on every macOS CI build that produces an AU.

### VST3 SDK validator

The Steinberg VST3 SDK ships its own validator (`validator` binary). Less broadly used than pluginval but worth running once as a baseline.

### LV2 lint: `lv2lint`

Validates the LV2 TTL metadata and basic plugin contract. Apt-installable on Ubuntu. Pamplejuce includes it for LV2 targets.

### Host-specific quirks worth knowing about

Surfaced rather than recommended — these are landmines, not action items:

| Host | Quirk |
|------|-------|
| **Logic Pro** | Caches AU validation results. After installing a new AU, often need to `killall AudioComponentRegistrar` or boot Logic with cache reset. Plugin bundle ID changes invalidate cache. |
| **Ableton Live** | Has its own authorization layer for some plugin types and caches plugin scans aggressively. State restoration test against Live is important. |
| **Reaper** | Most forgiving host; usually the first place to test. Scan behaviour is logged verbosely. Has its own VST3-bridging quirks for old VST2s but VST3 is clean. |
| **FL Studio** | Historically picky about VST3 ABI — especially MinGW-built VST3s. MSVC builds are fine. |
| **Bitwig** | First-class CLAP host; the canonical CLAP test target. |
| **Ardour / Qtractor** | First-class LV2 hosts; the canonical Linux test targets. |

The locked beta DAW list in the milestone brief (Reaper / Live / Logic / FL) covers VST3 + AU + Windows quirks well. Adding Bitwig (CLAP) and Ardour (LV2 on Linux) would round out the format-coverage check.

---

## 6. Confidence Summary and Open Gaps

| Claim | Confidence | Basis |
|-------|------------|-------|
| JUCE 8.0.12 is current; CLAP needs `clap-juce-extensions` until JUCE 9 | HIGH | Official roadmap + release tags |
| LV2 export is first-class in JUCE 8 | HIGH | Wikipedia + GitHub README + format reference |
| CMake is the modern build path | HIGH | Universal community consensus, every current template |
| `macos-latest` GH runner is Apple Silicon, universal2 via `CMAKE_OSX_ARCHITECTURES` | HIGH | Apple docs + JUCE forum |
| Plugin install paths per OS per format | HIGH | Cross-checked Steinberg + CLAP repo + Linux audio docs |
| Pamplejuce is the de facto JUCE 8 CI template | MEDIUM-HIGH | Widely cited; not formally endorsed |
| pluginval is the validation default | HIGH | Universal consensus |
| Azure Trusted Signing pricing / availability | MEDIUM | Microsoft policy moved during 2024-2025; Moonbase article is dated mid-2025; author should re-verify before paying |
| EV-cert SmartScreen bypass was removed in 2024 | MEDIUM | Multiple secondary sources agree; haven't located the primary Microsoft announcement |
| Logic / Live / FL / Reaper specific quirks | MEDIUM | Folklore-heavy; specific symptoms differ across DAW versions; author should triage by symptom not by version |

### Open gaps the author should verify at requirements time

1. **JUCE 9 release date.** If CLAP-native is imminent, waiting may be cheaper than the `clap-juce-extensions` glue. Public roadmap as of the cutoff did not commit to a release date — I could not confirm this.
2. **Notarization-deferred friction in practice.** macOS Sequoia tightened Gatekeeper UX again in 2024-2025; the "right-click → Open" bypass still works but the path differs from older OS versions. Worth a manual UAT before relying on it for beta.
3. **CLAP install-path conventions on Windows.** Less standardized than VST3; different hosts prefer different paths (`%COMMONPROGRAMFILES%\CLAP` vs `%LOCALAPPDATA%\Programs\Common\CLAP`). Bitwig and Reaper agree on the former; install script should write there.
4. **`clap-juce-extensions` CMake integration** with JUCE 8.0.x specifically — the README implies compatibility but the audit trail across JUCE point releases is informal. Worth a smoke build before committing the architecture.
5. **Microsoft Store / WinGet** as alternative distribution — not raised in the brief; surfaced here as a thing-to-not-bother-with for beta.

---

## Sources

### Primary (HIGH)

- [JUCE GitHub repo / README](https://github.com/juce-framework/JUCE)
- [JUCE 8.0.12 release](https://github.com/juce-framework/JUCE/releases/tag/8.0.12)
- [JUCE 8.0.9 announcement](https://forum.juce.com/t/juce-8-0-9-is-out/67096)
- [JUCE CMake API doc](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md)
- [JUCE Linux Dependencies doc](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md)
- [JUCE 8 LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
- [JUCE Roadmap Q3 2024 (CLAP in JUCE 9)](https://juce.com/blog/juce-roadmap-update-q3-2024/)
- [JUCE Roadmap Q1 2025](https://juce.com/blog/juce-roadmap-update-q1-2025/)
- [Tracktion/pluginval](https://github.com/Tracktion/pluginval)
- [free-audio/clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)
- [free-audio/clap Linux path issue #46](https://github.com/free-audio/clap/issues/46)
- [Steinberg VST plugin paths — Windows](https://helpcenter.steinberg.de/hc/en-us/articles/115000177084-VST-plug-in-locations-on-Windows)
- [Steinberg VST plugin paths — macOS](https://helpcenter.steinberg.de/hc/en-us/articles/115000171310-VST-plug-in-locations-on-Mac-OS-X-and-macOS)
- [Apple — Building a universal macOS binary](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary)
- [Microsoft — Code signing options for Windows developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
- [Microsoft — SmartScreen reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

### Secondary (MEDIUM)

- [sudara/pamplejuce](https://github.com/sudara/pamplejuce) — community-leading JUCE 8 CI template
- [Melatonin — How to use CMake with JUCE](https://melatonin.dev/blog/how-to-use-cmake-with-juce/)
- [Melatonin — Code sign and notarize macOS audio plugins in CI](https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/)
- [Melatonin — Azure Trusted Signing on Windows](https://melatonin.dev/blog/code-signing-on-windows-with-azure-trusted-signing/)
- [Moonbase — Code signing audio plugins in 2025](https://moonbase.sh/articles/code-signing-audio-plugins-in-2025-a-round-up/)
- [Reilly Spitzfaden — Plugins for Everyone! Cross-Platform JUCE with CMake & GitHub Actions](https://reillyspitzfaden.com/posts/2025/08/plugins-for-everyone-crossplatform-juce-with-cmake-github-actions/)
- [juce-cookbook — pluginval chapter](https://github.com/tobanteAudio/juce-cookbook/blob/master/chapters/testing/pluginval.md)
- [Jatin Chowdhury — Why I use JUCE](https://jatinchowdhury18.medium.com/why-i-use-juce-fae2b1b7441e)
- [Jatin Chowdhury — Building LV2 Plugins with JUCE and CMake](https://jatinchowdhury18.medium.com/building-lv2-plugins-with-juce-and-cmake-d1f8937dbac3)

### Tertiary (LOW — surfaced for completeness)

- [iPlug2 repo](https://github.com/iPlug2/iPlug2) — alternative framework, dismissed
- [DPF (DISTRHO) repo](https://github.com/DISTRHO/DPF) — alternative framework, dismissed
- KVR forum threads on iPlug2 vs JUCE, Windows code-signing options — community folklore, not authoritative

---

*Stack research for: shipping SPU-94 as VST3 + AU + LV2 + CLAP on Linux + macOS + Windows.*
*Researched: 2026-05-10.*
*Author should re-verify JUCE 9 release status and Azure Trusted Signing pricing/regional availability before committing.*
