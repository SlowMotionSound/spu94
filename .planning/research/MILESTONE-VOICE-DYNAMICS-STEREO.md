# Milestone: Voice Dynamics and Stereo Effects

**Status:** Pre-milestone research — distilled from 2026-05-23 design session
**Depends on:** v1.9 Complete Voice (must ship first)
**Research artifact:** `SWEEP-MUSICAL-GESTURES.md` (full gesture catalog, synth UX survey, PS1 historical usage)

## Goal

Expose the PS1's per-voice VCA ramp system (Sony calls it "volume sweep") as a curated suite of musician-facing modulation, stereo, and dynamics effects in the sampler GUI. Every effect maps down to the same underlying hardware: two independent one-shot amplitude envelopes per voice (one L, one R), each with linear/exponential curve, direction, speed, and phase polarity.

## Core Principle

The raw hardware gives you 5 parameters per channel (mode, direction, phase, shift, step) times 2 channels = 10 knobs that mean nothing to a musician. This milestone translates those into effects musicians already understand, ordered from proven to experimental.

## Feature Suite (ordered by confidence)

### Tier 1 — High Confidence (ship without prototyping)

**1. Fade In / Fade Out**
- One-shot VCA ramp, exactly what the hardware does natively
- Both L+R ramp together (increase for fade in, decrease for fade out)
- Exponential mode is the natural choice for fade out (fast initial drop, long tail — matches acoustic decay)
- Linear mode for fade in (constant rate, like a motorized fader)
- Controls: Speed + Curve (linear vs "Natural")
- Zero unknowns

**2. Tremolo**
- Retriggered VCA ramp, both channels pulse in sync
- Software retrigger alternates between increase and decrease
- Rate range: ~0.6 Hz (barely perceptible) to ~19 Hz (fast flutter)
- Linear retrigger = triangle-wave tremolo
- Exponential retrigger = asymmetric shape (fast drop, slow recovery — Uni-Vibe character)
- At faster rates, the counter-accumulate quantization becomes audible as staircase modulation — a distinctly "digital PS1" texture
- Controls: Speed, Depth, Curve
- Needs: retrigger mechanism (where it lives — C core vs host layer — is an open question)

**3. Auto-Pan**
- Same as tremolo but L and R ramp in opposition
- One channel increases while the other decreases, then swap on retrigger
- PS1's linear crossfade creates a volume dip at center (NOT equal-power panning) — this IS the character
- Controls: Speed, Depth, Curve
- Needs: same retrigger mechanism as tremolo

### Tier 2 — High Confidence, Needs Routing

**4. Sidechain Duck**
- Voice-to-voice ducking: KON on voice X triggers VCA ramp decrease on voices Y/Z
- Exponential decrease mode = fast attack, slow release — natural compressor behavior
- Speed parameter = release time
- The ramp itself is proven hardware behavior; the routing layer ("this voice ducks those voices") is new
- Recovery: automatically retrigger increase ramp after decrease completes, or manual re-arm
- Controls: Source voice, Target voice(s), Release speed, Depth
- Needs: voice-to-voice routing UI, KON detection hookup

### Tier 3 — Solid Foundation, Needs Tuning

**5. Stereo Widener**
- L and R diverge from a common level (one increases, other decreases, or phase inversion on one side)
- Phase inversion approach creates width "outside the speakers" in headphones (Dolby Pro Logic principle)
- BUT full phase inversion is mono-destructive — signal cancels when summed to mono
- Must include a mono-safety cap: limit how far the widening goes so mono compatibility isn't destroyed
- May also include a mono-check indicator showing signal loss at current width setting
- Controls: Width amount (capped), Curve
- Needs: tuning the cap value, possibly A/B listening between widened and mono-summed versions

**6. AM Synthesis**
- Audio-rate retrigger of VCA ramp (shift 0-8, producing 37 Hz to 7350 Hz modulation)
- Creates sidebands above and below original pitch — metallic, bell-like tones
- PS1's ramp modulator (linear or exponential) produces different harmonic series than clean sine AM
- Linear ramp = odd+even harmonics similar to sawtooth spectrum (harsh, bright)
- Completely unexplored on PS1 hardware — no game ever ran VCA ramps at audio rates
- Controls: Rate, Depth, Curve
- Needs: hearing how quantized ramp shapes actually sound vs clean sine AM; may need depth control to tame harshness

### Tier 4 — Experimental (prototype before committing)

