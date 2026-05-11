# Architecture: v1.7 DAW Plugin Port

**Domain:** Wrapping an existing 44.1 kHz / int16 bit-faithful C core (`libspu94`) as a JUCE `AudioProcessor`-based multi-format DAW plugin (VST3 + AU + LV2 + CLAP) across Linux / macOS / Windows. The C core is frozen; this is wrapper architecture only.
**Researched:** 2026-05-10
**Confidence:** HIGH on JUCE lifecycle / threading / state save (current `PluginProcessor.cpp` already implements this pattern correctly; v1.7 is mostly removing the WAV-loader scaffolding and adding SRC). HIGH on the build-layout question (CMake `juce_add_plugin` already drives the standalone; FORMATS list expansion is one-line). MEDIUM-HIGH on the SRC engineering decision (well-trodden territory; several viable libraries, tradeoff axis is clear). MEDIUM on CLAP path (no first-party JUCE 8 support; `clap-juce-extensions` is the standard route).

---

## 0. The Starting Position (what we are NOT building)

The "standalone GUI" referenced in PROJECT.md is already a JUCE `AudioProcessor` + `AudioProcessorEditor` pair in `src/standalone/`, built via `juce_add_plugin(... FORMATS Standalone)`. The existing structure:

- `PluginProcessor.{h,cpp}` -- the `juce::AudioProcessor` subclass. ~836 LOC across both files. Already implements: opaque-handle SPU lifecycle in `prepareToPlay`/`releaseResources`; lock-free GUI<->audio register handoff via `RegisterBridge` (an SPSC queue); double-buffered WAV upload (message thread fills slot, audio thread swaps); morph engine with snap/glide paths; mixer + DAC + ADPCM toggles via `std::atomic`s; preset save/load via deferred audio-thread apply.
- `PluginEditor.{h,cpp}`, `MorphPanel`, `RegisterPanel`, `ParameterBridge` -- the GUI. Already a JUCE `Component` tree that attaches to the processor through the bridge.
- `WavLoader.{h,cpp}` -- a sidecar for the standalone's "play this file" feature. This is the **only** part that does NOT belong in the plugin processor: in a DAW, the host provides the audio buffer.

**Implication for the milestone:** v1.7 is fundamentally three additions and one subtraction on top of an already-working JUCE `AudioProcessor`:
1. **Subtract**: gate / move the WAV-loader and playback bookkeeping out of the plugin processor (keep it for the standalone target only).
2. **Add**: float<->int16 conversion at the `processBlock` boundary.
3. **Add**: bidirectional 44.1 kHz <-> host-SR sample rate conversion.
4. **Add**: real `getStateInformation` / `setStateInformation` (today both are empty stubs in `PluginProcessor.cpp:587-595` -- this is the v1.4 preset serializer plumbed into the JUCE state hooks).

Plus, in CMake, change `FORMATS Standalone` to `FORMATS Standalone VST3 LV2 CLAP` (and `AU` on Apple) -- JUCE generates per-format binaries from the same `AudioProcessor` source. That single line drives the 10-binary matrix.

This research file is structured around those four wrapper concerns plus the cross-cutting "shared vs format-specific" / build-layout questions.

---

## 1. JUCE `AudioProcessor` Lifecycle

### 1.1 The three-call contract

```
prepareToPlay(sampleRate, samplesPerBlock)   // host calls before audio starts; may recall
processBlock(buffer, midi)                    // called on the audio thread at every block
releaseResources()                            // host calls when audio stops; may recall
```

