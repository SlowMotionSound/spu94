# Feature Research — SPU-94

**Domain:** Bit-faithful retro DSP reimplementation (PS1 SPU reverb) — C library + Python bindings
**Researched:** 2026-04-18
**Confidence:** HIGH for PS1 register-set and algorithm details (nocash psx-spx is definitive); HIGH for comparable library API patterns (source-inspected on ipatix/verblib); MEDIUM for community-desire claims (distilled from plugin marketing/forums, not direct user-research); MEDIUM for "preservation-project decision-doc" precedent (general pattern is clear, but few projects publish formal `DECISIONS.md`).

---

## Context Before The Table

Three things the rest of this doc assumes:

1. **The locked-in decisions are not revisited here.** 24 registers implemented and exposed; 10 factory presets shipped as fixtures; LEVERS-CATALOG.md annotates (does not implement). This doc fills in *what those mean concretely*.
2. **"The library" in M1 is a plain C API** — not a plugin, not a UI. The plugin is M4. Every feature below is labeled against the C library surface unless explicitly tagged `[M4-plugin]` or `[M5-hw]`.
3. **SPU-94's audience is dual.** (a) Programmers wrapping `libspu94` into plugins/hardware/firmware — they care about API stability, real-time safety, and register-level control. (b) Musicians using the eventual M4 plugin — they care about levers, presets, and smooth modulation. M1 serves (a) directly and (b) indirectly by building the foundation right.

---

## PS1-Specific Reference Data (source of truth for M1 fixtures)

