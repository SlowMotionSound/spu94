# Feature Landscape: DAW Plugin Port (v1.7)

**Domain:** Multi-format DAW plugin (VST3 + AU + LV2 + CLAP) on Linux/macOS/Windows, JUCE-based, wrapping the bit-faithful int16/44.1kHz `libspu94` C core
**Researched:** 2026-05-10
**Confidence:** HIGH on JUCE-side capabilities and what the four plugin formats expect (verified against Steinberg/CLAP/LV2/Apple docs and JUCE source). MEDIUM on real-world host quirks (Logic AU caching, Reaper LV2 quirks, FL Studio VST3 state restore order). LOW on Anthony's specific beta audience expectations -- listed where flagged.

Scope: lay out the field of what a modern DAW reverb plugin is expected to do, so the requirements step can pick what v1.7 actually ships. No commitments here.

---

## 1. Parameter Handling

### 1.1 Host Automation

The core machinery is `juce::AudioProcessorParameter` (and the typed subclasses `AudioParameterFloat`, `AudioParameterInt`, `AudioParameterChoice`, `AudioParameterBool`). Hosts read these on plugin instantiation and expose them as automation lanes. JUCE handles per-format wiring (VST3 `IEditController`, AU `AudioUnitGetProperty`, LV2 control ports, CLAP `clap_plugin_params`).

| Parameter style | JUCE class | Host UI | SPU-94 candidates |
|---|---|---|---|
| Continuous float | `AudioParameterFloat` | Continuous lane | Morph position, dry/wet, input gain |
| Discrete stepped | `AudioParameterInt` (or `AudioParameterFloat` with `isDiscrete=true`) | Stepped lane | Waypoint index (snap mode) |
| Choice/enum | `AudioParameterChoice` | Drop-down or stepped | Preset slot, mode select (Macro/Advanced), DAC on/off |
| Bool | `AudioParameterBool` | Toggle | ADPCM enabled, DAC enabled |

`isDiscrete()` override matters: hosts that draw continuous interpolation between automation points produce garbage on integer-valued parameters (e.g. waypoint index 3.7 is meaningless). For SPU-94's morph dial, the underlying parameter is a float `[0.0, 1.0]` but it visually snaps to 17 detents -- JUCE's `AudioParameterFloat` with a custom `stringFromValue` handles this; the parameter stays continuous from the host's perspective so smooth automation sweeps work as expected.

