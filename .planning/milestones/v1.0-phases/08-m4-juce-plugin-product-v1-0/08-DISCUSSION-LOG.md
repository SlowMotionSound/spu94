# Phase 8: SPU-94 Standalone GUI — Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-25
**Phase:** 08 — SPU-94 Standalone GUI (product v1.0)
**Areas discussed:** Plugin format scope, Lever surface, Plugin-layer additions, Audio I/O scope, UI direction, Preset selector UX, Sample rate / bit depth handling

---

## Plugin Format Scope

Initial framing presented six gray areas including an implicit Reaper-as-reference-DAW assumption (carried over from prior REQUIREMENTS.md / ROADMAP.md framing).

| Iteration | Options presented | User response |
|-----------|-------------------|---------------|
| 1 | Multiple plugin format combinations targeting Reaper as reference DAW | "I also don't have reaper on this system. Thats a weird assumption to make (which daw I am using)" |
| 2 | After Anthony said "cross DAW plugin... standard format": VST3 + LV2 + Standalone trio (recommended), or trio + CLAP | "CLAP is fine too. Realistically I only need a standalone format for V1.0... to test sound files through the reverb, and to test the sound quality of the DAC modeling and ADPCM portions of the code" |
| 3 | Three options: standalone only / standalone + plugins anyway / standalone + plugins-phase-later | Confirmed: standalone tool needed so the reverb code can actually be heard, tested, and debugged. Plugin and DAW integration explicitly out of scope until that primary need is met. |

**Decision: D-01-A — v1.0 ships standalone only. No VST3 / LV2 / CLAP / AU.**

---

## Lever Surface (Named musical levers vs raw register sliders)

| Iteration | Options presented | User response |
|-----------|-------------------|---------------|
| 1 | "What knobs do you reach for first?" — open question | "can you tell me what is realistically accessible on the algorithm code level?" — pushback against jumping to lever design without inventory |
| 2 | Inventory of 35 registers grouped by modulation class with topology-inferred musical-role labels (e.g., "vIIR = decay knob", "vWALL = damping knob") | Pushback: parameter picking shouldn't happen before establishing what's actually available; topology inferences should not be presented as known facts. |
| 3 | Three paths: (1) raw sliders + named-lever curation in follow-up phase [recommended], (2) generate audio sweeps + then design levers, (3) trust topology inferences and proceed | "1." + "Thank you. That is more helpful." |

**Decision: D-01 — v1.0 ships all 18 viable SPU registers as raw labeled sliders. Named-lever curation deferred to a follow-up phase informed by listening evidence.**

---

## Plugin-Layer Additions

| Iteration | Options presented | User response |
|-----------|-------------------|---------------|
| 1 | Wet/Dry only [recommended] / Wet/Dry + pull other extras forward (Pre-Delay, HPF, Freeze, Tail mod) | "Yes Wet/Dry is useful... I am confused again though. the 'standard reverb plugin additions' you listed.... Won't they be exposed as raw sliders like you just said previously?" |
| 2 | Clarification: 18 raw sliders are SPU-native register controls; "plugin-layer additions" are NEW DSP outside libspu94 (Wet/Dry mixer, sample-delay buffer for Pre-Delay, HPF, Freeze toggle, LFO module) | Confirmed: Wet/Dry only for v1.0; v1.0 should not patch additional DSP onto the native reverb algorithm. Other plugin-layer additions deferred to later phases. |

**Decision: D-02 — Wet/Dry mix only. No other plugin-layer additions for v1.0.**

---

## Audio I/O Scope

| Options presented | User response |
|-------------------|---------------|
| 1. File load + realtime playback + file save [recommended] / 2. Same + live audio input / 3. Something else | "I don't really need file save/export right now. Just the ability to load a .wav, press play and hear it through the reverb." |

**Decision: D-03 — WAV file load + realtime playback only. No file save/export. No live audio input.**

---

## UI Direction

| Options presented | User response |
|-------------------|---------------|
| 1. JUCE stock components [recommended] / 2. Custom-painted UI | "1" |

**Decision: D-04 — JUCE stock look-and-feel.**

---

## Preset Selector UX

| Options presented | User response |
|-------------------|---------------|
| 1. Flat dropdown [recommended] / 2. Visible button row / 3. Categorized dropdown | "1" |

**Decision: D-05 — Flat 10-item dropdown.**

---

## Sample Rate / Bit Depth Handling

| Options presented | User response |
|-------------------|---------------|
| 1. Reject non-44.1 kHz with error [recommended] / 2. Auto-resample on load / 3. Play at native rate (pitch-shifted) | "Without touching the internal reverb code, just build a light wrapper that plays any sample rate + bit depth wave....then feeds it into the reverb algo/code?" |

**Decision: D-06 — Light I/O wrapper handles any-SR / any-bit-depth WAV input. SPU core unchanged. JUCE-side I/O layer adapts (bit-depth → int16, SR → 44.1 kHz, channel → stereo).**

---

## Claude's Discretion

Items where the user said "you decide" or that are planner-level details:

- Slider layout / grouping on the panel
- Knob/slider widget choice within JUCE stock components
- Specific JUCE interpolator for resampling
- Whether numeric values display next to sliders
- File picker UX
- JUCE built-in WAV I/O vs vendored dr_wav
- JUCE version pin (7.x or 8.x)
- Whether play auto-starts on file load or requires button press

---

## Deferred Ideas

(Captured in CONTEXT.md `<deferred>` section)

- Named musical lever curation (follow-up phase, informed by listening evidence)
- Plugin format support — VST3 / LV2 / CLAP / AU (follow-up phase)
- Plugin-layer DSP additions: Pre-Delay, Input HPF, Freeze, Tail modulation/LFO
- WAV file save/export
- Live audio input
- Custom UI / visual identity
- macOS / Windows builds
- License pick (MIT vs Apache-2.0)