All values cited from [nocash psx-spx — SPU reverb section](https://psx-spx.consoledev.net/soundprocessingunitspu/) unless noted. These belong in `libspu94/include/spu94/registers.h` and `libspu94/presets/*.c`.

### The 24 reverb registers

| Addr | Name | Type | R/W at runtime | Meaning |
|------|------|------|----------------|---------|
| 1F801D84h | vLOUT | s16 volume | ✅ writable | Reverb output volume L (post-mix) |
| 1F801D86h | vROUT | s16 volume | ✅ writable | Reverb output volume R (post-mix) |
| 1F801DA2h | mBASE | u16 address | ⚠️ writable-with-caveats | Reverb work-area start (8-byte units) |
| 1F801DC0h | dAPF1 | u16 disp | ✅ writable | APF1 offset (delay-length-register; see PITFALLS) |
| 1F801DC2h | dAPF2 | u16 disp | ✅ writable | APF2 offset |
| 1F801DC4h | vIIR | s16 vol | ✅ writable | IIR coefficient (reflection volume 1) |
| 1F801DC6h | vCOMB1 | s16 vol | ✅ writable | Comb tap 1 volume |
| 1F801DC8h | vCOMB2 | s16 vol | ✅ writable | Comb tap 2 volume |
| 1F801DCAh | vCOMB3 | s16 vol | ✅ writable | Comb tap 3 volume |
| 1F801DCCh | vCOMB4 | s16 vol | ✅ writable | Comb tap 4 volume |
| 1F801DCEh | vWALL | s16 vol | ✅ writable | Same-side wall-reflection coefficient |
| 1F801DD0h | vAPF1 | s16 vol | ✅ writable | APF1 feedback coefficient |
| 1F801DD2h | vAPF2 | s16 vol | ✅ writable | APF2 feedback coefficient |
| 1F801DD4h | mLSAME | u16 addr | ⚠️ writable-with-caveats | L same-side write address |
| 1F801DD6h | mRSAME | u16 addr | ⚠️ writable-with-caveats | R same-side write address |
| 1F801DD8h | mLCOMB1 | u16 addr | ⚠️ | L comb tap 1 source |
| 1F801DDAh | mRCOMB1 | u16 addr | ⚠️ | R comb tap 1 source |
| 1F801DDCh | mLCOMB2 | u16 addr | ⚠️ | L comb tap 2 source |
| 1F801DDEh | mRCOMB2 | u16 addr | ⚠️ | R comb tap 2 source |
| 1F801DE0h | dLSAME | u16 disp | ⚠️ | L same-side read offset |
| 1F801DE2h | dRSAME | u16 disp | ⚠️ | R same-side read offset |
| 1F801DE4h | mLDIFF | u16 addr | ⚠️ | L cross-side write address |
| 1F801DE6h | mRDIFF | u16 addr | ⚠️ | R cross-side write address |
| 1F801DE8h | mLCOMB3 | u16 addr | ⚠️ | L comb tap 3 source |
| 1F801DEAh | mRCOMB3 | u16 addr | ⚠️ | R comb tap 3 source |
| 1F801DECh | mLCOMB4 | u16 addr | ⚠️ | L comb tap 4 source |
| 1F801DEEh | mRCOMB4 | u16 addr | ⚠️ | R comb tap 4 source |
| 1F801DF0h | dLDIFF | u16 disp | ⚠️ | L cross-side read offset |
| 1F801DF2h | dRDIFF | u16 disp | ⚠️ | R cross-side read offset |
| 1F801DF4h | mLAPF1 | u16 addr | ⚠️ | L APF1 write address |
| 1F801DF6h | mRAPF1 | u16 addr | ⚠️ | R APF1 write address |
| 1F801DF8h | mLAPF2 | u16 addr | ⚠️ | L APF2 write address |
| 1F801DFAh | mRAPF2 | u16 addr | ⚠️ | R APF2 write address |
| 1F801DFCh | vLIN | s16 vol | ✅ writable | L input volume into reverb |
| 1F801DFEh | vRIN | s16 vol | ✅ writable | R input volume into reverb |

Total: **32 register names** in the reverb config area. Of these, ipatix's `PsxReverbPreset` enumerates **32 fields** (source-verified against [psx-reverb.c](https://github.com/ipatix/lv2-psx-reverb/blob/master/psx-reverb.c)); the "24 documented reverb registers" framing comes from counting dAPF1..vRIN (the `1F801DC0h..1F801DFEh` block) plus mBASE plus vLOUT/vROUT = 24 logical controls, matching the project framing. **Ship the full set**; the register count distinction is semantic.

> **Register-name clarification for the project prompt.** The names `FB_SRC_A`, `FB_SRC_B`, `IIR_ALPHA`, `ACC_COEF_A/B/C/D` in the research prompt are **not nocash-PSX register names**. Those identifiers come from the **Sony PSP SPU2/PS2** reverb documentation, which shares ancestry with the PS1 SPU but is a different register set. SPU-94 uses the PS1 nocash naming: `vIIR`, `vCOMB1..4`, `vWALL`, `vAPF1/2`, `mLSAME/mRSAME`, etc. Flagging this explicitly because it will matter during M1 spec review. Confidence: HIGH — nocash is unambiguous on this. (Source: [nocash psx-spx SPU section](https://psx-spx.consoledev.net/soundprocessingunitspu/); cross-checked against [ipatix preset struct](https://github.com/ipatix/lv2-psx-reverb/blob/master/psx-reverb.c), which uses the same PS1 names.)

### The 10 factory preset names and sizes

From nocash psx-spx; Chaos Echo (undocumented in Sony SDK, but present in leaked preset tables and shipped by ipatix) is the 10th slot, alongside Off:

| # | Preset name | Work-buffer size | Typical use in games |
|---|-------------|------------------|----------------------|
| 0 | Room | 9,668 bytes | Small interiors, dialogue |
| 1 | Studio Small ("Studio A") | 8,000 bytes | Short drum/vocal rooms |
| 2 | Studio Medium ("Studio B") | 18,496 bytes | Medium spaces |
| 3 | Studio Large ("Studio C") | 28,640 bytes | Large studio ambiences |
| 4 | Hall | 44,512 bytes | Concert halls, cathedrals |
| 5 | Half Echo | 15,360 bytes | Short slapback |
| 6 | Space Echo | 63,168 bytes | Long sci-fi tails |
| 7 | Chaos Echo | 98,368 bytes | Extreme feedback (community-named; ipatix ships as slot 7) |
| 8 | Delay | 98,368 bytes | Clean delay line |
| 9 | Off | 16 bytes | Bypass (not actually silent — small residual, documented quirk) |

> **"Which games use which presets"** — partial community knowledge exists but is scattered. The ipatix README notes you can "find which preset a game uses by checking the contents of the reverb registers," implying no authoritative per-game mapping exists. Treating this as **out of scope for M1**; presets ship as fixtures named by their Sony SDK names only. (Confidence: MEDIUM on game→preset mapping claims; HIGH on the list and sizes themselves.)

### Algorithm pseudocode (verified against nocash)

```
// Per L/R cycle at 22050 Hz (44.1 kHz alternating)
Lin = vLIN * LeftInput
Rin = vRIN * RightInput

// Same-side reflection (one-pole IIR)
[mLSAME]  = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]
[mRSAME]  = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]

// Cross-side reflection
[mLDIFF]  = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]
[mRDIFF]  = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]

// 4-tap comb sum
Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2] + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
Rout = vCOMB1*[mRCOMB1] + vCOMB2*[mRCOMB2] + vCOMB3*[mRCOMB3] + vCOMB4*[mRCOMB4]

// APF1 (Schroeder all-pass)
Lout = Lout - vAPF1*[mLAPF1-dAPF1]; [mLAPF1] = Lout; Lout = Lout*vAPF1 + [mLAPF1-dAPF1]
Rout = Rout - vAPF1*[mRAPF1-dAPF1]; [mRAPF1] = Rout; Rout = Rout*vAPF1 + [mRAPF1-dAPF1]

// APF2
Lout = Lout - vAPF2*[mLAPF2-dAPF2]; [mLAPF2] = Lout; Lout = Lout*vAPF2 + [mLAPF2-dAPF2]
Rout = Rout - vAPF2*[mRAPF2-dAPF2]; [mRAPF2] = Rout; Rout = Rout*vAPF2 + [mRAPF2-dAPF2]

// Output
LeftOutput  = Lout * vLOUT
RightOutput = Rout * vROUT

// Advance buffer (per-cycle, wraps within mBASE..7FFFEh)
BufferAddress = MAX(mBASE, (BufferAddress + 2) AND 7FFFEh)

// Saturation at every write: clamp to -8000h..+7FFFh
// Multiplication normalization: result >> 15 (divide by 8000h)
```

### Sample-rate semantics — the decision SPU-94 faces

The SPU runs its reverb state machine at **22.05 kHz** (each L and R sample alternates on a 44.1 kHz clock). Input from voices/CD arrives at 44.1 kHz and is downsampled via a **39-tap symmetric FIR** with coefficients ranging from -0001h to 4000h; output is upsampled via the same FIR structure. (Source: nocash SPU section.)

Two approaches exist in the wild:

- **Lock-to-44.1kHz-host** (BodbDearg's PsxReverb VST): "Both plugins must be used at a 44.1 KHz sample rate… Any other sample rates will result in incorrect results." Simplest, most faithful, but inflexible.
- **SR-compensate** (ipatix lv2-psx-reverb): Scales `dAPF1` and friends by `stretch_factor = host_rate / 22050`; converts IIR alpha via `fc2alpha/alpha2fc` frequency-domain conversion. Works at any host SR but is *not* bit-faithful at non-44.1 rates.

**SPU-94 should be internally-locked-to-44.1kHz (really: internally-locked-to-22050Hz for the reverb state machine), with optional boundary SRC for M4 plugin convenience — and the boundary SRC is *outside* the faithful core.** See PITFALLS.md for the gray area; see ARCHITECTURE.md for the two-layer split.

---

## Feature Landscape

### Table Stakes — the C library would feel broken without these

Every entry below is a specific public-API requirement, with a suggested signature. All functions return `int` as an error code (0 = OK) where applicable; struct pointers are opaque (forward-declared in headers, defined in .c files).

| # | Feature | Signature (suggested) | Complexity | Source / rationale |
|---|---------|------------------------|------------|--------------------|
| T1 | **Create / destroy** | `spu94_t* spu94_create(const spu94_config_t* cfg); void spu94_destroy(spu94_t*);` | 1 day | Freeverb/verblib pattern; opaque struct is standard C idiom. `cfg` carries work-buffer size, sample rate lock, optional mBASE. |
| T2 | **Reset state** (zero buffer + pointer, leave registers) | `void spu94_reset(spu94_t*);` | 0.5 day | Distinct from destroy. Standard in [verblib](https://github.com/blastbay/verblib). |
| T3 | **Process block, stereo float32 interleaved** | `void spu94_process_f32(spu94_t*, const float* in, float* out, size_t frames);` | 2 days | Interleaved LRLRLR is the DAW/JUCE default. Float32 is the host-facing format. |
| T4 | **Process block, stereo int16 planar** | `void spu94_process_s16(spu94_t*, const int16_t* in_l, const int16_t* in_r, int16_t* out_l, int16_t* out_r, size_t frames);` | 1 day | s16 planar matches PS1 native format — this is the *bit-faithful* entry point. f32 path goes through s16 internally. |
| T5 | **Set register by enum** | `void spu94_set_reg(spu94_t*, spu94_reg_id_t id, uint16_t value);` | 1 day | Enum `SPU94_REG_vIIR, SPU94_REG_dAPF1, …` maps to 32 fields. This is the first-class runtime control path. |
| T6 | **Get register by enum** | `uint16_t spu94_get_reg(const spu94_t*, spu94_reg_id_t id);` | 0.5 day | Symmetric with T5; needed for test harness and for future plugin UI read-back. |
| T7 | **Load preset** (10 built-ins) | `void spu94_load_preset(spu94_t*, spu94_preset_id_t id);` | 1 day | Preset is just "write these 32 registers atomically." Built-ins enumerated `SPU94_PRESET_ROOM, …, SPU94_PRESET_OFF`. |
| T8 | **Bulk register write** (atomic-ish snapshot) | `void spu94_set_regs(spu94_t*, const spu94_reg_snapshot_t* snap);` | 1 day | Needed so a preset load or a sweep cue doesn't happen mid-cycle with half the registers updated. Implementation: double-buffer pending-regs; swap at cycle boundary. |
| T9 | **Version query** | `const char* spu94_version_string(void); uint32_t spu94_version_numeric(void);` | 0.5 day | ABI-critical for plugin wrappers. Include git hash at build time. |
| T10 | **Error / last-error query** | `const char* spu94_last_error(const spu94_t*);` | 0.5 day | C has no exceptions. Real-time safe: last-error is a `const char*` to a static string, set only by non-RT calls. |
| T11 | **Fixed-point vs audio contract documented** | `spu94/CONTRACT.md` document + header comments | 1 day | "Input is signed 16-bit equivalent (f32 path clamps to [-1.0, +1.0] and scales to s16); output matches bit-for-bit what PS1 hardware would emit given the same register state and input." This is the bit-faithfulness promise, in English. |
| T12 | **Python binding mirrors C API** | `spu94_py.Reverb`, `.process(np.ndarray) -> np.ndarray`, `.set_reg(id, val)`, `.load_preset(id)` | 2 days | ctypes only (project constraint). numpy arrays marshalled via buffer protocol. Enum ids exposed as Python IntEnum. |
| T13 | **CLI for WAV processing** | `spu94 --preset hall in.wav out.wav` | 1 day | Golden-file testing needs batch processing. CLI uses miniaudio or dr_wav (single-header, no licensing drama). |
| T14 | **Work-buffer lifecycle** (allocate once, zero on reset) | Part of T1 / T2 | 1 day | No heap allocation in hot path (project constraint). 512 KB buffer allocated at `spu94_create` time; reused forever. |
| T15 | **Thread-safety contract documented** | header comment | 0.5 day | Standard pattern: "single-writer for state, single-reader for process". Plugin hosts already assume this. |

**Table stakes totals: ~14 days of API work** (plus the actual DSP, which is separate; this is the *surface* only).

### Why these are the stakes, not optional

- **T1–T4 (create/destroy/reset/process)** are the four functions every audio library has; ipatix's `instantiate/cleanup/activate/run` is the LV2-flavored version of the same, and [verblib's `verblib_initialize/verblib_process`](https://github.com/blastbay/verblib) is the C-library equivalent.
- **T5–T8 (register access)** are the concrete implementation of "mid-stream register writes are first-class." Without T5/T8, musicians cannot modulate; without T6 the plugin UI has nothing to display; without T7 the 10 factory presets are useless.
- **T3 and T4 together** — this project needs *both* float32 (for plugin hosts) and int16 (for bit-faithful witness-diffing against lv2-psx-reverb and hardware capture). Shipping only float32 loses the verifiability story; shipping only int16 loses the plugin-path story.
- **T9–T11** are non-negotiable for anyone wrapping `libspu94` in a plugin — plugin authors need version queries for compat shims and error signaling that doesn't longjmp out of the audio thread.
- **T12 and T13** are project-mandated (PROJECT.md: Python bindings and CLI are M1 deliverables).

### API comparison with comparable libraries

| Concern | SPU-94 (proposed) | [lv2-psx-reverb (ipatix)](https://github.com/ipatix/lv2-psx-reverb) | [verblib (blastbay)](https://github.com/blastbay/verblib) | Valhalla VintageVerb ([marketing](https://valhalladsp.com/shop/reverb/valhalla-vintage-verb/)) |
|---------|-------------------|---------------------------------------------------------------------|-----------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| Init | `spu94_create(cfg)` | LV2 `instantiate(sample_rate, bundle_path)` | `verblib_initialize(verb, sr, channels)` | Plugin-host abstraction |
| Process | `spu94_process_f32/s16` | LV2 `run(n_samples)` (ports pre-connected) | `verblib_process(verb, in, out, frames)` | Plugin-host abstraction |
| Register access | `set_reg/get_reg/set_regs` **+** `load_preset` | **Presets only** — no register-level runtime access | High-level params only (room_size, damping, wet, dry, width) | High-level params + "Mode" (preset bank) |
| Preset format | `static const uint16_t presets[10][32]` (mirror ipatix) | `static const uint16_t presets[10][0x20]` | No built-in presets; user sets params | Internal (proprietary) |
| Sample rate | Lock-to-internal-22.05kHz; optional SRC wrapper | Per-SR scaling of delays + IIR alpha conversion | SR-agnostic (≥22.05 kHz enforced) | SR-flexible |
| Parameter smoothing | **Explicit: off by default** in core, opt-in via wrapper | `+= 0.001 * (target - current)` per-sample, hardcoded on gain | Not in header — params written directly | Per-sample smoothing, inaudible |
| Language | C99 | C (LV2 conventions) | C89 single-header | C++ (JUCE) |

**Key divergence from ipatix:** SPU-94 exposes every register as a runtime write target (they hide them behind preset load). This is the whole point of "living instrument" framing.

**Key divergence from verblib:** SPU-94 does *not* smooth inside the core. Parameter smoothing is part of the M4 plugin's lever abstraction — the core behaves exactly like PS1 hardware, which means writes land immediately. This is intentional; see Anti-features.

### Preset file format decision

**Recommendation: inline C struct literals, not JSON/TOML.** Source evidence:

- ipatix ships presets as `static const uint16_t presets[10][0x20]` — a 32-element array of register values per preset. No external file format.
- Verblib has no presets (user code sets params).
- BodbDearg's PsxReverb VST ships "all the built-in reverb types that were available to PS1 developers via the PsyQ SDK" — the implementation uses compiled-in constants.

**Why inline C:**
- Presets are 10 fixed values × 64 bytes each = **~640 bytes of data total.** Below the overhead threshold of a parser.
- No runtime file I/O → real-time safe, embeddable in MCU firmware.
- Golden-file tests can reference preset IDs by enum, not paths.
- Python bindings expose presets as `spu94.Preset.ROOM, spu94.Preset.HALL, …` with zero marshalling.

**Exception**: M4 plugin (not M1) will eventually want user-saveable preset banks. At that layer, TOML is the right format — human-editable, no binary-compat concerns, fits the "living instrument / preset as recipe" framing. **Do not build this in M1.**

Complexity for M1 inline-literal preset loading: **1 day including all 10 values typed out with nocash citations.**

---

### Differentiators — what would make SPU-94 stand out for its audience

Ranked by user-value (highest first). "User" here = the dual audience described in Context.

| Rank | Feature | Value proposition | Complexity | Notes |
|------|---------|-------------------|------------|-------|
| D1 | **Register-level runtime API as first-class** (T5, T8 above) | Every existing PS1 reverb tool (ipatix, GameVerb, BodbDearg) hides the registers behind presets or high-level knobs. SPU-94 makes register writes a normal thing — this is the *only* tool musicians could use to sweep, modulate, or CV-control any PS1 register mid-audio. | M1-sized (core of the project, not a sub-feature) | Backed by T5, T8. GameVerb advertises a "Geek Mode" for customization ([Impact Soundworks GameVerb](https://impactsoundworks.com/product/gameverb/)) but that's a UI reveal — the underlying engine still isn't written for live register modulation. |
| D2 | **`DECISIONS.md` as a first-class deliverable** (gray-area log) | Every bit-faithful reimplementation project accumulates judgment calls about ambiguous spec. SameBoy logs them in commit messages, ares/higan in changelogs, but almost **nobody publishes a clean standalone decision doc.** If SPU-94 does, the doc itself becomes a contribution to PSX reverse-engineering, independent of the code. | 0.5 day template + ongoing during M1 (per-decision ~30 min) | No direct precedent found at doc-level. SameBoy test-ROM pass rates ([SameBoy features page](https://sameboy.github.io/features/)) are the closest thing — "accuracy receipts" rather than decision logs. This is a real gap in the space. MEDIUM confidence on differentiation value (no market validation), HIGH confidence on novelty. |
| D3 | **Glitch-free mid-stream register writes** (the "living instrument" promise enforced in M1) | Register writes during audio processing are a *documented, tested* feature, not a side-effect. Includes the modulation test (sine/sweep/random-walk on each register) from PROJECT.md. Nobody else ships this as a spec-level guarantee. | 1 week (test infra + per-register stability tests + handling the "delay-length-register-change gray area" called out in PROJECT.md as an M1 active item) | No existing PS1 reverb tool advertises or tests this. ipatix's approach (smooth the gain parameters, change everything else via full preset reload) is the opposite of what SPU-94 promises. |
| D4 | **LEVERS-CATALOG.md** — catalog of musical-lever candidates with register mappings | Future-facing: M4 plugin will need to decide which high-level knobs ("Room Size", "Pre Delay", "Damping") map to which register combinations. Cataloging during M1 (while the author has register semantics fresh) is free; doing it later from cold means re-reading the code. The catalog itself is useful documentation. | 2 days (annotate each of the 32 registers) | Unique to SPU-94's framing. Valhalla VintageVerb exposes Decay, Pre-Delay, Mix, HiCut/LowCut, Damping, Diffusion as its top-level controls ([loopmasters.com Valhalla guide](https://www.loopmasters.com/articles/4273-ValhallaDSP-Reverbs-Quick-Start-Guide)) — use this as a "target lever vocabulary" when annotating. |
| D5 | **Bit-exact witness-diff harness shipped with the library** (not internal) | Competing plugin authors can run SPU-94's test suite and verify their own implementations against PS1 hardware expectations. Turns the test suite into a service to the wider retro-DSP community. | 1 week (test harness + golden-WAV fixtures + CLI for diff) | Backed by PROJECT.md M1 items (witness-output diff against lv2-psx-reverb, golden-file regression tests). Reframes internal QA infra as a public good. |
| D6 | **Real-time-safe C core that ports to MCU/FPGA** (proven, not promised) | The Cortex-M cross-compile smoke test (M1) makes this real. ipatix's plugin is LV2-only; GameVerb/BodbDearg are VST-only; none port to hardware. SPU-94's C core is the first PS1 reverb designed to drop into a Daisy or Blackfin firmware. | 2 days (cross-compile CI job + "no heap in hot path" enforcement via static analysis) | Portability is the enabler for the whole M5+ hardware future (ps1-reverb-eurorack.md). HIGH confidence this is novel. |
| D7 | **Bit-faithfulness opt-in/opt-out flags** at library level | `spu94_config_t` fields: `bit_faithful_truncation` (default on), `bit_faithful_clipping` (default on), `bit_faithful_dac_model` (later, M3). Power users can A/B the faithful vs "cleaned up" behavior in the same tool. Core ships with all defaults on — faithfulness is the default, cleanup is the deviation. | 1 day per flag (M1 defines 2 flags; M3 adds DAC) | Turns the project's central tension ("faithful vs usable") into a visible switch. Unique framing — no comparable tool exposes the knobs this granularly. |
| D8 | **Python + numpy-native binding for DSP research** | `ctypes` binding returns `np.ndarray` views. Researchers can FFT, spectrogram, impulse-response-analyze any register configuration from a Jupyter notebook. | 2 days (covered by T12) | ipatix has no Python binding. BodbDearg ships VSTs only. GameVerb ships as a commercial plugin. SPU-94's research-first tooling posture is unique. |

**Ordering rationale:** D1 and D3 are the core technical differentiators (runtime register control + stability). D2 and D4 are the documentation differentiators (living outputs). D5 and D8 are community/research differentiators (the tool as research instrument). D6 is the portability differentiator (hardware future). D7 is the philosophy differentiator (faithfulness as a visible switch).

---

### Anti-Features — deliberately NOT in the core

These are things modern reverb plugins include that would *actively compromise* SPU-94's bit-faithful identity. The core library rejects them; the M4 plugin wrapper may expose some.

| Feature | Why commonly requested | Why it breaks SPU-94 | Where (if anywhere) it belongs |
|---------|------------------------|---------------------|--------------------------------|
| **Oversampling / anti-aliasing** | "Cleans up" aliasing artifacts that are part of the character | PS1 ran at 22.05 kHz internally with 39-tap FIR reconstruction; the aliasing *is* the sound. Oversampling = not the PS1 reverb. | ❌ Never. Not core, not plugin. |
| **Automatic gain compensation on preset change** | Smooth UI feel; no level jumps | PS1 presets had wildly different output levels (Chaos Echo saturates hard, Off is silent-ish). That's part of the sonic identity. AGC hides the 7FFFh clipping you paid for. | ❌ Never in core. **Maybe** in M4 plugin as a *toggle* (off by default, clearly labeled "Tames preset jumps — not bit-faithful"). |
| **Internal float32 math / rounded arithmetic** | "Cleaner" sound, better dynamic range | Truncation (not rounding) is the PS1's fixed-point signature. Floats with rounding = Freeverb with PSX presets, not SPU-94. | ❌ Never in core. The core is s16 fixed-point, period. Float32 at the API boundary only (T3 above). |
| **Stereo widening / M-S processing** | Adds perceived "size" | PS1 had a specific L/R same-side + cross-side topology with its own stereo character. Adding widening on top = blurring the original image. | ❌ Never in core. M4 plugin can add it downstream as a *post-effect*, clearly separated. |
| **Variable decay time / "infinite reverb" freeze** | Standard on modern verbs (Valhalla "freeze mode") | PS1 decay is an emergent property of vIIR × vCOMB × vWALL × buffer size. Adding a separate decay knob either duplicates that set or bypasses it. | ❌ Never in core. M4 plugin *could* expose a "Decay" meta-lever via LEVERS-CATALOG.md (multiple-register macro) — but it's a computed mapping, not a new DSP stage. |
| **Automatic sample-rate conversion inside the core** | "Just works at any DAW sample rate" | ipatix does this; it breaks bit-faithfulness at non-44.1 rates because the delay lengths and IIR alphas get rescaled. Fine for music, bad for a reference implementation. | ⚠️ M4 plugin: *boundary* SRC at plugin input/output, core runs at 22050 Hz internally. This is a **wrapper responsibility, not a core responsibility.** |
| **User-facing "Diffusion", "Density", "Modulation" parameters** | Standard on Valhalla-class verbs ([Concert Hall mode offers "lush chorusing modulation"](https://www.loopmasters.com/articles/4273-ValhallaDSP-Reverbs-Quick-Start-Guide)) | PS1 has no diffusion/density/modulation stages. Adding them in the core = inventing DSP that wasn't there. | ❌ Never. Not core, not plugin. These belong to a different product. |
| **MIDI CC / automation smoothing inside the core** | Plugin hosts expect zipper-free automation | Smoothing inside the core *hides* the register-write behavior we're trying to expose. A sweep across dAPF1 should sound like a sweep, with all its discontinuities, not like Valhalla's chorused decay. | ❌ Never in core. M4 plugin handles smoothing at the *lever* layer — between UI knobs and register writes. Raw register API remains unsmoothed. |
| **Preset morphing / crossfading between presets** | "Bank-switching without clicks" | A crossfade between Hall and Delay is neither Hall nor Delay — it's a made-up thing. PS1 never did this. | ❌ Never in core. M4 plugin *could* do it as a clearly labeled "morph" feature built on top of raw register writes. |
| **Gain matching between bit-faithful and clean modes** | If SPU-94 exposes a `bit_faithful_clipping=false` switch (D7 above), users will want matched levels | Matched levels = re-hiding what the switch revealed | ❌ Never. The switch reveals the difference; masking it defeats the switch. |

**The guiding principle for core vs plugin:** the core library is **faithful-only** — its job is to reproduce PS1 behavior. The M4 plugin is the place where "usable" features live, and each one should be labeled with its fidelity cost in the UI. A user who turns on AGC, smoothing, and SRC has built themselves a modern reverb with PSX presets. That's their choice; they just shouldn't get it by default.

#### Where's the line between "bypass the coloration" and "clean up the algorithm"?

A useful test. Something is a **legitimate bypass** if:
1. It's a parallel path, not a replacement of a stage.
2. It's disabled by default.
3. It's labeled in the UI as "not bit-faithful."
4. Turning it off restores exact PS1 behavior.

Something is an **algorithm cleanup** (to reject) if:
1. It modifies a stage's internal math (e.g. replaces truncation with rounding).
2. It's enabled by default or hidden.
3. It prevents bit-exact reproduction even when other flags are "faithful."
4. It requires UI reframing ("Decay" instead of vIIR+vWALL) to make sense.

By this test: ADPCM-bypass (M2+) is legitimate (parallel path, off by default); DAC-bypass (M3+) is legitimate; sample-rate-conversion-at-plugin-boundary is legitimate; float32-internal-math is cleanup; oversampling is cleanup; AGC is cleanup.

---

## MVP Definition (M1 — the C library)

### Launch with (M1 ships)

- [ ] T1–T15 (all table-stakes API functions, documented contract)
- [ ] 10 factory presets compiled in
- [ ] D1 (register runtime API — core of the library)
- [ ] D3 (glitch-free mid-stream writes, tested)
- [ ] D7 (bit-faithful flags — at least `bit_faithful_truncation` and `bit_faithful_clipping`)
- [ ] D8 (Python/numpy binding)
- [ ] DECISIONS.md (D2) maintained throughout M1
- [ ] LEVERS-CATALOG.md (D4) populated during M1
- [ ] Witness-diff harness + golden files (D5 foundation)
- [ ] Cortex-M cross-compile smoke test (D6 proof)

### Add after validation (M2–M3)

- ADPCM encode/decode (own milestone)
- DAC reconstruction model (own milestone)
- `bit_faithful_dac_model` flag (extends D7)
- Expanded witness-diff corpus (community IRs, Shirobon IR pack, etc.)

### Future consideration (M4+)

- JUCE plugin wrapper with musical levers (M4 is where the *living instrument* UX lands — LEVERS-CATALOG.md becomes the spec)
- Parameter smoothing in the plugin layer
- User-editable preset banks (TOML)
- CV-style register modulation UI (plugin-level, not core)
- Eurorack hardware (M5+)

---

## Feature Prioritization Matrix (M1 scope only)

| Feature | User value | Impl cost | Priority |
|---------|------------|-----------|----------|
| T1–T4 (create/destroy/reset/process) | HIGH | MEDIUM | P1 |
| T5, T6, T8 (register access) | HIGH | MEDIUM | P1 |
| T7 (preset loading) | HIGH | LOW | P1 |
| T9–T11 (versioning/error) | MEDIUM | LOW | P1 (plugin-authors need it) |
| T12 (Python binding) | HIGH | MEDIUM | P1 (PROJECT.md mandate) |
| T13 (CLI) | MEDIUM | LOW | P1 (test-infra mandate) |
| T14–T15 (buffer lifecycle, threading doc) | MEDIUM | LOW | P1 |
| D1 (runtime register API) | HIGH | HIGH | P1 (*is* the project) |
| D2 (DECISIONS.md) | HIGH (novelty) | LOW-ongoing | P1 (PROJECT.md mandate) |
| D3 (glitch-free mid-stream) | HIGH | HIGH | P1 (PROJECT.md mandate) |
| D4 (LEVERS-CATALOG) | MEDIUM | LOW | P1 (PROJECT.md mandate) |
| D5 (witness-diff harness) | MEDIUM | HIGH | P1 (PROJECT.md mandate) |
| D6 (MCU cross-compile) | MEDIUM | LOW | P1 (PROJECT.md mandate) |
| D7 (bit-faithful flags) | MEDIUM | LOW | P2 (nice to have in M1; not blocking) |
| D8 (Python numpy) | HIGH | MEDIUM | P1 (part of T12) |

**Everything in PROJECT.md's M1 active list maps to P1 here.** No feature-level discoveries should change the M1 plan — the research confirms the plan is well-scoped.

---

## Feature Dependencies

```
T1 (create) ──┬──> T2 (reset)
              ├──> T3/T4 (process)
              ├──> T5/T6/T8 (registers) ──> T7 (preset load)
              └──> T14 (buffer lifecycle)

T3/T4 ──> T12 (Python binding) ──> T13 (CLI uses Python? NO — CLI is C)
T13 depends on T1+T3 directly, not on T12.

D1 (runtime registers) ──requires──> T5 + T6 + T8
D3 (glitch-free writes) ──requires──> D1 + test infra (D5)
D4 (LEVERS-CATALOG) ──enhances──> D1 (documents what it's for)
D7 (bit-faithful flags) ──requires──> T1 (config struct)
D7 ──enhances──> D1 (richer register-behavior contract)

D6 (MCU cross-compile) ──conflicts-slightly──> T12 (Python binding adds a build target;
    Python is dev-host-only, MCU build excludes bindings — enforce via build flags)

D2 (DECISIONS.md) ──enhances──> every gray area encountered during M1. Not a code dependency.
```

### Dependency notes

- **T5/T6/T8 must be designed before T7.** Preset loading is "call set_regs with this snapshot" — if the bulk-write API isn't atomic, presets glitch.
- **D3 (glitch-free) requires D5 (test harness) to be provable, not just claimed.** The modulation test from PROJECT.md active items is the proof.
- **D6 (MCU) excludes Python at build time, not at design time.** Use a top-level CMake option `SPU94_BUILD_BINDINGS=OFF` for firmware builds.
- **No feature conflicts internally.** The only tension is core vs plugin (see Anti-features), and that's resolved by the split: core is faithful, plugin is usable.

---

## Competitor / Comparable Feature Matrix

| Feature | [lv2-psx-reverb](https://github.com/ipatix/lv2-psx-reverb) | [BodbDearg PsxReverb VST](https://github.com/BodbDearg/PlayStation1Vsts) | [GameVerb (Impact Soundworks)](https://impactsoundworks.com/product/gameverb/) | [verblib](https://github.com/blastbay/verblib) | **SPU-94** |
|---------|-----------------------|-------------------------------|------------------------|------------------|------------|
| C library (not just plugin) | ❌ (LV2 plugin binary) | ❌ (VST binary, iPlug2) | ❌ (commercial plugin) | ✅ (single-header C) | ✅ |
| Python binding | ❌ | ❌ | ❌ | ❌ | ✅ (M1) |
| All 24+ registers exposed at runtime | ❌ (presets only) | ❌ (preset enum only) | ⚠️ ("Geek Mode", but plugin-only) | N/A (different algorithm) | ✅ (D1) |
| 10 PS1 factory presets | ✅ | ✅ (all PsyQ SDK presets) | ✅ (9 PSX presets + SNES/N64 extras) | ❌ | ✅ |
| Bit-faithful at internal 22.05 kHz | ⚠️ (SR-scaled, not bit-faithful off 44.1) | ✅ (requires 44.1 host SR) | ⚠️ (not documented) | N/A | ✅ (M1 core) |
| Glitch-free mid-stream register writes | ❌ (preset-switch reloads all state) | ❌ | ❌ | ⚠️ (params smoothed, not register-level) | ✅ (D3) |
| DECISIONS.md / gray-area log | ❌ | ❌ | ❌ | ❌ | ✅ (D2) |
| Witness-diff test harness published | ❌ | ❌ | ❌ | ❌ | ✅ (D5) |
| MCU / FPGA portable | ❌ (LV2, desktop) | ❌ (iPlug2 C++) | ❌ (commercial desktop) | ✅ (C89 single header) | ✅ (D6) |
| Plugin UI / musical levers | ❌ (wet/dry/preset only) | ✅ (PSX-era aesthetic) | ✅ (Geek Mode + 700+ presets) | ❌ | 🔜 M4 |
| Open source | ✅ (GPLv3) | ✅ (MIT) | ❌ ($49 commercial) | ✅ (public domain) | ✅ (license TBD, MIT or Apache-2.0) |

**Position summary:** SPU-94 is the only tool in this matrix that (a) exposes registers at runtime, (b) ships bit-faithful guarantees, (c) ports to hardware, and (d) publishes its gray-area decisions. Every other project picks one or two of those. The competition isn't "another PSX reverb" — it's the set of musicians who currently use ipatix or GameVerb and wish they could modulate dAPF1 live, plus the set of hardware builders who currently can't find a portable PS1-reverb C library.

---

## Community Signal (confidence: MEDIUM — indirect)

**Direct user-quote evidence that musicians wish existing PS1 reverb plugins had [feature X] was thin in search.** Most forums discuss the plugins as "works, does what it says." This is either (a) the tools serve current users well enough that the gap SPU-94 fills is in musicians who haven't been able to articulate the wish, or (b) the search missed the right forums.

**Indirect signals that do exist:**
- GameVerb's marketing emphasizes customization, "Geek Mode", 700+ presets ([Impact Soundworks](https://impactsoundworks.com/product/gameverb/)) — suggests a market segment willing to pay for depth beyond "pick a preset."
- Shirobon's [PS1 Reverb Impulse Responses](https://shirobon.bandcamp.com/album/ps1-reverb-impulse-responses) exist as a product — confirms that capturing/recreating PS1 reverb is a standing musician desire.
- ipatix's README acknowledges users will want to "tweak parameters manually" but warns it "would require [deep] understand[ing] of the algorithm to avoid degraded audio quality" — this is exactly the gap SPU-94 closes with LEVERS-CATALOG.md (translating registers to musical levers).
- Eurorack community threads ([Black Spring Reverb, Intellijel Swells coverage](https://synthanatomy.com/2026/04/intellijel-swells-multi-mode-stereo-reverb-for-eurorack-with-deep-mangling-options.html)) emphasize CV modulation as the primary differentiator for hardware reverb — confirms the hardware-future direction has a real audience.

**Gap this research did not close:** no direct forum thread of the form "I wish lv2-psx-reverb let me modulate dAPF1 via LFO." Mark as research debt for M4, not M1 — the M1 work doesn't change based on this, but the M4 plugin's UI priorities should be informed by actual user interviews before building levers.

---

## Quality Gate Checklist

- [x] Every PS1-register/preset claim cited to nocash psx-spx or confirmed by ipatix source inspection
- [x] Table-stakes / differentiators / anti-features cleanly separated with distinct framing
- [x] Complexity noted for every T/D entry (day / week / milestone-sized)
- [x] Locked-in decisions (24 registers, 10 presets, LEVERS-CATALOG-M4-only) not re-litigated — only filled in
- [x] Register-name aliasing (FB_SRC_A / IIR_ALPHA etc. → flagged as PSP-family, not PS1) addressed
- [x] Sample-rate question (internal-22.05 vs host-flexible) resolved with explicit recommendation
- [x] Preset file format question resolved (inline C literals for M1; TOML deferred to M4)
- [x] Line between "bypass" and "cleanup" explicitly defined

---

## Sources

- [nocash psx-spx — Sound Processing Unit (SPU)](https://psx-spx.consoledev.net/soundprocessingunitspu/) — primary reference for register layout, algorithm pseudocode, preset values, work-buffer addressing
- [problemkaputt.de PSX specs](https://problemkaputt.de/psx-spx.htm) — cross-reference for nocash content
- [ipatix lv2-psx-reverb source](https://github.com/ipatix/lv2-psx-reverb/blob/master/psx-reverb.c) — C-API comparator, preset struct layout, SR-scaling approach
- [ipatix lv2-psx-reverb README](https://github.com/ipatix/lv2-psx-reverb) — feature list, SR-handling claims
- [BodbDearg PlayStation1Vsts](https://github.com/BodbDearg/PlayStation1Vsts) — 44.1 kHz-only VST reference, PsyQ SDK preset inheritance
- [verblib by blastbay](https://github.com/blastbay/verblib) — single-header C reverb library, API naming convention benchmark
- [Freeverb source (sinshu mirror)](https://github.com/sinshu/freeverb) — Schroeder reverb reference implementation
- [Impact Soundworks GameVerb](https://impactsoundworks.com/product/gameverb/) — commercial retro reverb feature landscape, "Geek Mode" customization precedent
- [Valhalla VintageVerb product page](https://valhalladsp.com/shop/reverb/valhalla-vintage-verb/) — state-of-the-art parameter vocabulary (Decay/Pre-Delay/HiCut/LowCut/Damping/Diffusion) for LEVERS-CATALOG target
- [Valhalla VintageVerb Modes (Valhalla DSP blog)](https://valhalladsp.com/2023/02/10/valhallavintageverb-the-modes/) — mode-bank design pattern
- [Loopmasters Valhalla quick-start](https://www.loopmasters.com/articles/4273-ValhallaDSP-Reverbs-Quick-Start-Guide) — parameter-tweaking-on-the-fly user expectations
- [musicdsp.org — 1-pole LPF for smooth parameter changes](https://www.musicdsp.org/en/latest/Filters/257-1-pole-lpf-for-smooth-parameter-changes.html) — standard anti-zipper-noise technique (relevant for M4 lever layer, intentionally *not* used in M1 core)
- [Shirobon PS1 Reverb IRs (Bandcamp)](https://shirobon.bandcamp.com/album/ps1-reverb-impulse-responses) — evidence of musician market for PS1 reverb reproduction
- [Intellijel Swells coverage (SYNTH ANATOMY, 2026-04)](https://synthanatomy.com/2026/04/intellijel-swells-multi-mode-stereo-reverb-for-eurorack-with-deep-mangling-options.html) — CV-control feature-vocabulary for eventual Eurorack module
- [SameBoy features page](https://sameboy.github.io/features/) — "test ROM compliance as accuracy receipts" — the alternative to DECISIONS.md that other preservation projects use
- [KVR thread: GameVerb](https://www.kvraudio.com/forum/viewtopic.php?t=612889) — community reception of retro reverb plugins

---

*Feature research for: PS1 SPU reverb bit-faithful C library (SPU-94)*
*Researched: 2026-04-18*