**Guarantees from JUCE:**
- `prepareToPlay` is called on the message thread before audio starts. Allocation, JUCE object construction, and `setLatencySamples` are appropriate here. ([JUCE docs](https://docs.juce.com/master/classAudioProcessor.html))
- `processBlock` is called on the **audio thread**. JUCE explicitly warns: no `malloc`, no locks, no logging, no `File` I/O, no Component/GUI calls. This is the same real-time rulebook the C core already follows.
- The block size passed to `prepareToPlay` is an **upper bound**, not a contract. `processBlock` may receive any size from 1 to that bound. The current `PluginProcessor.cpp:329-336` uses a `kMaxBlock = 4096` ceiling and `jassert`s on overrun -- this is the right pattern; v1.7 keeps it.
- Sample rate from `prepareToPlay` is the **host's** rate, not the internal 44.1 kHz. v1.7 stashes both: `hostSampleRate` (from JUCE) and the fixed `kCoreSampleRate = 44100.0` (the core's frozen internal rate).

### 1.2 Threading model in the existing processor

The current code is a clean instance of the JUCE-canonical pattern. Three thread classes touch the processor:

| Thread | What it does | Sync mechanism into audio thread |
|---|---|---|
| **Audio thread** | `processBlock`, all `spu94_*` mutation and processing | n/a (this is the consumer) |
| **Message thread** | GUI slider drag, button click, file dialog, `setStateInformation` | `std::atomic` for scalars (mixer, toggles, morph position); SPSC queue for register writes (`RegisterBridge`); double-buffered slot + `std::atomic<bool> ready` for bulk uploads (WAV, presets) |
| **Background threads** | None (no audio file decoding; the WAV loader runs synchronously on the message thread, then hands off) | -- |

For v1.7, the plugin version of the processor will additionally have:
- The **resampler state** lives on the audio thread only -- allocated in `prepareToPlay`, freed in `releaseResources`. Never touched from the message thread.
- The float<->int16 scratch buffers live on the audio thread only (preferably as `juce::HeapBlock` allocated in `prepareToPlay`, not stack arrays -- see §3.4).

### 1.3 RT-safety enforcement of the C core through the wrapper

The C core is already RT-safe (verified by `tests/rt_safety/`: no heap, no locks, no syscalls). The wrapper must **not regress** that. Concrete rules:

- Never call `spu94_init`, `spu94_destroy`, or `spu94_reset` from `processBlock` -- those happen in `prepareToPlay`/`releaseResources` only. The current code already follows this.
- Register writes from the audio thread go through `spu94_set_*` setters or the engine-layer `spu94_set_reg_*` family, both of which are RT-safe by construction (Phase 2 ADRs).
- The SRC library chosen in v1.7 MUST be heap-free / lock-free / syscall-free in its `process()` path. This is a hard filter on SRC library choice (§2).

---

## 2. Sample Rate Conversion -- the single biggest architectural choice

Host can be any of 44.1 / 48 / 88.2 / 96 / 176.4 / 192 / 384 kHz, plus arbitrary non-standard rates if the user picks one. The core runs at 44.1 kHz only. The wrapper SRCs both directions per `processBlock`.

### 2.1 The ratios that matter

| Host rate | Ratio (host : 44.1k) | Character |
|---|---|---|
| 44.1 kHz | 1 : 1 | **Bypass path** -- no SRC, just float<->int16 |
| 48 kHz | 160 : 147 | Classic ugly fractional. Most common DAW project rate. |
| 88.2 kHz | 2 : 1 | Integer ratio -- half-band trivially usable |
| 96 kHz | 320 : 147 | Ugly fractional. Common pro session rate. |
| 176.4 kHz | 4 : 1 | Integer |
| 192 kHz | 640 : 147 | Ugly fractional |

**The 48 / 96 / 192 case is where SRC engineering effort goes.** 44.1 / 88.2 / 176.4 are essentially free.

### 2.2 SRC library options

| Option | Quality | CPU | Latency | RT-safe? | Linear phase? | Verdict |
|---|---|---|---|---|---|---|
| **JUCE `LinearInterpolator`** | Bad | Tiny | 1 sample | Yes | No | **No** -- audible aliasing on a reverb with HF content. |
| **JUCE `CatmullRomInterpolator`** | OK for audio | Small | ~2 samples | Yes | No | Marginal. Spec-accurate reverb doesn't need this kind of compromise. |
| **JUCE `LagrangeInterpolator`** | Decent | Small | order/2 samples | Yes | Approximately | Used widely for variable-rate playback (pitch shifters). Not designed for fixed fractional ratio SRC in a quality-first context. ([forum thread](https://forum.juce.com/t/lagrangeinterpolator-with-fractional-ratio/32457)) |
| **JUCE `WindowedSincInterpolator`** | Good | Medium | Configurable | Yes | Approximately | The best of JUCE's built-ins. Less mature than dedicated SRC libraries. |
| **libsoxr** | Very high | High | Configurable (high in HQ mode) | Yes (no malloc in `process`) | Yes | Battle-tested in SoX. C library, easy to link. License: LGPL-2.1 (linking constraint -- static link forces LGPL exposure of object code, or use dynamic linking). |
| **libsamplerate (Secret Rabbit Code)** | Very high | High | ~500 samples in best mode | Yes (process is lock-free) | Yes (in SINC modes) | The reference for desktop pro audio SRC. License: BSD-2-Clause (clean for MIT/Apache final license pick). ([juce_libsamplerate](https://github.com/talaviram/juce_libsamplerate)) |
| **r8brain-free-src** | Very high | Medium | Hundreds-to-thousands of samples; reducible by relaxing `ReqAtten`/`ReqTransBand` | Yes (no malloc in `process`) | Yes (default) | Used by Voxengo's commercial SRC. Header-only C++. License: MIT. Latency depends on quality settings -- "minimum-phase mode" available in PRO version but the free version is linear-phase only. ([avaneev/r8brain-free-src](https://github.com/avaneev/r8brain-free-src)) |
| **zita-resampler** | Very high | Low-medium | Low (~100 samples typical) | Yes | Yes | Used in Ardour and qjackctl. Specifically designed for low-latency live SRC. License: GPL-3.0 -- **disqualified** by SPU-94's licensing posture (project deliberately keeps GPL dependencies out). |

**Recommendation: libsamplerate ("SRC")** as the default. Rationale:
- BSD-2-Clause licence is compatible with both MIT and Apache (deferred license pick is not blocked).
- Used by Audacity, Ardour, many commercial plugins. The "Sinc-Best" / "Sinc-Medium" / "Sinc-Fastest" / "Zero-Order-Hold" / "Linear" preset axis lets the user (or a hidden Quality menu) trade CPU for SNR. Sinc-Medium gives >120 dB SNR and is usually plenty for music.
- The `src_callback_*` API is the natural fit for streaming/block-based audio. No allocation in the hot path once `src_new()` returns.
- Latency reportable to the host: SRC's `src_set_ratio` / `src_callback_read` model has known group delay per quality setting -- specifically, Sinc-Medium = 121 samples one-way at the higher rate. The wrapper reports this via `setLatencySamples` (§2.5).

**Fallback if libsamplerate licensing/build friction arises:** r8brain-free-src is MIT and header-only. Linker-wise it is the easiest possible drop-in; its only downside vs SRC is higher latency in the linear-phase configuration.

**Bypass-fast-path:** When `hostSampleRate == 44100.0` exactly, skip SRC entirely -- the float<->int16 path runs alone. This is the cleanest case and the one the developer's own test sessions will run in.

### 2.3 Polyphase vs streaming

For fixed ratios known at `prepareToPlay` time (the host's SR doesn't change mid-stream except via `prepareToPlay` recall), **polyphase is the right architecture**: precompute the filter banks once for the specific 147/160 (or whatever) ratio, then per-sample work is just a tap-by-tap MAC of one polyphase row. libsamplerate, libsoxr, and r8brain all do this internally. The wrapper does NOT need to implement polyphase itself; it just calls the library.

### 2.4 The "ugly fractional" problem (147/160)

At 48 kHz host, every 160 host samples produce 147 core samples (and conversely 147 core samples produce 160 host samples). Per `processBlock`:

```
processBlock(host_samples_in[N]):
    n_core = src_callback_read(downsampler, ratio=44100/48000, N_max_core, scratch_in)
    spu94_process(state, scratch_in_L, scratch_in_R, scratch_out_L, scratch_out_R, n_core)
    src_callback_read(upsampler, ratio=48000/44100, N, scratch_out -> host_out)
```

The number of core samples per host block is **not constant** -- it fluctuates by ±1 around the average (147/160 × N). The wrapper must:
- Size the core scratch buffers for the worst case: `ceil(N_max * 44100 / 48000) + safety_margin`. A safety margin of 8 samples is generous.
- NOT assume the count is fixed. The C core's `spu94_process` already accepts any N >= 1 (per its public API doc), so this is fine.

### 2.5 Latency reporting (PDC)

Plugin delay compensation matters because DAWs use the plugin's `getLatencySamples()` to align the plugin's output with other tracks. The total reportable latency is:

```
latency_samples = SRC_downsampler_group_delay (in host samples)
                + spu94_get_total_latency_samples(state)  // FIR + ADPCM + DAC, in 44.1k samples
                + SRC_upsampler_group_delay (in host samples)
```

The middle term is already reported by `libspu94`'s `spu94_get_total_latency_samples`. The SRC contributions depend on library + quality setting:
- libsamplerate Sinc-Medium: ~121 samples one-way at the higher of the two rates. Round-trip ~242 samples at 48 kHz = ~5 ms. Acceptable.
- libsamplerate Sinc-Best: higher (~300+ samples one-way). Still acceptable for a reverb.

`setLatencySamples(total)` is called from `prepareToPlay` once the host SR is known. Reaper / Live / Logic all honor this. Reports indicate some hosts ignore PDC for certain routing topologies ([JUCE forum](https://forum.juce.com/t/audioprocessorgraph-pdc-and-changing-latency/18121)) -- this is a host bug, not a plugin issue. Documenting "57 + (varies) sample latency" in the README is sufficient.

### 2.6 SRC quality user surface

**Question deferred to requirements:** does the user see a "SR Quality" menu, or is it baked? Recommended baked choice = libsamplerate Sinc-Medium, with no user knob. Reasoning: this is a reverb plugin, not a SRC plugin; the user shouldn't be optimizing CPU vs SRC quality, and Sinc-Medium is high enough that no listener will hear it relative to the reverb signal itself. If CPU profiling reveals it dominates, expose the knob in a v1.8.

---

## 3. Bit-depth conversion (float32 ↔ int16)

### 3.1 The hot question: does dither defeat the core's character?

The core's character lives in the **44.1 kHz Q15 reverb network's** truncation/saturation. The float-to-int16 step **at the input boundary** is upstream of all that character. It doesn't compete with the core's quantization-as-feature; it just sets the int16 representation of the host's float signal.

**At input (float32 → int16):** the host signal arrives as float32 in [-1.0, 1.0] (nominal). Three policies:
1. **Truncate**: `int16_t(sample * 32768.0f)` with clamp. Cheap. Adds a sample-correlated quantization noise around -90 dBFS. The core then processes that as its 16-bit input -- and quantization noise that small is well below anything the reverb itself contributes.
2. **TPDF dither**: add triangular-PDF random noise of ~1 LSB peak before truncation. Decorrelates the quantization noise. Adds ~3 dB more total noise floor but it's spectrally white and uncorrelated with signal.
3. **Noise-shaped dither**: pushes quantization noise into HF where it's psychoacoustically less audible.

For SPU-94 the recommended choice is **TPDF or no dither**, with a documented decision. Argument for no dither: the core's authentic experience is already a 16-bit input stage; period-faithful sources arrived at the SPU via int16 already-quantized PCM. Adding dither at the wrapper boundary is a modern courtesy that the original hardware never had. Argument for TPDF: a 24-bit/float source going into a 16-bit converter has audible truncation distortion on quiet passages; TPDF eliminates that audibility cost at 3 dB of broadband noise. Both arguments are defensible; **lean: no dither, document the decision, expose a hidden compile-time flag if anyone reports artifacts**.

**At output (int16 → float32):** trivial. `float(sample) * (1.0f / 32768.0f)`. No dither needed -- expanding from 16-bit to float can't add quantization error.

### 3.2 Headroom / clipping

The current standalone (PluginProcessor.cpp:343-349) reads from the WAV at int16 verbatim with no level matching, and there's an `inputLevel` atomic (default 0.5) that scales by 0x7FFF in the `spu94_set_input_gain` setter. **In the plugin, the host can send full-scale +1.0 float signals which would clip immediately to +0x7FFF on conversion.**

Recommended input policy for the plugin:
- Convert `clamp(sample * 32768.0f, -32768.0f, 32767.0f)`. Clamp explicitly -- don't let the cast wrap.
- The existing `input_gain` knob (Q15) provides user-facing headroom control. Default it to 0.5 (–6 dB) for the plugin to match the standalone's behavior; pro users will set 1.0 if they want.
- A "soft-clip-on-overage" mode (tanh or polynomial) is a creative knob, NOT a v1.7 requirement; the core's own hard-clip behavior (vIIR / saturation on store) is part of the sound.

There's already a side-channel limiter at PluginProcessor.cpp:357-377 on the output. Keep it for the plugin path; the constants (`kSideKnee = 0.125`, `kSideCeiling = 0.06`) are a v1.6 design choice that shouldn't change in v1.7.

---

## 4. Block size handling

### 4.1 The C core's block contract

From `include/spu94/spu94.h:316-332`: `spu94_process` accepts any `num_samples >= 1`, including 0 (no-op) and 1 (single sample). Internally it walks at 44.1 kHz one host-rate sample at a time, calling `spu94_fir_chain_step` (which time-multiplexes the 22.05 kHz reverb network). So there is **no requirement for any specific input block size**. The wrapper does NOT need a ring buffer between itself and the core for block-size adaptation.

### 4.2 What COULD force a ring buffer

The only thing that forces a buffer between wrapper and core is the SRC step's "input samples per output samples" non-integer ratio. The libsamplerate `src_callback_read` API handles this internally: it pulls input samples on demand and produces an integer number of output samples per call. So a separate ring buffer is unnecessary -- libsamplerate IS the ring buffer.

### 4.3 Block-size budget

With kMaxBlock = 4096 host samples at 192 kHz, the core scratch needs `4096 × 44100/192000 + 8 = 949 samples` per channel. With kMaxBlock = 4096 at 44.1 kHz (bypass), still 4096. So pre-allocate two pairs of 4096-sample int16 scratches in `prepareToPlay` and we're done.

Current code uses **stack-allocated** scratch arrays (`int16_t tmpL_in[kMaxBlock]` -- PluginProcessor.cpp:337). 4 × 4096 × 2 bytes = 32 KiB on the audio-thread stack. This works today but is fragile -- some hosts run audio threads with small stacks (8-64 KiB is common). For v1.7 the **safer pattern is `juce::HeapBlock<int16_t>` allocated in `prepareToPlay`**, freed in `releaseResources`. Same RT-safety; no stack pressure.

---

## 5. Parameter management

### 5.1 The existing approach is intentional and good

The current `PluginProcessor` uses **raw `std::atomic<float>` / `std::atomic<bool>` / `std::atomic<int>` members + an SPSC queue (`RegisterBridge`) + a deferred-apply mechanism for bulk preset loads**. It does NOT use `AudioProcessorValueTreeState` (APVTS).

Tradeoffs of the two approaches:

| | Raw atomics + custom bridge (current) | APVTS |
|---|---|---|
| Host automation | Manual: declare `AudioProcessorParameter`s and route them to atomics | Automatic |
| Undo/redo in host | Manual | Automatic |
| Save/restore | Manual (writes our own XML) | Automatic XML |
| GUI binding | Manual | `SliderAttachment`/`ButtonAttachment` helpers |
| Granularity | Anything (we have 35 SPU registers + 6 faders + 5 toggles + morph + slots) | Each `AudioProcessorParameter` is one continuous float; coarser mapping |
| Performance | Hand-tuned; no parameter-tree overhead | Slight per-param overhead, usually irrelevant |
| Fit for SPU's int16-register model | Excellent (sliders write Q15 values via the engine setter API) | Awkward (each param needs a normalized-to-Q15 map) |

**Recommendation for v1.7: keep the current design.** Reasons:
- The 35 SPU registers don't naturally map to host-automatable parameters (DAWs would choke on a 35-parameter list of cryptic register names). The user-facing automatable parameters should be a curated, smaller set.
- The existing morph engine + waypoint slots are the v1.6 musical surface; that already provides a user-meaningful parameter (morph position 0-1).
- We don't need APVTS's XML save/restore -- we already have `spu94_preset_save`/`spu94_preset_load` for that.

**What v1.7 does add:** a small **`juce::AudioProcessorParameter` (or, equivalently, an APVTS) surface for HOST-AUTOMATABLE parameters**, separate from the GUI bridge. Recommended initial set (exact set is a requirements-phase decision):
- `morph_position` (float 0-1) -- the headline automatable
- `input_gain` (float 0-1)
- `dry_level`, `patina_level`, `reverb_level`, `dry_send`, `patina_send` (5x float 0-1)
- `dac_enabled` (bool)
- `morph_speed` (float 0-1)

These would be implemented as `juce::AudioParameterFloat` etc., registered in the constructor, with their `valueChanged` callbacks pushing into the existing atomics. That way the rest of the architecture is unchanged; we just gain host automation on the curated subset.

### 5.2 Parameter smoothing

The audio thread reads atomics with `memory_order_relaxed` and writes them directly to the C core's setters every `processBlock`. The C core's mixer setters (`spu94_set_input_gain` etc.) snap immediately without smoothing -- that's a v1.2 design choice (D-06). For fast automation on `morph_position`, the morph engine already has Snap/Glide via the `morphSpeed` knob (PluginProcessor.cpp:255-308); no further smoothing in the wrapper is required.

For the mixer faders specifically (the new automatable parameters), if zipper noise appears on fast automation, the cheapest mitigation is a `juce::SmoothedValue` per fader in the wrapper, sampled per-sample and applied as part of the audio thread's per-block setter writes. This is a follow-up if needed, not a v1.7 prerequisite.

### 5.3 Mapping JUCE parameters to SPU registers

The flow is already established:

```
GUI slider -- (message thread) --> std::atomic<float>
                                        |
                                        v (audio thread reads per processBlock)
                                   convert to Q15 (× 0x7FFF)
                                        |
                                        v
                                   spu94_set_input_gain(state, q15_value)
                                        |
                                        v (core's internal write-policy decides
                                            IMMEDIATE vs TICK_LATCHED per register)
```

This is the standard "atomic + read per block" pattern and is already correct.

---

## 6. State save/restore (`getStateInformation` / `setStateInformation`)

Currently empty stubs at PluginProcessor.cpp:587-595. The implementation is straightforward because v1.4 already shipped `spu94_preset_save`/`spu94_preset_load` for human-readable text serialization of all 46 engine fields.

**Recommended implementation:**

```cpp
void getStateInformation(juce::MemoryBlock& destData) override {
    char buf[SPU94_PRESET_BUF_SIZE];
    int n = spu94_preset_save(engines[0],
                              "DAW Session State",
                              "SPU-94 plugin state, autosaved by host",
                              buf, sizeof(buf));
    if (n > 0) destData.append(buf, (size_t)n);
}

void setStateInformation(const void* data, int size) override {
    // Defer to audio thread via the same pendingPresetBuf mechanism
    // that loadPresetFromString already uses (PluginProcessor.cpp:428-440).
    if (size <= 0 || (size_t)size >= sizeof(pendingPresetBuf)) return;
    std::memcpy(pendingPresetBuf.data(), data, (size_t)size);
    pendingPresetBuf[size] = '\0';
    pendingPresetLen.store((size_t)size, std::memory_order_relaxed);
    pendingTargetSlot.store(-1, std::memory_order_relaxed);
    filePresetReady.store(true, std::memory_order_release);
}
```

**Format choice: text (.spu94)**, not XML or binary. Three reasons:
1. The .spu94 serializer already exists, is tested, and round-trips bit-identically.
2. Plain text in `getStateInformation` is human-readable to anyone who dumps the DAW session and inspects the chunk -- a debugging gift.
3. Forward compatibility: missing keys retain the engine's current value (D-08 in v1.4); unknown keys are silently ignored (D-09). That means a v1.7 plugin loading a v1.8 session works, and a v1.8 plugin loading a v1.7 session works. The forward-compat story is already won by the v1.4 format.

**The host wraps this in its own chunked container per format** (VST3 has its own state framing; AU uses CFPropertyList; LV2 uses Turtle; CLAP uses a raw byte chunk). JUCE handles all of that wrapping automatically; the plugin only sees `getStateInformation(MemoryBlock&)` and `setStateInformation(const void*, int)`.

---

## 7. Shared vs format-specific code

### 7.1 The model JUCE gives us

JUCE generates one binary per format from a single `juce_add_plugin` target. The `AudioProcessor` source code is shared 100%; per-format wrappers are generated by JUCE itself (under `JUCE/modules/juce_audio_plugin_client/`). We write zero format-specific code for VST3, AU, and LV2.

For CLAP, JUCE 8 does not natively ship CLAP. The community-standard route is **`clap-juce-extensions`** ([free-audio/clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)), a drop-in that adds a CLAP target to `juce_add_plugin` (so `FORMATS Standalone VST3 LV2 CLAP AU` works). JUCE 9 is planned to land first-party CLAP support per the [JUCE Q3 2024 roadmap](https://juce.com/blog/juce-roadmap-update-q3-2024/), but waiting for JUCE 9 is unnecessary; the extension is mature and used by AudioThing, Surge XT, and Chowdhury DSP plugins among others.

### 7.2 What bleeds through per-format

A few format idiosyncrasies need wrapper-level acknowledgment:
- **AU bus configuration**: Logic / GarageBand expect a specific bus layout call sequence. JUCE handles this transparently if `BusesProperties().withInput(...).withOutput(...)` is used (already done in PluginProcessor.cpp:9-11). `isBusesLayoutSupported` may need to be implemented if we ever care about mono/sidechain (we currently don't -- stereo-in / stereo-out is the only supported layout).
- **VST3 inactive-state behavior**: VST3 hosts may call `processBlock` with `bypass` enabled or with parameter updates during the disabled state. The current code handles this correctly because state management runs unconditionally and the gating is via `wavSource.loaded/playing` -- but the **plugin** path won't have those gates; processing always runs from host audio.
- **LV2 worker thread**: For long-running operations LV2 has a worker-thread extension. We don't need it -- preset save/load is already deferred to the audio thread via the existing message<->audio handoff.
- **CLAP threading**: CLAP has more granular thread-pool semantics than VST3. clap-juce-extensions abstracts this; we use JUCE's threading model and it Just Works.
- **CLAP automation**: CLAP exposes per-sample-accurate parameter automation. If we want to take advantage, we'd need to opt in via clap-juce-extensions. v1.7 can skip this (host-block-granular automation is fine for a reverb).

### 7.3 wrapperType detection

Some plugins want to behave slightly differently in standalone vs hosted vs offline modes. JUCE exposes `juce::PluginHostType` and `AudioProcessor::wrapperType`. We have one such case already: in standalone, the WAV-loader/playback UI is shown; in hosted, it should be hidden. This is a `PluginEditor` concern, gated by `if (processor.wrapperType == AudioProcessor::wrapperType_Standalone)`. **One small if-statement, not a separate processor class.**

---

## 8. Build layout

### 8.1 Recommended CMake structure

Single `juce_add_plugin` target with `FORMATS` listing every format we want. CMake generates one binary per format automatically.

```cmake
# src/standalone/CMakeLists.txt  (rename folder to src/plugin/ in v1.7?)

if(APPLE)
    set(SPU94_FORMATS Standalone VST3 LV2 CLAP AU)
else()
    set(SPU94_FORMATS Standalone VST3 LV2 CLAP)
endif()

juce_add_plugin(spu94_plugin
    PRODUCT_NAME                "SPU-94"
    COMPANY_NAME                "SPU-94 Project"
    BUNDLE_ID                   "com.spu94project.spu94"
    PLUGIN_NAME                 "SPU-94"
    PLUGIN_MANUFACTURER_CODE    Spu9
    PLUGIN_CODE                 Spv1   # might need different codes per format on AU
    FORMATS                     ${SPU94_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx|Reverb
    LV2URI                      "https://spu94project.org/spu94"
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_SYNTH                    FALSE
)

# CLAP via extension (only if CLAP is in SPU94_FORMATS):
if("CLAP" IN_LIST SPU94_FORMATS)
    # FetchContent the clap-juce-extensions repo, then:
    clap_juce_extensions_plugin(TARGET spu94_plugin
                                CLAP_ID "com.spu94project.spu94"
                                CLAP_FEATURES audio-effect reverb)
endif()

target_sources(spu94_plugin PRIVATE ... )    # same .cpp list as today, minus WavLoader
target_link_libraries(spu94_plugin PRIVATE
    spu94_static
    juce::juce_audio_utils
    SampleRate::samplerate           # libsamplerate
    PUBLIC juce::juce_recommended_*
)
```

JUCE will produce, per host OS:

- Linux: `SPU-94.vst3`, `SPU-94.lv2`, `SPU-94.clap`, `SPU-94` (standalone binary)
- macOS: `SPU-94.vst3`, `SPU-94.component` (AU), `SPU-94.clap`, `SPU-94.app` (standalone), `SPU-94.lv2`
- Windows: `SPU-94.vst3`, `SPU-94.clap`, `SPU-94.exe` (standalone) -- LV2 is technically buildable on Windows but barely used; we may drop it from the Windows matrix and keep the official count at 10 binaries.

### 8.2 The C core as static library

`libspu94` builds as a static library (`spu94_static` target in `src/spu94/CMakeLists.txt`). Each format binary statically links `spu94_static`. This is already the case today for the standalone target -- no changes needed.

**Implication for binary size:** the C core is ~8.2k LOC of C compiled with `-Os` or `-O3` is probably under 200 KiB of object code. JUCE adds a few MB per format binary. So each `.vst3` / `.clap` / `.component` is on the order of 5-15 MB, dominated by JUCE.

### 8.3 The standalone vs plugin source split

Today everything lives in `src/standalone/`. The cleanest v1.7 split:

```
src/
├── spu94/            (the C core, unchanged)
├── plugin/           (shared AudioProcessor + Editor + Panels + ParameterBridge,
│                      reformulated from current src/standalone/, minus WAV loader)
└── standalone_extra/ (just WavLoader.{cpp,h} -- the only standalone-only code)
```

The `juce_add_plugin(... FORMATS Standalone VST3 ...)` target uses both `plugin/` and `standalone_extra/`. The standalone wrapper enables the WAV-loader UI conditionally based on `wrapperType == wrapperType_Standalone`.

This keeps the v1.6 standalone shipping with no behavioral change while adding the four new wrappers.

### 8.4 CI / per-OS toolchain matrix

GitHub Actions matrix: `{linux-ubuntu-22.04, macos-13, windows-2022}` × `{Release}`. Each runner:
- Linux: gcc 11+, JUCE 8 deps (libasound2-dev, libjack-jackd2-dev, libxinerama-dev, ...). `pluginval` runs on the VST3 output. `lv2lint` runs on the LV2 output.
- macOS: Xcode 15+. `auval -v aufx Spv1 Spu9` runs on the .component (AU). `pluginval` runs on the VST3.
- Windows: VS 2022 with C++17 toolchain. `pluginval` runs on the VST3.

Code signing / notarization is deferred per the v1.7 locked decisions; this is a CI-credentials question, not an architectural one.

---

## 9. Architecture summary diagram

```
+---------------------------------------------------------------+
|  DAW host (Reaper / Live / Logic / FL / Bitwig / Ardour...)   |
|  +-- per-format wrapper boilerplate generated by JUCE         |
|       (VST3 SDK / AU CoreAudio / LV2 / clap-juce-extensions)  |
+--------+------------------+------------------+----------------+
         |                  |                  |
         v (audio thread)   v (message thread) v (state save/load)
+--------+------------------+------------------+----------------+
|                    juce::AudioProcessor                       |
|                    (src/plugin/PluginProcessor)               |
|                                                               |
|  processBlock(host_float_buf, host_SR, host_block_size):      |
|    1. host_float_buf  --(× 32768, clamp)--> int16_in[host_N]  |
|    2. int16_in        --(libsamplerate /->44.1k)--> int16_core_in[core_N] |
|    3. spu94_process(engine, core_in_L,R, core_out_L,R, core_N)|
|    4. int16_core_out  --(libsamplerate /->host_SR)--> int16_host_out[host_N] |
|    5. int16_host_out  --(/ 32768.0f)--> host_float_buf        |
|                                                               |
|  prepareToPlay(host_SR, host_block_size):                     |
|    - alloc state buffers, work buffers, src state, scratch    |
|    - setLatencySamples(57 + ADPCM_lat + DAC_lat + 2*SRC_lat)  |
|                                                               |
|  getStateInformation / setStateInformation:                   |
|    - wrap spu94_preset_save / spu94_preset_load over MemoryBlock |
|                                                               |
|  getParameter / setParameter (host automation surface):       |
|    - {morph_position, input_gain, 5×mixer, dac_enabled,       |
|       morph_speed} as juce::AudioParameterFloat / Bool        |
|    - each writes to existing std::atomic<float/bool>          |
+---------------------------------------------------------------+
         |
         v (audio thread only)
+--------+------------------------------------------------------+
|                      libspu94 (C core)                        |
|                     spu94_process(int16 @ 44.1k)              |
|              -- FROZEN; v1.7 changes nothing here --          |
+---------------------------------------------------------------+
```

---

## 10. Pitfalls and gotchas (flagged for PITFALLS-v1.7 / requirements)

- **Reaper master tempo and offline render.** Reaper supports "offline render" where the audio thread is the message thread. Our atomics still work, but if SRC libraries silently allocate during initialization of a different SR, that allocation may now be on what JUCE considers the audio thread. Mitigation: ensure SRC `prepareToPlay` (with the offline SR) is on the message thread; verify by running pluginval at multiple sample rates.
- **Logic AU validation cache.** macOS caches AU plugin metadata aggressively. Bumping `PLUGIN_CODE` or `PLUGIN_MANUFACTURER_CODE` between builds invalidates the cache; users will need to delete `~/Library/Caches/AudioUnitCache/` if they re-install over a bad earlier build during beta. Document in the beta README.
- **pluginval strict-thread checks.** pluginval's `--strict` mode catches some real-time-safety violations (locks taken in `processBlock`, etc.). Run with `--strict-level 10` in CI. Our current code is clean -- the C core is verified rt-safe and the wrapper uses only atomics + lock-free queues -- but adding libsamplerate is new code; verify with valgrind/Helgrind locally before trusting CI.
- **JUCE 8 → 9 upgrade for CLAP.** If we want first-party CLAP eventually, the clap-juce-extensions path is throwaway. Designing the parameter surface to map cleanly onto either is mostly free (avoid CLAP-specific features like per-sample automation in v1.7).
- **Sample rate change mid-session.** Some DAWs (Ardour, occasionally Live) recall `prepareToPlay` with a new SR. Our SRC needs to gracefully rebuild for the new ratio. libsamplerate's `src_delete` + `src_new` does this; just make sure they're called in `releaseResources`/`prepareToPlay`, not from the audio thread.
- **The 22.05 kHz core internal rate is invisible to the wrapper.** The wrapper SRCs to/from 44.1 kHz only; the core handles its own 44.1↔22.05 internally via the 39-tap half-band FIR. This is critical to internalize: do NOT try to SRC straight from host-SR to 22.05 kHz in the wrapper -- that bypasses the core's bit-faithful half-band FIR which is part of the sound.

---

## Sources

- JUCE 8 `AudioProcessor` reference -- [docs.juce.com/master/classAudioProcessor](https://docs.juce.com/master/classAudioProcessor.html). HIGH confidence.
- JUCE Q3 2024 roadmap (CLAP coming in JUCE 9) -- [juce.com blog](https://juce.com/blog/juce-roadmap-update-q3-2024/). HIGH.
- `clap-juce-extensions` -- [github.com/free-audio/clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions). HIGH (community-standard).
- libsamplerate / Secret Rabbit Code -- via [talaviram/juce_libsamplerate](https://github.com/talaviram/juce_libsamplerate). MEDIUM-HIGH (existence + license verified; quality claims from project README).
- r8brain-free-src -- [github.com/avaneev/r8brain-free-src](https://github.com/avaneev/r8brain-free-src). MEDIUM (latency claims sampled from project README + KVR forum threads, not benchmarked locally).
- "Low latency sample rate conversion" -- [JUCE forum](https://forum.juce.com/t/low-latency-sample-rate-conversion/54524). MEDIUM (community discussion, useful for tradeoff axis).
- LagrangeInterpolator with fractional ratio -- [JUCE forum](https://forum.juce.com/t/lagrangeinterpolator-with-fractional-ratio/32457). MEDIUM (cites built-in interpolator limitations).
- Plugin delay compensation / setLatencySamples -- [JUCE forum thread on AudioProcessorGraph PDC](https://forum.juce.com/t/audioprocessorgraph-pdc-and-changing-latency/18121). MEDIUM (community + JUCE staff replies).
- Existing project source: `src/standalone/PluginProcessor.{h,cpp}` (836 LOC), `include/spu94/spu94.h` (public C API), `src/standalone/CMakeLists.txt` (current `juce_add_plugin` target with `FORMATS Standalone`). HIGH (read in full).

---

*Architecture research for v1.7 DAW Plugin Port. Researched 2026-05-10 against current main at 6816b16. Author: gsd-project-researcher agent (architecture lane).*
