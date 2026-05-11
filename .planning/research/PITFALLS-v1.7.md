# Pitfalls Research — v1.7 DAW Plugin Port

**Domain:** Packaging the bit-faithful libspu94 reverb as VST3 + AU + LV2 + CLAP on Linux + macOS + Windows (10 binaries), built via JUCE, reusing the v1.6 standalone GUI.
**Researched:** 2026-05-10
**Confidence:** MEDIUM-HIGH on platform/host facts (paths, cache file names, validator behaviour all cross-checked across vendor support docs + JUCE forum threads); LOW on transient details (SmartScreen reputation timing, JUCE-version-specific regressions) — flagged inline.

---

## Orientation

v1.0-v1.6 are shipped. The bit-faithful C core (`libspu94`) is *immutable* through this milestone — already proven RT-safe by 4 ctest gates (`rt_no_heap`, `rt_no_locks`, `rt_no_syscalls`, `rt_bench_latency`). All v1.7 pitfalls land in the *wrapper layer* between host and core: sample-rate conversion, float→int16→float, format-specific glue, host quirks, packaging, validation.

The standalone GUI is already JUCE; reusing it in the plugin window is mostly a wiring exercise — but mostly is doing work.

Severity legend:

- **WILL BLOCK RELEASE** — plugin fails to load, fails validation, or is silently broken for a meaningful host
- **WILL CAUSE BETA CONFUSION** — installs but produces wrong behaviour, support load, "is this me or the plugin?"
- **MINOR** — polish, second-order

---

## WILL BLOCK RELEASE

### B1: Audio-thread real-time safety violations in the JUCE wrapper

**What goes wrong:**
The C core is RT-safe and CI-verified. The JUCE wrapper around it almost certainly is not, by default. `juce::AudioProcessor::processBlock` runs on the audio callback; anything that allocates, locks, syscalls, logs, or touches an `std::shared_ptr` violates RT safety. Pluginval since ~1.0 now intercepts realloc/malloc/free called from the processBlock context (`tracktion/pluginval` adds explicit RT-safety checks).

