# SPU-94XA — Input Module & Codec Effects Processor

**Status:** Brief — ready for milestone scoping when the time comes
**Date:** 2026-05-29

## What It Is

A Eurorack-style input module for the SPU-94 ecosystem. Live audio goes in, two time-aligned parallel outputs come out: a clean dry path and a codec-processed path. The user blends externally — we give them the building blocks, not the mix decision.

Think Cwejman AP-1 MK2, but instead of just conditioning levels, it runs audio through vintage digital compression algorithms as creative effects.

## Core Codec: PS1 XA-ADPCM

The native algorithm. This is the starting requirement — everything else layers on later. XA-ADPCM is Sony's streaming audio format from the PS1, used for music and voice playback off the CD-ROM. 4-bit ADPCM with interleaved sectors. We already have the ADPCM engine; XA extends it with the streaming codec's specific block format and characteristics.

This is the home sound of the module. The other codecs are expansion packs.

## Additional Codecs (Future, Each Requires Independent Research)

Each codec destroys signal differently. Each needs its own research spike to find the musically useful sweet spots before implementation.

- **MP3** — pre-echo artifacts, spectral smearing, psychoacoustic model exploitation
- **Bluetooth SBC** — thin brittle midrange, bandwidth limiting
- **GSM / cheap cell phone** — vocoder-like warble, linear predictive coding artifacts
- **Frequency extenders** — broadcast codec character (Comrex-style)
- **Others TBD** — any vintage or modern digital compression with interesting failure modes

## Creative Levers

These are ways to make the codecs malfunction musically — not random glitch, but controllable texture:

- **Bit overflow** — the "evaporate then sprinkle back" sound from the v1.5 register-shadow overflow. Anthony loved this. Re-enable as an opt-in control.
- **Wrong prediction filter forcing** — manually select the wrong ADPCM prediction filter for intentional artifacts. This lever should also be available in the sampler (SPU-94S) and reverb ADPCM coloration path (SPU-94R) — anywhere a prediction filter exists in the system.
- **Bitrate starvation** — push the codec below its designed operating range
- **Cross-codec chaining** — feed one codec's output into another
- **Per-codec creative parameters** — unique to each algorithm, discovered during research

## Envelope Follower

Any good Eurorack input module extracts control signals from the incoming audio. The XA module should include:

- **Envelope follower** — extract amplitude envelope from source audio
- **Gate extractor** — derive gate/trigger signals from transients

These control outputs could drive parameters on other SPU-94 modules (reverb depth, sampler pitch, etc.).

## Architecture Notes

- Separate product in the SPU-94 ecosystem, not a feature of SPU-94R
- Own window, own identity, own code feel
- Shares the ADPCM engine code with SPU-94R and SPU-94S
- Dry and codec outputs must be latency-matched for clean external blending
- The codec path is where all the DSP interest lives — the dry path is just a time-aligned passthrough

## Open Questions (For Milestone Scoping)

- How many codecs ship in v1? Just XA-ADPCM, or XA + one other?
- Envelope follower output format — CV-like float, MIDI CC, internal mod bus, or all three?
- Does the wrong-prediction-filter lever get its own milestone across all three modules, or roll into XA?
- Standalone window layout — separate from sampler window, or tabbed/integrated?

---

*This brief captures the product vision as discussed 2026-05-29. Pick it up with `/gsd:new-milestone` when ready to build.*