**7. Phase Modulator**
- Retriggered polarity oscillation: one channel's VCA ramp cycles between positive and negative volume
- When one channel goes negative, frequencies identical in both channels cancel; decorrelated content survives
- NOT a real phaser (no all-pass filters, no sweeping notches) — it's a polarity-modulation effect unique to SPU-94
- Slow rates (0.5-4 Hz): stereo image breathes, widens/narrows rhythmically
- Medium rates (4-15 Hz): repeated cancellation creates hollow, phasey timbral character
- Fast rates (15+ Hz): becomes ring modulation territory, hard polarity toggle creates odd-harmonic sidebands
- Most pronounced on mono sources (full cancellation possible), less dramatic on stereo content
- Zero-crossing behavior unknown — the moment volume passes through zero may create audible gaps or clicks
- Controls: Rate, Depth, Stereo balance
- Needs: prototype to determine if it sounds good or broken; zero-crossing character is the key unknown

## GUI Concept

A "VCA Modulation" section in the sampler window. The research identified three layers:

**Layer 1 — Effect selector:** Pick from the 7 effects above. Each effect pre-configures both L+R VCA ramps with the right relationship (same direction for tremolo, opposite for auto-pan, etc.)

**Layer 2 — Musical controls:** Speed (labeled in musical time or seconds, not raw shift values), Depth, Curve (linear vs "Natural"), and effect-specific controls (Spread for auto-pan, Width for stereo widener, Source/Target for sidechain duck)

**Layer 3 — Raw register access (collapsible):** All 5 parameters per channel independently, for power users who want configurations the presets don't cover (like different speeds for L vs R, or different curves for L vs R)

## Existing Volume L/R Controls

The current Volume L and R knobs need rework to be more musically intuitive. They currently expose raw register values (-0x4000 to +0x3FFF). Decision from this session: collapse them into a **Pan knob + Level fader** that maps down to the two volume registers underneath. The VCA Modulation section then sits alongside as "here's how to automate those controls over time."

## Key Decisions Made

| Decision | Rationale |
|----------|-----------|
| Call it "VCA ramp" not "sweep" in all user-facing contexts | "Sweep" is Sony register jargon, meaningless to musicians |
| Order features by confidence, build easiest-first | Reduces risk, delivers value early, experimental items get prototyped last |
| Sidechain ducking triggered by voice KON | Maps naturally to one-shot VCA ramp decrease; exponential mode = compressor-like release curve |
| Stereo widener must have mono-safety cap | Full phase inversion destroys mono compatibility; cap prevents users from going too far |
| Phase Modulator is the name for the polarity-oscillation effect | Unique to SPU-94, distinct from real phasers, needs prototype before committing to ship |
| Retrigger mechanism location TBD | C core (simple but not PS1-faithful) vs host layer (faithful but latency) vs callback (clean but RT-safety concern) |

## Open Questions (carry forward)

1. **Retrigger: C core or host layer?** One-shot VCA ramp is hardware-faithful. Retrigger is a creative extension. Where should retrigger logic live?
2. **BPM sync?** Should effect speeds lock to DAW tempo? Host-layer concern only.
3. **Equal-power compensation for auto-pan?** PS1's linear crossfade dips at center. Offer "Faithful" vs "Smooth" toggle?
4. **Compound envelope visualization?** ADSR and VCA ramp both affect output. Show them overlaid, or separately, or just the product?
5. **Phase Modulator zero-crossing character?** Needs prototyping — could be a signature sound or could click/pop.
6. **Phaser as separate DSP stage?** Anthony expressed interest. Would require all-pass filters not present in PS1 hardware — a departure from spec. Shelved for now.
7. **NON and PMON GUI controls?** Also needed for v1.9 but not part of this milestone's VCA Modulation scope. Separate phases.

## Also Needed for v1.9 (not this milestone)

These came up in the same session but belong in v1.9's remaining work:
- **Pan + Level fader** replacing raw Volume L/R knobs
- **NON toggle** — per-voice noise enable in sampler GUI
- **PMON toggle** — per-voice pitch modulation enable in sampler GUI
- **VCA ramp on/off + basic speed control** — minimal exposure of the ramp system, enough to verify it works before the full effect suite

---

*Distilled from 2026-05-23 design session. Full gesture catalog, synth UX survey, and PS1 historical usage in `SWEEP-MUSICAL-GESTURES.md`.*