Specific JUCE-template patterns that violate RT safety:
- `std::vector::resize`, `std::string` construction, `new`/`delete` inside processBlock
- `MidiKeyboardState`'s internal lock (taken by both GUI and processBlock paths)
- `MessageManagerLock` from the audio thread (deadlock, not just RT violation)
- `DBG()` / `Logger::writeToLog` (syscall + may allocate)
- `juce::String` operations that aren't on stable interned strings
- `std::shared_ptr` for any audio-thread object (atomic refcount contention + destructor may run on audio thread)
- `juce::CriticalSection` used to gate parameter reads (`getCallbackLock()` is fine to *hold* during host's processBlock, but acquiring it from the GUI thread blocks audio)

**Why it happens:**
JUCE's `AudioProcessorValueTreeState` (APVTS) is the mandatory thread-safe parameter bridge but is opt-in. Project templates often skip it. SR/BD conversion adds an extra trap surface: a JUCE `juce::Interpolators::Lagrange` or `LagrangeInterpolator` instance held by the processor is safe; one allocated inside processBlock when sample rate changes mid-block is not.

**Prevention:**
1. All wrapper state (SRC buffers, int16 scratch, latency-compensation ring) allocated in `prepareToPlay`. Never inside `processBlock`.
2. Use APVTS for every automatable parameter. No raw atomics under app-level locks.
3. Run pluginval at strictness ≥7 in CI; that strictness level enables RT-safety interception.
4. The C core already provides the gold standard — keep the wrapper to the same discipline.

**Verification:** `pluginval --strictness-level 10 --validate-in-process false plugin.vst3` in CI. Strictness 5 is "host compatibility floor"; 7+ is "you actually mean it"; 10 includes parameter fuzz + multi-state restore.

---

### B2: AU validation (`auval`) failure — Logic / GarageBand will silently refuse to load

**What goes wrong:**
`auval` runs whenever Logic, GarageBand, MainStage, or Final Cut first sees a new/changed `.component`. If `auval` returns nonzero, the plugin is added to a per-user cache as "failed" and never loaded. Users do not see "your plugin is broken" — they see "your plugin is missing." Logic's GUI does not surface the auval log unless you go to Plug-In Manager → "Show in Finder" path-by-path.

Causes specific to a JUCE wrapper around an int16 mono-or-stereo DSP:
- **`BusesProperties` mismatch with `isBusesLayoutSupported`** — recurring JUCE 7/8 issue where AU reports a different channel-layout matrix than the JUCE callback claims to support; result is mono routing reported as "not allowed" and auval fails the channel-config phase. JUCE forum thread #66725 documents this still being open on multiple JUCE 7.x/8.x branches.
- **Reporting a sidechain / aux bus that has no actual implementation** — auval enumerates every advertised bus and crashes when one is null.
- **Latency reported as non-integer or negative after a sample-rate change** — auval rejects.
- **Plugin allocates / blocks in the AU initialise path** — auval is fairly tolerant of initialisation slowness but not unbounded.

The often-cited "plugin took too long to load" error in Logic is not actually a fixed timeout from Apple's published docs — it surfaces when `AudioUnitInitialize` does heavy work synchronously. Multiple JUCE forum threads (e.g. #35213, #15719) attribute this to filesystem reads at startup (loading presets, IR files) and recommend moving all such work into `prepareToPlay` or off-thread.

**Prevention:**
1. Start with the *simplest possible* `BusesProperties`: mono in/mono out + stereo in/stereo out. No sidechain, no aux. Only widen once validators pass.
2. Run `pluginval --strictness-level 7 ...` — pluginval ≥1.0 invokes `auval` internally at strictness ≥5, so this catches most auval failures before manual auval is needed.
3. Run `auval -v aufx <subtype> <manuf>` manually before each release (e.g. `auval -v aufx SP94 Spu9`). The `-v` is the verbose load test; non-zero exit = won't load in Logic.
4. After every change to bus config or component metadata, clear Logic's AU cache or Logic will keep the stale "failed" entry indefinitely (see B3).

---

### B3: Stale host caches — your fix doesn't appear to work because the host is cached against the old broken build

**What goes wrong:**
Every host caches plugin scan results to avoid re-validating GBs of plugins on every launch. When you ship a beta-tester a fixed build, their host may keep using the broken-cached entry, and they will report "still broken." You will spend hours debugging code that is fine.

| Host | Cache location | Reset method |
|---|---|---|
| **Logic / GarageBand / MainStage** (macOS) | `~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache` and `com.apple.audiounits.sandboxed.cache` | Delete both files, or Logic → Settings → Plug-In Manager → "Full Audio Unit Reset" |
| **Ableton Live 12.1+** | `~/Library/Application Support/Ableton/Live Database/Live-plugins-1.db` (mac) / `%LOCALAPPDATA%\Ableton\Live Database\Live-plugins-1.db` (win) | Preferences → Plug-Ins → Rescan; **Alt+click Rescan** for full rescan. Live 12.1 rescans automatically on first launch after update, not subsequent. |
| **Reaper** | `<resource path>/reaper-vstplugins64.ini` (and `-au`, `-clap`, `-lv2` siblings) | Preferences → VST → Re-scan, or "Clear cache and re-scan VST paths for all plugins", or delete the .ini |
| **Cubase / Nuendo** (Win) | `%APPDATA%\Steinberg\Cubase <ver>_64\Vst3 Cache\` plus `vst3blacklist.xml` and `vst3plugins.xml` | Delete files, or VST Plug-in Manager → Blacklist tab → Reactivate |
| **Studio One** | `PluginBlacklist.settings` + `PlugInScanner.log` under user prefs | Preferences → Locations → VST Plug-Ins → Reset Blocklist |
| **Bitwig** | Scans in a separate worker process so a crashing plugin doesn't kill the host (this is by design); CLAP path scanned alongside VST3, no documented priority order despite repeated forum claims | Dashboard → Settings → Locations → Rescan |

**A plugin that crashes during scan is immediately blacklisted in Cubase, Studio One, and (effectively) Logic.** If your first beta build crashes, every tester's host will refuse to look at it again until they reset.

**Prevention:**
1. Document the cache-reset procedure per host in the beta-tester README — assume *every* beta-tester will hit this once.
2. Bump the plugin's *unique ID* / *bundle version* on every beta build; some hosts use that to invalidate cache entries.
3. Before declaring a build broken, ask the tester to do a full rescan.

---

### B4: AU plugins ignored when installed to user path on macOS

**What goes wrong:**
Apple's Audio Units architecture officially supports both `/Library/Audio/Plug-Ins/Components/` (system) and `~/Library/Audio/Plug-Ins/Components/` (user). In practice, the system path is what every tester expects to work; Logic in particular has been observed to ignore the user path in some configurations (especially when sandboxed/sandbox-extended). Survey of Apple's third-party AU support article: "Audio Units plug-ins appear as individual components in `/Library/Audio/Plug-Ins/Components`" — system path is documented; user path is acknowledged but not primary. KVR and Steinberg helpdesks consistently recommend system path for AU.

For VST3: both `/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/VST3/` work reliably across hosts.

**Prevention:**
1. AU installer writes to `/Library/Audio/Plug-Ins/Components/` (requires admin password — acceptable). Do not offer "user install" for AU.
2. VST3 / CLAP can use user path freely.
3. LV2 on macOS: install to `/Library/Audio/Plug-Ins/LV2/` (system) or `~/Library/Audio/Plug-Ins/LV2/`. Ardour / Carla / Mixbus look in both. Note macOS LV2 hosting is niche — most beta testers using LV2 will be on Linux.

---

### B5: Channel layout — JUCE BusesProperties / `isBusesLayoutSupported` / AU mismatch

**What goes wrong:**
SPU-94 is naturally stereo (the algorithm has L+R independent paths). But the JUCE template often advertises `MainBusIn = stereo, MainBusOut = stereo` only, and auval then rejects mono→stereo or mono→mono use, which Logic auto-instances on a mono track. The result: plugin appears in Logic's effect list but greys out on mono channels.

JUCE forum thread #34326 documents the specific incantation. KVR + JUCE forum #66725 are still open on JUCE 8 about specific layout combinations being mis-reported to AU when LV2/CLAP/VST3 report them correctly.

**Prevention:**
1. Explicitly enumerate supported layouts in `isBusesLayoutSupported`: mono→mono, mono→stereo, stereo→stereo. Reject everything else. Surround / Atmos: out of scope, fail-gracefully (return false).
2. Test with `auval` and pluginval that all three configurations validate.
3. For mono→stereo: decide what "mono input to a stereo reverb" means (most likely: drive both L and R reverb paths with the same input, output stereo). Document the decision in an ADR — beta testers *will* try mono tracks.

---

### B6: Sample-rate conversion latency not reported → host timing is wrong

**What goes wrong:**
The wrapper does float-host-rate ↔ int16-44.1k SRC. SRC filters have group delay — typically 16-32 samples for a high-quality polyphase, more for windowed-sinc. If the wrapper does not report this latency via `setLatencySamples()`, the host's plugin-delay-compensation (PDC) is wrong, and the wet signal arrives offset from where the user placed it. On a busy mix this manifests as ghosted-double transients or pre-echo — extremely confusing to diagnose.

Compounded: many hosts only read `getLatencySamples()` *once* per session, at first instantiation. If the wrapper computes latency in `prepareToPlay` (correct) but the host has already read latency from an earlier call (e.g. construction-time default), the new value is ignored. Steinberg's own docs note Cubase/Nuendo update latency on transport start/stop, not mid-stream.

**Prevention:**
1. Call `setLatencySamples(totalLatency)` *inside* `prepareToPlay`, *every* time it's called, after computing total latency = SRC_in_group_delay + core_latency + SRC_out_group_delay.
2. If sample rate or block size changes (host can do this on the fly), recompute and call `setLatencySamples` again. Cubase will re-poll; Logic / Live may not until next instantiation — acceptable for v1.7.
3. Document the precise latency formula in an ADR.
4. Verify with a null-test in Reaper (which honours PDC accurately): place plugin on track A, dry copy on track B, invert track A's polarity — sum should be silent (or near-silent) ±latency drift.

---

### B7: VST3 bundle structure differs per OS — easy to ship a non-bundle

**What goes wrong:**
VST3 on macOS = `.vst3` *bundle* (a directory tree with `Contents/MacOS/<binary>` + `Contents/Info.plist`). On Windows = `.vst3` *file* in modern style, or a directory with `Contents/x86_64-win/<binary>.vst3` (the bundle-on-Windows form, which is what every modern host expects). On Linux = directory `<name>.vst3/Contents/x86_64-linux/<name>.so`.

Ship the wrong form and the host's scanner skips it silently. JUCE's CMake `juce_add_plugin(... FORMATS VST3 ...)` produces correct output by default — but custom CMake glue or zip/installer scripts can flatten the bundle and break it.

LV2 is a *directory* on all platforms — never a single file. The directory must contain `manifest.ttl` + plugin `.so`/`.dylib`/`.dll` + per-plugin `.ttl`.

CLAP is a single file (`.clap`) on all platforms but Apple still treats it as needing to live in `/Library/Audio/Plug-Ins/CLAP/` or `~/Library/Audio/Plug-Ins/CLAP/`.

**Prevention:**
1. After install (or before zipping for distribution), `ls` the install path and verify the bundle structure. Add a CI step that runs `pluginval` against the *installed* artifact, not the build artifact.
2. Windows VST3: prefer the bundle-on-Windows form. Most current hosts accept either, but Cubase 12+ and Studio One 6+ prefer the bundle form.

---

### B8: LV2 ttl errors will fail lv2lint silently

**What goes wrong:**
LV2 plugins are validated by `lv2lint` (and `sord_validate` for ttl syntax). Common failures pulled from GitHub issue trackers (`DPF-Plugins #17`, `surge-synthesizer/surge #2392`, `amsynth #178`):
- Missing `doap:name`, `doap:license`, `lv2:minorVersion`, `lv2:microVersion`
- `doap:license` not a valid URI
- Missing `foaf:mbox` or `foaf:email` for the maintainer
- TTL parse errors (unescaped characters, unexpected EOF)
- Plugin .so exports `lv2_generate_ttl` or other extraneous symbols
- UI type declaration missing or wrong

JUCE's LV2 export (added relatively recently — JUCE 7+) generates the ttl automatically and is generally clean, but if the wrapper exposes parameters with unusual names or unusual types, the auto-generated ttl can be incomplete.

**Prevention:**
1. CI step: `lv2lint <plugin-uri> <bundle-dir>` and `sord_validate <ttl-files>`. Both must exit 0.
2. Pick a stable plugin URI early — once published it's a contract, e.g. `https://spu94.dev/plugins/reverb`. Changing it later orphans every saved Ardour/Carla session.

---

## WILL CAUSE BETA-TESTER CONFUSION

### C1: Standalone JUCE wrapper changes system volume on launch/close (already documented in MEMORY.md)

**What goes wrong:**
Project memory already flags this: JUCE's standalone wrapper changes macOS system volume on launch and close. v1.6 ships with the standalone — v1.7 adds plugin formats. The standalone keeps shipping, so the bug keeps shipping. Beta testers running the standalone alongside the plugin will hit it.

**Prevention:** Tracked separately as a pre-release bugfix carry. Not v1.7 scope to fix, but worth a release-note line.

---

### C2: JUCE 7/8 standalone wrapper sample-rate-change handling broken on Windows

**What goes wrong:**
JUCE forum #67625 (open): `StandalonePluginHolder` on Windows does not call `prepareToPlay` with the new SR when the user changes audio device sample rate via the device control panel. The processor stays initialised for the old SR and silently produces wrong-rate audio. JUCE 7.0.8 also had a Mac standalone I/O config bug; JUCE 7 standalone wrapper crashes on selecting "None" device where JUCE 6 didn't.

The plugin-format wrappers are less affected because hosts call `prepareToPlay` explicitly on SR change.

**Prevention:**
1. In the plugin port, handle SR changes inside `prepareToPlay` — recompute SRC ratios, reallocate scratch buffers (the existing pre-allocated ones from `prepareToPlay`), recompute latency.
2. For the standalone (still shipping alongside): document the known Windows limitation; tell users to relaunch on SR change.

---

### C3: Denormals on x86_64 vs Apple Silicon — different default behaviour

**What goes wrong:**
On x86_64, denormalized floats can be 17-100× slower than normals. Without FTZ (flush-to-zero) and DAZ (denormals-are-zero) enabled, a feedback line that decays toward zero will slow down dramatically — audible as CPU meter spikes near silence. KVR thread #573073 confirms Apple Silicon defaults to flushing denormals on macOS (single FPCR flag), so M1/M2 users won't see it; x86_64 Windows / Linux / Intel-Mac will.

The libspu94 core is int16-only and immune. The wrapper, however, runs SRC in float and feedback for any future float-domain processing in float — vulnerable.

**Prevention:**
1. Wrap every `processBlock` body in `juce::ScopedNoDenormals` (sets FTZ/DAZ on entry, restores on exit). One-line fix.
2. Document in the wrapper code why it's there.

---

### C4: State save / restore — saving a v1.7 preset that won't load in v1.8

**What goes wrong:**
`getStateInformation` / `setStateInformation` are how DAW projects round-trip plugin state. v1.4 already shipped a preset file format (`.spu94`) and v1.6 extended it for user waypoints with byte-identical backward compatibility — that discipline is the right model. The plugin layer adds parameter-automation state on top of that. Pitfalls:

- **Locale-dependent float parsing** (JUCE forum #55189): `juce::String::getFloatValue` does not handle comma-decimal locales. A tester on a German / French / Russian Windows system saves a session; their host stores plugin state via `getStateInformation`; reloading on a US-locale machine parses "1,5" as "1" silently. Use `String::toStdString` + `std::stof` with explicit `std::locale::classic()`, or store as integer (Q15 fixed-point, which matches the core anyway), or use binary serialisation through `MemoryBlock`. The cleanest: binary-serialise the existing `.spu94` preset bytes into the host state.
- **Parameter ordering** — if the param ID list changes between versions, sessions saved with v1.7 won't restore correctly in v1.8. Use APVTS with stable string IDs (not integer indices). Add a version byte at the start of the state blob; refuse to load future versions, do best-effort on older.
- **Multi-instance state crosstalk** — if any plugin state lives in a static / global, two instances corrupt each other. The C core is per-instance (state struct passed by pointer), so this is mostly the wrapper's discipline.

**Prevention:**
1. Wrap state as: `[4-byte magic 'SPU9'] [1-byte version] [body length] [body = existing .spu94 preset bytes]`. Body is the exact byte format v1.4 / v1.6 already produce.
2. Multi-instance smoke test: open two instances, set different params, save session, reopen, verify each has its own state.

---

### C5: Plugin GUI keyboard-focus stealing in Live / Logic / Cubase

**What goes wrong:**
JUCE plugin windows on Windows can "eat" keyboard messages so that host shortcuts (spacebar play, etc.) don't reach the host. JUCE forum #68017 (open). On M1 Logic, plugin window resize permanently loses keyboard focus (#51292).

The current standalone GUI was designed without host-focus etiquette. Reusing it in the plugin window means: a tester hits spacebar expecting transport, the plugin's TextEditor or focused Component catches it instead.

**Prevention:**
1. Override `keyPressed`/`keyStateChanged` in the editor's root component to return `false` for unhandled keys, letting the host receive them.
2. Set `setWantsKeyboardFocus(false)` on non-input components.
3. Test in Reaper, Live, Logic: open plugin window, focus it by clicking, hit spacebar — host transport must respond.

---

### C6: HiDPI / Retina scaling mismatch between host and plugin

**What goes wrong:**
Host has its own DPI scale (Windows 150%, Retina 2×). Plugin window can either honour the host's reported scale or paint at 1:1. JUCE handles this reasonably out of the box, *but* if the standalone GUI uses pixel-exact assets (which the PS1-themed UI may, e.g. for the 280px morph dial), the result on a 1440×900 Retina Logic window will look correct, while on a 4K Windows 200% Studio One window text may render hand-tiny or hand-huge depending on which DPI awareness manifest is set.

**Prevention:**
1. Test the plugin window at 100%, 150%, 200% on Windows; at 1× and 2× on Mac.
2. JUCE plugin format wrappers handle DPI declaration; the editor itself must use `juce::Component::setTransform` or relative layout, not pixel-fixed positions, where possible.

---

### C7: Logic AU and resizable window contract

**What goes wrong:**
Different formats have different resizable-window contracts:
- VST3: host calls `onSize` before showing
- AU v2: host queries size once at open
- AU v3: host fully manages resize
- CLAP: explicit `gui.set_size` / `gui.can_resize` calls

If the standalone GUI is fixed-size, reuse is easy: declare non-resizable in every format. If it's resizable (v1.5's morph panel hints it might be), each format wrapper needs its own resize plumbing.

**Prevention:** Declare the plugin window as fixed-size for v1.7 unless the standalone GUI is fully resize-clean already. Defer resizable to a later milestone.

---

## MINOR

### M1: VST3 SDK licence changed to MIT in October 2025

Steinberg relicensed VST3 SDK 3.8.0 to MIT (Oct 2025). ASIO went dual GPL3 / proprietary. The old "you must sign Steinberg's developer agreement" friction is gone — pull the SDK from GitHub freely. JUCE bundles it. Worth confirming the JUCE version pinned uses 3.8.0+ (JUCE 8.x as of 2026-05-10 should). Sources: KVR news 2025-10, librearts.org write-up 2025-11, Steinberg developer FAQ.

### M2: GitHub Actions macOS runner Xcode-version drift

GitHub-hosted macOS runners change default Xcode every few weeks. A build that signs and notarises today can fail tomorrow because Xcode changed `codesign` defaults. Pin Xcode explicitly: `sudo xcode-select -s /Applications/Xcode_15.4.app`. Re-pin every quarter.

### M3: VST3 SDK auto-download by JUCE 7+

JUCE 7+ can auto-fetch the VST3 SDK on first CMake configure. In offline / restricted CI this fails. Either commit the SDK as a submodule or use `juce_set_vst3_sdk_path()` pointing at a vendored copy.

### M4: Windows toolchain — MSVC vs MinGW vs clang-cl

JUCE supports all three but historically MSVC is the smoothest path. MinGW DLL builds can produce VST3 binaries that fail to load in Live/Cubase due to MSVC-runtime ABI mismatches in the host. Stick with MSVC; clang-cl works but adds CI complexity.

### M5: CLAP plugin features string

CLAP plugins declare features via `clap_plugin_descriptor.features[]` — a null-terminated array of strings. For SPU-94, the correct features are `CLAP_PLUGIN_FEATURE_AUDIO_EFFECT` + `CLAP_PLUGIN_FEATURE_STEREO` + `"reverb"`. Missing or wrong feature strings means the plugin doesn't appear in host category filters but still loads.

### M6: Logic plugin manufacturer/subtype code collisions

AU plugins are identified by `(type, subtype, manufacturer)` four-char codes. If another plugin on the tester's system uses the same triple, only one will load. Pick a manufacturer code unique to this project (e.g. `Spu9` or `S94 `) and a subtype that won't collide (`SP94`). Document in `Info.plist` template.

---

## Phase-Specific Warnings

| Phase topic | Likely pitfall | Mitigation |
|---|---|---|
| SRC design | B6 latency; quality vs CPU tradeoff | Pick library early (JUCE Lagrange, libsamplerate, r8brain); benchmark; document |
| Float↔int16 conversion | Clip vs saturate vs dither | Match core's saturation behaviour at the boundary; the core is `sat_s16` (ADR-0001); the wrapper should saturate the same way |
| BusesProperties | B5 channel-layout failure | Enumerate mono/mono, mono/stereo, stereo/stereo explicitly; reject all else |
| State serialisation | C4 locale + version drift | Wrap with magic+version header; body is byte-identical `.spu94` bytes |
| Code signing (deferred) | Will become blocker before public release | OK for beta; add CI hooks now even if disabled, so the day you flip the switch isn't a panic |
| CI matrix | M2 Xcode drift, M4 toolchain choice | Pin every toolchain version explicitly |
| Validators in CI | B1, B2, B5, B8 caught here | `pluginval --strictness-level 7`, `auval -v`, `lv2lint`, VST3 `validator` — all on every build |

---

## Sources

### HIGH confidence (Apple / Steinberg / JUCE official, cross-checked)

- [Apple — Where third-party AU plug-ins are installed](https://support.apple.com/en-us/102239)
- [Steinberg — VST plug-in locations on macOS](https://helpcenter.steinberg.de/hc/en-us/articles/115000171310-VST-plug-in-locations-on-Mac-OS-X-and-macOS)
- [JUCE — Configuring bus layouts for your plugins](https://docs.juce.com/master/tutorial_audio_bus_layouts.html)
- [JUCE — AudioProcessor class reference](https://docs.juce.com/master/classAudioProcessor.html)
- [JUCE — CMake API documentation](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md)
- [Tracktion pluginval — README + strictness levels](https://github.com/Tracktion/pluginval)
- [Steinberg — VST3 SDK validator command line](https://steinbergmedia.github.io/vst3_dev_portal/pages/What+is+the+VST+3+SDK/Validator.html)
- [VST3 SDK licence change to MIT (Oct 2025) — librearts.org write-up](https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/)
- [free-audio/clap — plugin-features.h](https://github.com/free-audio/clap/blob/main/include/clap/plugin-features.h)
- [Ableton — Rescanning plug-ins in Live 12.1](https://help.ableton.com/hc/en-us/articles/16261934134940-Rescanning-plug-ins-in-Live-12-1)

### MEDIUM confidence (vendor support articles + cross-confirmed forum threads)

- [Acustica — Logic AU cache reset path](https://acusticaudio.freshdesk.com/support/solutions/articles/35000146023-re-setting-the-logic-pro-x-plug-ins-cache-manually)
- [Acustica — Cubase plugin cache reset](https://acusticaudio.freshdesk.com/support/solutions/articles/35000145928-re-setting-the-cubase-plug-ins-cache-manually)
- [Soundtoys — Studio One blacklist reset](https://support.soundtoys.com/article/75-studio-one-blacklist-guide)
- [Slate — Rescan Plugins in Reaper](https://stevenslateaudio.zendesk.com/hc/en-us/articles/360047223613-How-To-Rescan-Plugins-In-Reaper)
- [Melatonin — Code signing & notarising audio plugins in CI](https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/)
- [Microsoft — SmartScreen reputation for app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)
- [Bitwig — VST plug-ins user guide](https://www.bitwig.com/userguide/latest/vst_plug-ins/)

### MEDIUM-LOW confidence (single-thread forum reports; useful as failure-mode signal, not as authoritative behaviour spec)

- [JUCE forum — Pluginval real-time safety checking](https://forum.juce.com/t/pluginval-real-time-safety-checking/67439)
- [JUCE forum — auval BusesLayout / channel-info mismatch](https://forum.juce.com/t/br-wrong-auchannelinfo-reported-for-some-bus-layout-combinations/66725)
- [JUCE forum — isBusesLayoutSupported / auval mono/mono SOLVED](https://forum.juce.com/t/isbuseslayoutsupported-auval-and-mono-mono-configuration-solved/34326)
- [JUCE forum — Updating properties of AU FAILED (Logic)](https://forum.juce.com/t/updating-properties-of-au-failed-message-from-logic/35213)
- [JUCE forum — calling setLatencySamples in prepareToPlay](https://forum.juce.com/t/calling-setlatencysamples-in-preparetoplay/48131)
- [JUCE forum — StandalonePluginHolder Windows SR change bug](https://forum.juce.com/t/standalonepluginholder-cant-cope-with-external-samplerate-changes-on-windows/67625)
- [JUCE forum — Mac standalone I/O bug in 7.0.8](https://forum.juce.com/t/mac-standalone-plugin-i-o-configuration-bug-in-7-0-8/59737)
- [JUCE forum — M1 Logic keyboard focus lost after resize](https://forum.juce.com/t/bug-m1-apple-silicon-keyboard-focus-lost-permanently-in-logic-when-plugin-is-resized/51292)
- [JUCE forum — String::getFloatValue locale comma-decimal](https://forum.juce.com/t/string-getfloatvalue-do-not-handle-decimal-comma/55189)
- [KVR — Denormals on arm64/M1 systems](https://www.kvraudio.com/forum/viewtopic.php?t=573073)
- [KVR — Logic Pro AU validation rant thread](https://www.kvraudio.com/forum/viewtopic.php?t=596679)
- GitHub LV2 issue trackers: DPF-Plugins #17, surge-synthesizer/surge #2392, amsynth #178

### Not verified (flagged inline as LOW where used)

- Exact Logic AU validation timeout threshold (commonly cited but not in Apple's published docs)
- Bitwig CLAP-first scan-order claim (asserted in some KVR threads, contradicted by user-guide reading)
- SmartScreen reputation aging "several weeks / hundreds of installs" — Microsoft's official text is intentionally vague

---

*Pitfalls research for: v1.7 DAW Plugin Port milestone*
*Researched: 2026-05-10*