**Sources:** [AudioProcessorParameter (JUCE)](https://docs.juce.com/master/classAudioProcessorParameter.html), [Adding plug-in parameters (JUCE tutorial)](https://docs.juce.com/master/tutorial_audio_parameter.html). HIGH confidence.

### 1.2 Parameter Smoothing

Two distinct concerns, often conflated:

1. **Denormal/click protection on raw int16 register writes.** When a host automates a value over a sample-rate-dependent timeline, the parameter callback fires asynchronously from `processBlock`. Reading a parameter once per block and applying it as a step can cause zipper noise on audible parameters (gains, faders). JUCE provides `juce::SmoothedValue<float, ValueSmoothingTypes::Linear>` (or `Multiplicative` for gains) -- the standard pattern is per-sample interpolation in the audio thread.
2. **SPU-94-specific: register writes already have well-defined timing semantics** (TICK_LATCHED, etc., per ADR-0005). The reverb engine itself does NOT want smoothed register values -- that would smear the hardware-faithful step behavior. Smoothing belongs on the wrapper-side musical controls (input gain, dry/wet mix, output gain), not on the registers.

This is a sound-AND-feel decision: smoothing dry/wet at the wrapper level prevents zipper noise during DAW automation passes; NOT smoothing register-level controls preserves the characteristic "thunk" of mid-stream parameter changes that's part of the SPU's character. The North Star applies -- quirks ARE the product.

**Sources:** [Parameter smoothing methods (JUCE forum)](https://forum.juce.com/t/parameter-smoothing-methods-algorithms/14773). MEDIUM confidence on JUCE 8 specifics; HIGH on the architectural principle.

### 1.3 A/B Comparison

A/B is a strong table-stakes expectation in modern reverb plugins (FabFilter, Valhalla, ReLab, Eventide all have it). Implementation in JUCE is notoriously awkward: the obvious approach (`AudioProcessorValueTreeState::replaceState()`) nukes the undo history, and developers usually end up rolling their own. There's an active JUCE forum thread asking for first-class A/B support -- it doesn't exist.

For v1.7 beta scope, this is a likely-defer ("nice-to-have"), not table-stakes for beta testers. Could be deferred to a polish milestone.

**Sources:** [How to make A/B button (JUCE forum)](https://forum.juce.com/t/how-to-make-a-b-button-in-plugin-without-reseting-undo-history-with-apvts/54464). MEDIUM confidence.

### 1.4 Undo/Redo

Host-level undo is automatic when parameters use `setValueNotifyingHost`. Plugin-internal undo (e.g. waypoint edits via the v1.6 EDIT/EXPORT/LOAD stack) is plugin-owned -- hosts won't touch it. SPU-94 already has a notion of editable state outside the parameter surface (waypoint slots, preset slots), so the wrapper has to be deliberate about which actions hit the host's undo stack and which stay internal.

---

## 2. State Persistence

### 2.1 Plugin State Save/Load

Every format requires the plugin to round-trip its full state to/from a host-owned blob. JUCE unifies this via `AudioProcessor::getStateInformation(MemoryBlock&)` and `setStateInformation(const void*, int)`. Underneath:

| Format | State container | Notes |
|---|---|---|
| VST3 | `IComponent::getState` + `IEditController::getState` chunks (two distinct chunks; first 4 bytes record IComponent length) | JUCE bundles both via `MemoryBlock` |
| AU | `kAudioUnitProperty_ClassInfo` -- a CFPropertyList dict | JUCE wraps the binary blob in a property-list entry; class-info is what Logic stores in `.cst` files |
| LV2 | `LV2_State_Interface` save/restore callbacks using Atom properties; serialized to Turtle for portability; `atom:Path` REQUIRED for any file paths | LV2 state is the most spec-formal of the four |
| CLAP | `clap_plugin_state` save/load via stream callbacks; binary blob, plugin-defined format | Simplest API; just a stream of bytes |

**SPU-94-specific:** v1.6 already serializes 46 engine fields + 8 user waypoint slots + 9 anchor waypoints + morph position + mixer state to `.spu94` files. Reusing that serializer inside `getStateInformation` is the right move -- one format internally, embedded into whatever the host wants externally. Care needed: the `.spu94` format must remain forward-compatible because a v1.7 plugin instance saved into a DAW session today must reload correctly after a future SPU-94 version update.

**Sources:** [VST3 State (KVR thread on inspector)](https://github.com/CharlesHolbrow/vst-chunk-inspector), [LV2 State spec](https://lv2plug.in/ns/ext/state), [AU ClassInfo (Apple Developer)](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/AudioUnitDevelopmentFundamentals/AudioUnitDevelopmentFundamentals.html). HIGH confidence on LV2 and VST3 mechanics; MEDIUM on AU class-info edge cases.

### 2.2 Factory Preset Packaging

Each format handles factory presets differently:

| Format | Mechanism | SPU-94 fit |
|---|---|---|
| VST3 | `IUnitInfo::getProgramListInfo` + program changes via parameter | Sony's 9 + 10 anchor presets ship in-binary as program list |
| AU | `kAudioUnitProperty_FactoryPresets` returns `CFArrayRef` of `AUPreset` | Same: ship anchors as factory presets |
| LV2 | Companion `.ttl` files in plugin bundle declaring `pset:Preset` | More work: presets are external files in the LV2 bundle |
| CLAP | `clap_plugin_preset_load` extension; presets can be in any format the plugin can load | Simplest: just point at bundled `.spu94` files |

For v1.7 beta, "factory presets accessible from the host's preset menu" is **table stakes** for VST3/AU/CLAP (users expect it; Reaper/Live/Logic all surface it) and **nice-to-have** for LV2 (LV2 hosts vary in preset UI quality; users often manage `.ttl` presets out-of-band). The 9 Sony preset names already ship as `spu94_presets[]` strings in libspu94.

### 2.3 User Preset Directories (Per-OS)

If SPU-94 ships its own preset browser (it does -- v1.4 custom dropdown), there's a convention for where to look. JUCE's `File::getSpecialLocation(File::userApplicationDataDirectory)` gives the OS-correct base path:

| OS | Convention | Example |
|---|---|---|
| Linux | `~/.config/SPU-94/presets/` (XDG) or `~/.SPU-94/presets/` | freedesktop XDG |
| macOS | `~/Library/Audio/Presets/SPU-94/SPU-94/` | Apple AU convention |
| Windows | `%APPDATA%\SPU-94\presets\` | `C:\Users\<u>\AppData\Roaming\SPU-94\presets\` |

**Convention conflict on macOS:** Logic auto-discovers `.aupreset` files in `~/Library/Audio/Presets/<Manufacturer>/<Plugin>/` and exposes them in its preset menu. If SPU-94 uses the same path for `.spu94` files, Logic will try to load them as `.aupreset` and fail noisily. Two-directory split or distinct file extension fixes this.

**Sources:** [Cross-platform presets (JUCE forum)](https://forum.juce.com/t/cross-platform-presets-any-ideas/6449), [getSpecialLocation (JUCE docs)](https://docs.juce.com/master/classFile.html). HIGH confidence.

### 2.4 Default-on-Load Behavior

When a host instantiates a fresh plugin (no saved state), it expects audible defaults. Currently v1.6 starts with the Hall preset loaded. v1.7 inherits this. Edge case: when a DAW restores a session, the plugin gets a `setStateInformation` call -- but if the saved state is corrupted or from a future version, the plugin must gracefully fall back to defaults rather than crash. JUCE does NOT enforce this; it's on the plugin to validate.

---

## 3. Latency & Timing

### 3.1 Latency Reporting (PDC)

`AudioProcessor::setLatencySamples(int)` reports latency to the host. JUCE distributes this to each format's PDC mechanism (VST3 `getLatencySamples`, AU `kAudioUnitProperty_Latency`, LV2 `lv2:latency` port, CLAP `clap_plugin_latency`).

**SPU-94's latency story for v1.7 is non-trivial:**

| Source | Latency at 44.1kHz | Notes |
|---|---|---|
| Half-band FIR (39-tap) at I/O | 19 samples each side (= 38 round-trip) | Already in core, well-defined |
| ADPCM upstream | 28 samples | Only when ADPCM enabled |
| True 8x oversampled DAC (v1.3) | 35 samples equivalent at 44.1kHz | Only when DAC enabled |
| **NEW for v1.7:** SRC wrapper | ~32-128 samples depending on resampler quality and host SR | Variable |
| **NEW for v1.7:** Block alignment | up to (block_size - 1) | Worst-case |

JUCE forum warns: many hosts ignore `setLatencySamples` calls AFTER initial setup, so dynamic latency changes (DAC toggle, ADPCM toggle) may NOT be re-negotiated. Best practice: report worst-case latency once in `prepareToPlay`, keep it stable. Trades a few ms of dead air for reliability across hosts.

**Sources:** [Delay Compensation (JUCE forum)](https://forum.juce.com/t/delay-compensation-and-setlatency/15936), [setLatencySamples (JUCE forum)](https://forum.juce.com/t/setlatencysamples/14507). HIGH confidence on JUCE-side; MEDIUM on host-specific behavior.

### 3.2 Tempo Sync

Modern reverbs commonly offer **tempo-synced pre-delay** (1/8th, dotted-1/16, etc.). SPU-94's "pre-delay" isn't a real parameter -- it's a side-effect of waypoint position via the SPU's dLAPF1/dRAPF1 register pair. So *traditional* tempo-sync doesn't map cleanly. Future creative direction (already in deferred ideas list): tempo-synced morph LFO -- the morph dial sweeping at musical subdivisions. Out of v1.7 scope; flag as future.

`AudioPlayHead::PositionInfo` gives BPM, time signature, and play state. Cheap to query; no implementation cost if not exposed to user.

### 3.3 Transport Awareness

`AudioPlayHead::PositionInfo` also surfaces `isPlaying`, `isLooping`, `isRecording`. Some reverb plugins use this for "freeze on stop" -- when transport stops, hold the reverb tail rather than letting it ring out (or vice versa). SPU-94 has no equivalent at v1.7 scope. Flag as a possible future creative lever ("transport-aware freeze" = hold the SPU memory contents on stop).

---

## 4. Audio I/O

### 4.1 Channel Configurations

SPU-94 is stereo-in / stereo-out by nature (the SPU reverb has independent L/R sides with cross-coupling). JUCE's `isBusesLayoutSupported` callback declares which `AudioChannelSet` combinations the plugin accepts.

| Config | Support recommendation | Rationale |
|---|---|---|
| Stereo -> Stereo | **Required** | Native fit; this is the SPU |
| Mono -> Stereo | **Required** | Common reverb send pattern from mono source |
| Mono -> Mono | **Strongly recommended** | Logic and Ableton mono channels; AU validation (`auval`) tests this |
| Stereo -> Mono | Not recommended | Discards information; unusual for reverb |
| Sidechain (stereo + sidechain) | Out of scope | No SPU-94 use case |
| Surround (5.1/7.1.x/Atmos) | Out of scope | Niche reverb on a vintage hardware emulation; users who want Atmos go elsewhere |

**`auval` constraint:** AU validation is stricter than VST3 -- a plugin that supports mono-out but not mono-in will fail Logic's plugin scan. Cleanest: declare a small fixed set (Mono->Mono, Mono->Stereo, Stereo->Stereo) and let other configs be rejected explicitly.

**Sources:** [Configuring bus layouts (JUCE)](https://juce.com/tutorials/tutorial_audio_bus_layouts/), [isBusesLayoutSupported + AUVAL (JUCE forum)](https://forum.juce.com/t/isbuseslayoutsupported-auval-and-mono-mono-configuration-solved/34326). HIGH confidence.

### 4.2 Buffer Sizes

The plugin MUST tolerate any block size the host hands it -- 32, 64, 128, ..., 2048, AND weird sizes (Logic sometimes uses 1024+remainder when transport scrubs; FL Studio can deliver 23-sample blocks during automation render). The bit-faithful core is sample-by-sample inside, so block size only affects the wrapper's loop structure, not correctness. Tests should cover at least {32, 64, 128, 512, 1024, 2048, and one prime like 257} -- pluginval does this automatically.

### 4.3 Sample Rate

This is the v1.7 architectural centerpiece. Host SR can be anything (44.1, 48, 88.2, 96, 176.4, 192 kHz; some hosts also expose 22.05 and 384). Core is fixed at 44.1. The wrapper does float32(host SR) -> int16(44.1k) -> CORE -> int16(44.1k) -> float32(host SR). The SRC choice is a separate planning decision (likely JUCE's `LagrangeInterpolator` for cheap, or `Catmull-Rom`, or a polyphase libsoxr-style). For 48kHz->44.1k specifically, the ratio 147/160 admits exact polyphase decimation.

---

## 5. MIDI & Control

### 5.1 MIDI CC

Some hosts (Reaper, Bitwig, Live) let users assign MIDI CC to plugin parameters via the host's automation/MIDI-learn system -- this requires NOTHING from the plugin beyond `setValueNotifyingHost`. Plugins that expose their own MIDI-learn UI (e.g. NI Massive) are doing additional work.

**Recommendation for v1.7:** no plugin-side MIDI-learn UI. Host handles it. Free feature.

### 5.2 MIDI Note Input

Irrelevant for a reverb. Plugin should declare `acceptsMidi() = false` and `producesMidi() = false`. This stops the host from drawing a MIDI input lane next to the plugin, which is the correct UX.

### 5.3 OSC / Generic Remote

Out of scope for v1.7. Power users of Reaper/Bitwig can use external control via the host's OSC layer if they want. No plugin work needed.

---

## 6. UI/UX Integration

### 6.1 Resizable Plugin Window

Modern hosts expect plugin windows to be resizable, often with drag handles. JUCE's `AudioProcessorEditor::setResizable(bool, bool)` flags whether the user and/or host can resize. A `ComponentBoundsConstrainer` enforces min/max bounds and aspect ratio.

The current standalone GUI is fixed-size. Plugin port adds a constraint: it should at minimum scale gracefully. **Recommendation for v1.7 beta:** fixed-size is acceptable for beta but expect feedback. Resizable with constrained aspect ratio is the polished path; ~1-2 days of refactor on the GUI side.

**Sources:** [AudioProcessorEditor (JUCE docs)](https://docs.juce.com/master/classAudioProcessorEditor.html). HIGH confidence.

### 6.2 HiDPI / Retina

`AudioProcessorEditor::setScaleFactor(float)` is called by the host when it knows the display DPI. JUCE's own components scale correctly when this is honored. Custom-drawn components (SPU-94 has many: the morph dial with PS1 dots, the panels) need to use the scale factor in their `paint()` calls.

The current standalone implementation may or may not be DPI-correct -- worth checking on a 4K monitor before v1.7 ships. Flagged for verification, not a research finding.

### 6.3 Generic UI Fallback

LV2 hosts (Ardour, Carla, Qtractor) and some VST3 hosts can render a generic parameter-grid UI when the plugin's custom UI fails to load or isn't supported. Requires NO plugin work; it's host-side. The plugin gets it for free as long as parameters are properly declared.

CLAP also has `clap_plugin_gui` extension; if the plugin doesn't implement it, the host falls back to generic. Same as above -- free.

### 6.4 Host Transport State in UI

The UI can display BPM, time signature, play indicator, etc. by polling `getPlayHead()` from a UI timer. Useful for a future tempo-sync feature; not needed for v1.7 beta.

---

## 7. Discovery & Metadata

### 7.1 Plugin Name / Vendor / Category

JUCE Projucer/CMake sets the name and vendor once; format-specific category mappings are auto-generated.

| Format | Category convention |
|---|---|
| VST3 | `Fx\|Reverb` (Steinberg-defined string -- verified) |
| AU | `kAudioUnitType_Effect` ('aufx'); subtype is 4-char manufacturer-chosen (not standardized) |
| LV2 | `lv2:ReverbPlugin` class in `.ttl` |
| CLAP | features array including `audio-effect`, `reverb`, `stereo` (constants from `clap/plugin-features.h`) |

**Sources:** [VST3 plugType strings](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/group__plugType.html), [AU kAudioUnitType_Effect](https://developer.apple.com/documentation/audiotoolbox/1584142-audio_unit_types/kaudiounittype_effect), [CLAP plugin-features.h](https://github.com/free-audio/clap/blob/main/include/clap/plugin-features.h). HIGH confidence.

### 7.2 Search Keywords / Tags

VST3 SubCategories accept multiple pipe-separated values (`"Fx|Reverb|Bass"` etc.). CLAP features array is a list of strings. Useful tags for SPU-94: `reverb`, `audio-effect`, `stereo`, possibly `lo-fi` (CLAP defines this), possibly `vintage`. Free metadata; helps discovery in plugin browsers that index by tag.

### 7.3 "Beta" Labeling

No standardized convention across formats. Options:
- Append "(beta)" to display name (visible everywhere; ugly on release)
- Include version in name ("SPU-94 1.7-beta")
- Custom about/info dialog with "BETA" badge (cleaner; only visible when user looks)

**Recommendation for v1.7:** version-in-name during beta; drop the "-beta" suffix on stable release. This is conventional in the JUCE plugin world (Surge XT, Dexed, Vital all do versioned naming).

---

## 8. Niche-but-Expected for Reverb Plugins

### 8.1 Wet/Dry Mix at Plugin Level

**Expected by users**, even when the engine has its own dry/wet (SPU-94 has the v1.2 send/return mixer). The plugin-level mix is the FINAL knob users reach for, the one that says "how much of this plugin am I hearing." The internal mixer is for shaping what goes into the reverb.

Two designs:
1. Plugin "Mix" knob *separate* from engine mixer (post-engine crossfade, like FabFilter)
2. Plugin "Mix" knob *bound to* the engine's master fader (less confusing, simpler)

Option 2 is more honest to SPU-94's architecture: the engine already has the right faders. Just surface the master fader as a host-automatable parameter labeled "Mix" or "Output". The "Dry/Wet" framing is foreign to the SPU model anyway.

### 8.2 Bypass

JUCE provides `getBypassParameter()` -- override to return a bool parameter that the plugin uses to short-circuit `processBlock`. Hosts are *supposed* to call `processBlockBypassed` when bypassed, but Logic/Reaper/Bitwig often don't -- they either stop calling processBlock entirely or pass audio through. Best practice: implement BOTH (the bypass param crossfade inside processBlock AND processBlockBypassed) and assume nothing.

**Click-free bypass:** the parameter is a bool, but the *internal* transition should be a short crossfade (5-20 ms) between dry and wet. JUCE forum is full of plugins that get this wrong -- the click is universally hated.

**Sources:** [Current state of bypass management (JUCE forum)](https://forum.juce.com/t/current-state-of-bypass-management/54662). HIGH confidence on the problem; MEDIUM on best-practice consensus.

### 8.3 Tail Length

`AudioProcessor::getTailLengthSeconds()` tells the host how long the reverb will continue to ring after input goes silent. Critical for offline render (host won't truncate the audio early). Reverbs typically return a value based on current params -- for SPU-94, the longest preset (Space) has a tail of ~5-8 seconds at maximum vIIR. Safe blanket value: return 10.0 seconds, or compute from current vIIR + comb gains. JUCE supports returning `std::numeric_limits<double>::infinity()` for "infinite tail" -- useful for freeze/sustain modes but probably not v1.7.

**Sources:** [getTailLengthSeconds (JUCE)](https://forum.juce.com/t/gettaillengthseconds/16564). HIGH confidence.

---

## Table-Stakes vs Nice-to-Have vs Luxury (Summary Matrix)

| Feature | Beta Tier | Implementation cost in JUCE |
|---|---|---|
| Host parameter automation (morph + key controls) | **Table stakes** | Easy |
| State save/load (DAW session reload) | **Table stakes** | Easy (reuse `.spu94` serializer) |
| Latency reporting via `setLatencySamples` | **Table stakes** | Trivial (one int) |
| Stereo + Mono->Stereo + Mono->Mono bus support | **Table stakes** | Easy |
| All host buffer sizes including weird ones | **Table stakes** | Easy if core is sample-accurate (it is) |
| Host any-SR support via SRC | **Table stakes** | Medium (SRC choice is its own design) |
| `getTailLengthSeconds` for offline render | **Table stakes** | Trivial |
| Plugin metadata (name, vendor, category) per format | **Table stakes** | Trivial |
| Bypass parameter + click-free crossfade | **Table stakes** | Easy |
| Factory presets in host menu (VST3/AU/CLAP) | **Table stakes** | Medium |
| User preset directory per OS | **Table stakes** | Easy (`getSpecialLocation`) |
| LV2 `.ttl` factory preset bundles | **Nice-to-have** | Medium (Turtle-format file generation) |
| Resizable plugin window | **Nice-to-have** | Medium (GUI refactor) |
| HiDPI scale factor honoring | **Nice-to-have** | Medium (paint() audit) |
| A/B parameter compare | **Nice-to-have** | Hard (JUCE doesn't provide it) |
| Plugin-level wet/dry separate from engine mixer | **Nice-to-have** | Easy |
| Tempo-synced morph LFO | **Luxury** | Medium (new feature; deferred) |
| Transport-aware freeze | **Luxury** | Medium (new feature; deferred) |
| Per-note modulation (CLAP polyphonic modulation) | **Luxury** | Out of scope (effect plugin, not synth) |
| Sidechain input | **Luxury** | Out of scope |
| Surround/Atmos channel configs | **Luxury** | Out of scope |
| MIDI-learn UI inside plugin | **Luxury** | Out of scope (host handles it) |

---

## Easy-in-JUCE vs Hard-in-JUCE

**Easy (JUCE does it for you):**
- Host parameter wiring across all 4 formats
- Buffer-size and SR handling at the framework level
- File-system paths via `getSpecialLocation`
- Generic parameter UI fallback
- Bypass parameter declaration (`getBypassParameter`)
- Format-specific metadata (Projucer/CMake handles the mapping)

**Medium (some glue code, but well-trodden):**
- State serialization (one entry point: `getStateInformation`)
- Latency reporting (one call: `setLatencySamples`)
- SRC wrapper (use `juce::ResamplingAudioSource` or `LagrangeInterpolator`)
- Factory preset arrays per format
- Resizable + HiDPI editor

**Hard (JUCE doesn't help, you write it):**
- A/B parameter compare without losing undo history
- Click-free bypass that works across Logic/Reaper/Bitwig's inconsistent bypass behavior
- LV2 `.ttl` factory preset bundle generation (no built-in JUCE tooling)
- Robust handling of host-side state-restoration ordering quirks (FL Studio sometimes calls setStateInformation before prepareToPlay)
- Dynamic latency changes that hosts actually re-negotiate (most don't)

---

## Flagged Unknowns

1. **Logic AU caching**: Logic Pro caches AU plugin metadata aggressively. Changing parameter count, bus configs, or even the plugin's display name across builds can leave Logic showing stale data until the user wipes `~/Library/Caches/AudioUnitCache` or runs `killall -9 AudioComponentRegistrar`. Beta testers on Logic will hit this. Mitigation: document the workaround in the beta install instructions.

2. **FL Studio VST3 state-restore timing**: FL Studio is known to call `setStateInformation` at unexpected times -- sometimes before `prepareToPlay`, sometimes mid-process. The state-load code must be robust to "we don't know the sample rate yet" conditions. Worth validating with FL Studio specifically during pluginval/uat.

3. **CLAP host coverage**: CLAP is supported by Bitwig (native), Reaper (via wrapper), and a handful of others as of 2026. The CLAP user base is small but growing. For beta, CLAP coverage is a "we ship the format because it's cheap with `clap-juce-extensions`" decision, not because of beta-user demand. Anthony's beta tester list determines whether anyone will actually try the CLAP build.

4. **Linux LV2 host quirks**: Ardour, Carla, Qtractor each have their own quirks around state save/restore. Worth running `lv2lint` and at least one real Ardour session save/reload during UAT.

5. **No verified data on Anthony's beta-tester DAW mix.** Common assumption: Reaper (cross-platform), Logic (Mac), Ableton Live (Mac+Win), FL Studio (Win). Bitwig and Studio One are also in the mix for some users. Requirements step should sanity-check this with Anthony.

---

## Sources

- [AudioProcessorParameter (JUCE)](https://docs.juce.com/master/classAudioProcessorParameter.html)
- [AudioProcessor (JUCE)](https://docs.juce.com/master/classAudioProcessor.html)
- [AudioProcessorEditor (JUCE)](https://docs.juce.com/master/classAudioProcessorEditor.html)
- [Adding plug-in parameters (JUCE tutorial)](https://docs.juce.com/master/tutorial_audio_parameter.html)
- [Configuring bus layouts (JUCE)](https://juce.com/tutorials/tutorial_audio_bus_layouts/)
- [VST3 plugType subcategories (Steinberg)](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/group__plugType.html)
- [LV2 State spec](https://lv2plug.in/ns/ext/state)
- [Programming LV2 Plugins (drobilla)](https://drobilla.net/files/guide.html)
- [CLAP plugin-features.h](https://github.com/free-audio/clap/blob/main/include/clap/plugin-features.h)
- [CLAP repo](https://github.com/free-audio/clap)
- [AU kAudioUnitType_Effect (Apple)](https://developer.apple.com/documentation/audiotoolbox/1584142-audio_unit_types/kaudiounittype_effect)
- [Audio Unit Development Fundamentals (Apple)](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/AudioUnitDevelopmentFundamentals/AudioUnitDevelopmentFundamentals.html)
- [Delay Compensation and setLatency (JUCE forum)](https://forum.juce.com/t/delay-compensation-and-setlatency/15936)
- [setLatencySamples (JUCE forum)](https://forum.juce.com/t/setlatencysamples/14507)
- [getTailLengthSeconds (JUCE forum)](https://forum.juce.com/t/gettaillengthseconds/16564)
- [Current state of bypass management (JUCE forum)](https://forum.juce.com/t/current-state-of-bypass-management/54662)
- [How to make A/B button without resetting undo (JUCE forum)](https://forum.juce.com/t/how-to-make-a-b-button-in-plugin-without-reseting-undo-history-with-apvts/54464)
- [Cross-platform presets (JUCE forum)](https://forum.juce.com/t/cross-platform-presets-any-ideas/6449)
- [Where should user presets be stored, sandboxed app (JUCE forum)](https://forum.juce.com/t/where-should-user-presets-be-stored-sandboxed-app/36416)
- [Resizable AudioProcessorEditor (JUCE forum)](https://forum.juce.com/t/resizable-audioprocessoreditor/44658)
- [Parameter smoothing methods (JUCE forum)](https://forum.juce.com/t/parameter-smoothing-methods-algorithms/14773)
- [isBusesLayoutSupported + AUVAL (JUCE forum)](https://forum.juce.com/t/isbuseslayoutsupported-auval-and-mono-mono-configuration-solved/34326)

---
*Feature research for: SPU-94 v1.7 DAW Plugin Port milestone*
*Researched: 2026-05-10*
