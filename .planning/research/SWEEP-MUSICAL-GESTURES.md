# Volume Sweep: Musical Gestures, Synth UX Survey, and Control Surface Design

**Researched:** 2026-05-23
**Domain:** Musical/creative/UX -- what the PS1 volume sweep CAN DO and how musicians should interact with it
**Audience:** Recording/broadcast engineer who thinks in gestures, not register values

---

## 1. Gesture Catalog: The Creative Possibilities

The PS1 volume sweep is two independent one-shot ramps -- one for left volume, one for right. Each ramp runs from wherever the volume currently sits toward a boundary, then stops. That constraint (one-shot, not oscillating) shapes everything below. Some gestures are native to the hardware; others require software retriggering to simulate what other synths do with LFOs.

### 1.1 Volume Gestures

#### Fade In
**What the listener hears:** Sound emerges from silence, growing louder over time. Like a musician slowly bringing up a fader.

**How to achieve:** Set both L and R to the same sweep: direction=increase, starting from vol=0. Shift controls how fast. Linear mode produces a constant rate of change (like a motorized fader at fixed speed). Exponential mode is slower to start and then accelerates past the 75% mark (0x6000), which actually sounds LESS natural for a fade-in because humans expect a gradual ramp, not an accelerating one.

**Best shift range:**
- shift=12 (~200ms): fast fade-in, good for sound effects "popping in"
- shift=14 (~850ms): musical fade, about one beat at 140 BPM
- shift=16 (~3.4s): slow cinematic fade-in

**Exponential vs linear:** Linear is the right choice here. The PS1's "fake exponential" (rate change above 0x6000) creates an awkward speed-up near full volume that makes the fade feel rushed at the end. Use exponential only if you want that specific character.

**One-shot:** Yes, and that is perfect. A fade-in should happen once.

#### Fade Out
**What the listener hears:** Sound dies away to silence. The classic "end of the record" gesture.

**How to achieve:** Both L and R: direction=decrease, starting from current volume. Here, exponential mode is the clear winner -- it mimics natural acoustic decay where loud sounds lose energy faster. The proportional decrease (`step = step * level / 0x8000`) means the first part of the fade is fast and the tail stretches out, which is exactly how human perception expects a fade-out to work. This is the same curve shape as the ADSR release phase.

**Best shift range:**
- shift=11 (~106ms): quick fade-out, useful for cutting a sound short without a click
- shift=13 (~425ms): tight musical fade, feels like a drummer damping a cymbal
- shift=15 (~1.7s): standard musical fade-out
- shift=18 (~13.6s): long ambient tail

**Exponential vs linear:** Exponential. Always. A linear fade-out sounds mechanical and unnatural because the last 20% of the fade drags on at the same rate as the first 20%. Exponential decrease is perceptually linear -- it matches the logarithmic sensitivity of human hearing. The PS1's proportional decrease is the right curve.

**Anti-stall note:** At very low volumes, the proportional step rounds to zero. The hardware has a guard (`if step==0 && level>0, step=-1`) that ensures it eventually reaches true silence instead of hanging at a whisper forever. This is already implemented in spu94_envelope_step.c.

**One-shot:** Yes, and that is perfect.

#### Swell (Fade In + Hold)
**What the listener hears:** Volume rises to maximum, then stays there. Like a reverse cymbal or an organ growing from nothing.

**How to achieve:** Both L and R: direction=increase, start from 0 or a low level. The sweep ramps up to +0x7FFF and clamps there. Because the sweep IS the volume register (not a multiplier on top of it), the volume permanently stays at max until something else changes it.

**Combined with ADSR:** This is where sweep + ADSR interaction gets interesting. ADSR shapes the overall amplitude envelope (attack-decay-sustain-release), while sweep shapes the volume independently. If you trigger a sound with a normal ADSR (fast attack, some sustain) but set the sweep to a slow increase from zero, you get a compound envelope: the ADSR's attack fires instantly but the volume sweep means the actual output starts quiet and grows. The result is a "shaped swell" -- the ADSR gives the sound its tonal character (bright attack decaying to sustained tone) while the sweep controls the overall loudness contour. No other hardware synth I'm aware of has two concurrent, independent amplitude envelopes per voice like this.

**One-shot:** Yes.

#### Ducking / Dip
**What the listener hears:** Volume drops suddenly and then... stays dropped. For a true duck-and-recover, you'd need to retrigger a second sweep (increase) after the decrease finishes, which requires CPU intervention.

**How to achieve:** Both L and R: direction=decrease, starting from current volume. Volume ramps down and clamps at 0. To recover, fire a second sweep with direction=increase.

**Musical use:** This is how PS1 games implemented "volume reduction during dialogue" -- duck the music voices, play the voice clip, then sweep the music back up. Two separate sweep configurations, timed by the game engine.

**One-shot:** The dip is one-shot. The recovery requires a second trigger.

---

### 1.2 Stereo Gestures

These are where the PS1's independent L/R sweeps become genuinely unique. No other sampler hardware from the era gave you two independent volume envelopes per voice.

#### Auto-Pan (Left to Right)
**What the listener hears:** Sound moves from the left speaker to the right speaker (or right to left). The classic auto-pan effect.

**How to achieve:** L and R sweep in opposite directions simultaneously:
- sweep_l: direction=decrease (volume goes from current down to 0)
- sweep_r: direction=increase (volume goes from 0 up to max)
- Same shift and step on both for matched timing

The key: both L and R must run at the same speed, or the pan movement will be lopsided. Using identical shift/step values guarantees they track.

**What it sounds like vs. a "real" pan pot:** This is NOT equal-power panning. It is a linear crossfade (one side goes down while the other goes up). In a real equal-power pan, the midpoint volume is boosted to ~0.707 (-3dB) to prevent the perceived "hole in the middle" where the sound seems quieter when centered. With the PS1's linear crossfade, there IS a slight volume dip at the midpoint. This is a character -- it makes the pan movement feel more dramatic, more like a sound physically leaving one speaker and arriving at the other, rather than smoothly rotating.

**Speed range:**
- shift=13 (~425ms): fast pan, like a ping-pong delay
- shift=15 (~1.7s): medium pan, good for a guitar or pad slowly drifting across the stereo field
- shift=17 (~6.8s): slow glacial drift, ambient music territory

**Exponential vs linear:** Linear mode is better for auto-pan because exponential would make the pan movement speed up and slow down unevenly (exponential decrease is proportional to current level, so L starts fading fast then slows; R increases linearly then speeds up above 0x6000). The asymmetry between L and R curves would make the pan feel drunk. Stick with linear for clean motion.

**One-shot:** YES. This is the big limitation. The sound pans once and stays there. For continuous back-and-forth panning (what most musicians mean by "auto-pan"), you need software retriggering -- see section 1.3.

#### Stereo Narrowing
**What the listener hears:** A wide stereo sound collapses toward the center. Like pushing a stereo width knob toward mono.

**How to achieve:** Start with asymmetric L/R volumes (e.g., L=+0x7FFF, R=+0x3000, or vice versa). Set both to sweep toward the same target level. If the target is the midpoint, both sweeps converge and the image narrows.

In practice: if L is louder, sweep L downward and R upward, using different shift values chosen so they arrive at the same level at approximately the same time. This requires calculation but is achievable.

**Simpler approach:** Set both L and R to sweep toward a common fixed volume. Since sweep is one-shot, L decreases from loud to that level and clamps; R increases from quiet to that level and clamps. The trick is choosing shift/step combinations for each side that produce the right duration.

**One-shot:** Yes, and the result is permanent narrowing.

#### Stereo Widening
**What the listener hears:** A centered sound expands outward, left and right channels diverging. The opposite of narrowing.

**How to achieve:** L and R sweep in opposite directions from a common starting point. L increases (louder in left), R decreases (quieter in right, or vice versa). The stereo image expands outward.

**Phase inversion twist:** If one side uses the negative phase bit (phase=1), the expansion isn't just a volume change -- it's a polarity flip. One channel's waveform inverts relative to the other. This creates a sense of width that extends "outside the speakers" in headphones. It's the same principle used in Dolby Pro Logic surround encoding. The PS1 hardware specifically supports this (negative volumes are legal and create phase inversion).

**Caution:** Phase inversion between L and R channels causes cancellation when summed to mono. A sound that's been stereo-widened this way will partially or fully disappear on a mono playback system. This is historically accurate -- PS1 games that used this trick had the same mono-compatibility problem.

**One-shot:** Yes.

#### Spatial Motion (Crossfade)
**What the listener hears:** Like a sound source walking past you -- approaching from one side, getting louder, then receding on the other side. Or the inverse: a sound that starts centered and splits apart.

**How to achieve:** This is the most expressive stereo gesture. L fades out while R fades in (or vice versa) with a slight time offset. If L starts decreasing slightly before R starts increasing, the sound briefly dips in volume during the "crossover" -- creating the impression of something passing directly in front of (or behind) the listener.

**Game audio heritage:** This is exactly how PS1 games positioned enemy sounds. An enemy walking from your left to your right: voice starts with L=max, R=0. CPU triggers L sweep decrease, waits a moment, triggers R sweep increase. The volume dip at the crossover mimics the real acoustic effect of a sound source moving through 0-degrees azimuth (directly ahead). Game developers discovered this naturally because it's what the hardware makes easy.

**One-shot:** Each crossfade is one-shot. Continuous movement requires re-triggering.

---

### 1.3 Tremolo and Modulation Gestures (Retriggered)

The PS1 sweep is one-shot, not oscillating. But if the software retriggers the sweep when it reaches its boundary (or at fixed intervals), you can simulate periodic modulation. This is NOT what the original PS1 hardware was designed for -- it requires CPU involvement to keep re-arming the sweep -- but it creates sounds that are genuinely useful and that no other sampler hardware produces exactly.

#### Pseudo-Tremolo
**What the listener hears:** Volume wobbles up and down rhythmically. Classic tremolo, but with a distinct character because the ramp shape is either perfectly linear or has the PS1's particular exponential curve.

**How to achieve:** Software timer retriggers sweep alternating between increase and decrease. Each half-cycle is one sweep run.

**Rate range (from timing calculations):**

| Shift | Half-cycle | Full-cycle | Equivalent Hz | Musical feel |
|-------|-----------|------------|---------------|-------------|
| 9 | 27ms | 53ms | ~18.8 Hz | Fast, nearly audio-rate. Flutter. |
| 10 | 53ms | 106ms | ~9.4 Hz | Fast tremolo. Leslie speaker territory. |
| 11 | 106ms | 212ms | ~4.7 Hz | Moderate tremolo. Classic guitar amp vibrato. |
| 12 | 212ms | 425ms | ~2.4 Hz | Slow tremolo. Vintage Fender amp warble. |
| 13 | 425ms | 849ms | ~1.2 Hz | Very slow pulse. Breathing effect. |
| 14 | 849ms | 1698ms | ~0.6 Hz | Sub-tremolo. Barely perceptible motion. |

**Shape character:** Linear mode produces a triangle-wave tremolo (volume goes up at a constant rate, then down at a constant rate). Exponential mode produces an asymmetric shape -- the decrease half is faster at the start and slower at the tail (like a sawtooth), while the increase half has the fake-exp speed change above 0x6000. This asymmetry IS a sound. It gives the tremolo a particular "gulp" quality where the volume drops quickly and recovers slowly. Musicians who know the Uni-Vibe know this asymmetric shape.

**What makes this different from a DAW tremolo:** Every DAW tremolo plugin uses a smooth, mathematically perfect LFO (sine, triangle, square). The PS1 sweep produces a QUANTIZED ramp -- the level changes in discrete steps, timed by the counter-accumulate mechanism. At slower rates this quantization is inaudible, but at faster rates (shift 8-10) the steps become audible as a staircase modulation. This is a texture, not a flaw. It sounds "digital" in the same way that early digital synths have a character that's sought-after today.

**One-shot:** No -- this requires continuous retriggering by software.

#### Pseudo-Auto-Pan (Retriggered)
**What the listener hears:** Sound bounces back and forth between speakers continuously. The classic auto-pan or ping-pong effect.

**How to achieve:** Software alternates between two sweep configurations:
1. L=decrease, R=increase (pan right)
2. L=increase, R=decrease (pan left)

Retrigger when both L and R reach their boundaries.

**Rate and feel:** Same timing table as tremolo above, but applied to the stereo field instead of volume.

**What makes this different from a DAW auto-pan:** The PS1's linear crossfade (not equal-power) means there IS a volume dip each time the sound passes through center. Ableton's Auto Pan device specifically compensates for this; the PS1 does not. The dip gives the effect a characteristic "swishing" quality, like the sound is physically moving through space rather than being smoothly rotated.

**One-shot:** No -- requires continuous retriggering.

#### Audio-Rate Amplitude Modulation
**What the listener hears:** At the fastest sweep speeds (shift 0-8), the retriggered sweep enters audio-rate territory (37 Hz to 7350 Hz). At these rates, the individual volume changes are so fast they create new frequencies in the output -- sidebands above and below the original pitch. This is amplitude modulation (AM synthesis), the same principle that creates the metallic, bell-like tones in ring modulators.

**How to achieve:** Retrigger sweep at shift 0-8 with step=0 (fastest). At these rates, the CPU must retrigger every few ticks, which is technically possible on the PS1's R3000 but was never intended. In SPU-94's software sampler, this is trivially achievable since retrigger is just a function call.

**What it sounds like:** Unlike clean AM synthesis from a sine-wave modulator, the PS1's AM uses a RAMP modulator (linear or exponential), which creates a different harmonic series than sine-based AM. Linear ramp AM produces odd and even harmonics in a pattern similar to a sawtooth wave's spectrum. The result is a harsh, bright, metallic tone -- more aggressive than sine AM, less pure, with more overtones. At intermediate rates (shift 7-9, around 20-75 Hz), it crosses through the frequency range where the human ear transitions from perceiving "rhythm" to perceiving "pitch" -- creating a distinctive buzzing/rattling texture.

**Creative territory:** This is unexplored ground for the PS1 hardware. No PS1 game used sweep at audio rates for AM synthesis because the CPU overhead of retriggering thousands of times per second was impractical. SPU-94's software architecture makes it easy. This could be a unique selling point -- "AM synthesis using the PS1's native volume envelope curve."

**One-shot:** No -- requires continuous retriggering at audio rates.

---

### 1.4 Phase Inversion Gestures

The negative phase bit (phase=1) makes volumes go negative, which means the audio waveform flips polarity. This is not a volume change -- it's a phase change. Acoustically, a single channel with inverted polarity sounds identical to non-inverted (your ear doesn't hear absolute phase). But in STEREO, phase differences between L and R create very specific psychoacoustic effects.

#### Mid-Note Phase Flip
**What the listener hears:** If one channel's polarity flips while the other stays normal, the listener hears a sudden shift in the stereo image. In headphones, this is dramatic -- the sound seems to jump from "inside your head" to "outside your head" (or vice versa). On speakers, it's more subtle -- a shift in the perceived width and depth of the sound.

**How to achieve:** One channel: sweep from positive to negative phase (direction=increase, phase=1, which ramps toward -0x7FFF). The other channel: no sweep, or a different sweep. The moment the sweeping channel crosses zero and goes negative, the polarity flips.

**Timing:** The zero-crossing happens at a specific moment determined by the sweep speed and starting volume. With shift=11 and starting volume at +0x7FFF, the volume takes about 106ms to reach zero -- then continues into negative territory. The phase flip is not instant; it's a gradual polarity transition through zero, which means there's a brief moment of silence (or very quiet volume) at the crossover. This is acoustically different from an instantaneous phase flip (which would produce a click). The gradual crossing produces a smooth, eerie transition.

**Creative use:** Time the phase flip to musical events. A chord that starts normal and flips to inverted polarity on beat 3 creates a subtle but physical feeling of the sound "turning inside out."

#### Stereo Cancellation Fade
**What the listener hears:** Sound gradually disappears, not by getting quieter, but by the left and right channels slowly cancelling each other. In headphones, it sounds like the sound shrinks into a point between your ears and then vanishes. On speakers, it's a bizarre effect where the sound seems to lose its body while the volume stays the same, then silently disappears.

**How to achieve:** Start with L and R at equal positive volume. Sweep one channel from positive toward negative (phase=1, direction=increase). As that channel crosses zero and goes negative, it begins cancelling the other channel. The degree of cancellation depends on how well the two channels correlate -- mono sources cancel perfectly; stereo sources with decorrelated content will leave a residue.

**This is unique:** I'm not aware of any hardware synth or sampler that offers a "phase cancellation fade" as a gesture. It exists as a studio technique (polarity-reversed parallel channel slowly brought in to cancel the original) but it's never been available as a per-voice hardware parameter.

**One-shot:** Yes -- the cancellation happens once and then the volume sits at -0x7FFF (full inverted).

---

### 1.5 Combined Gestures: Sweep + ADSR + Other Features

The PS1's per-voice architecture means sweep interacts with everything else in the voice pipeline. These compound gestures are emergent -- they fall out of the architecture without any extra code.

#### Shaped Attack
ADSR provides the tonal envelope (bright attack, decaying harmonics). Sweep provides a separate volume contour. Setting a slow sweep increase (shift=12-14) with a fast ADSR attack creates a sound where the ADSR "tries" to be punchy, but the sweep gate holds the overall volume back. The listener hears a muffled attack that gradually opens up -- like a sound heard through a door that's slowly opening.

#### Reverse Decay
Normal decay: sound starts loud and gets quieter. Reverse: start the sound with a sweep at zero volume, increasing. The ADSR attack fires instantly to full amplitude, but because the sweep volume starts at zero, nothing is heard. As the sweep ramp increases over the next second, the ADSR-shaped sound fades in. If the ADSR is in its sustain or decay phase by the time the sweep volume is high enough to hear, the listener hears a sound that starts from nothing and arrives at a decaying tail -- a reverse decay. Like playing a recording backward, but synthesized in real time.

#### PMON + Sweep: FM with Fading Modulation Depth
If voice N modulates voice N+1's pitch (PMON), and voice N has a sweep running on its volume, then the pitch modulation intensity of voice N+1 gradually changes as voice N's output changes. A sweep decrease on the modulator voice creates FM synthesis where the modulation depth decays over time -- starting with harsh, metallic overtones and settling into a pure tone. This is the same technique used in the Yamaha DX7 (operator envelope controlling modulation index), except here it's the PS1's native volume sweep controlling it rather than a dedicated FM envelope.

#### Noise + Sweep: Shaped Noise Bursts
If a voice is set to NON (noise output), the noise goes through ADSR AND through the sweep volume. Sweep can create slow-evolving noise textures -- wind that gradually rises, static that slowly pans across the stereo field, ocean waves that build and recede (if retriggered).

---

### 1.6 Speed Reference Chart

All times are for a full 0-to-max sweep with step=0 (fastest step magnitude):

| Shift | Time | Musical Context |
|-------|------|-----------------|
| 0-6 | <3ms | Sub-sample. Inaudible as volume change. Audio-rate AM territory if retriggered. |
| 7 | 7ms | Click/transient. Useful as a very fast gate open. |
| 8 | 13ms | Click range. Faster than any perceivable fade. |
| 9 | 27ms | Fast attack. Snappy, like a pick hitting a string. |
| 10 | 53ms | Medium attack. Like a bowed string catching. |
| 11 | 106ms | ~1/8 note at 120 BPM. Snappy swell. |
| 12 | 212ms | ~1/4 note at 120 BPM. Musical fade-in/out. |
| 13 | 425ms | ~1/2 note at 120 BPM. Standard fade. |
| 14 | 849ms | ~1 bar at 140 BPM. Medium fade. |
| 15 | 1.7s | ~1.5 bars. Slow, cinematic. |
| 16 | 3.4s | ~3 bars. Very slow. |
| 17 | 6.8s | Ambient territory. |
| 18-20 | 14s-54s | Glacial. Installation art, generative music. |
| 21-26 | 2min-58min | Effectively frozen. These exist because the shift field is 5 bits, not because anyone would use them musically. |

The step parameter (0-3) provides fine-tuning within each shift value. Step=0 is fastest (magnitude 7 per tick when shift<=11), step=3 is slowest (magnitude 4). The difference between step=0 and step=3 at a given shift is roughly 1.75x in duration.

---

## 2. Synth UX Survey: How Others Solve This

### 2.1 Classic Analog Synths (Moog, Sequential, Oberheim)

**Architecture:** VCA (Voltage Controlled Amplifier) with dedicated ADSR envelope. Modulation sources (LFO, second envelope, modulation wheel, aftertouch, pedal) can be routed to the VCA control input.

**What musicians expect:**
- A dedicated ADSR for amplitude, always present
- A separate LFO that can target volume (tremolo), filter (wah), pitch (vibrato)
- Rate and Depth knobs for the LFO
- Mod wheel depth control (how much the wheel adds LFO to the VCA)
- Optional: a second envelope that can be routed to VCA for compound envelope shapes

**Key UX pattern:** Modulation sources and destinations are separate concepts. You pick a source (LFO, envelope, wheel) and a destination (VCA, filter, pitch). The amount knob controls how much.

**Moog Matriarch specific:** Two VCAs with three modes -- AMP ENV (both VCAs get amplitude envelope), SPLIT (VCA1 gets filter envelope, VCA2 gets amplitude envelope), DRONE (VCAs controlled by CV inputs, no envelope). The "multiple VCA modes" idea is relevant to sweep -- it's about giving the same hardware different personalities based on a mode switch.

**Source:** [ASSUMED -- based on general synth architecture knowledge and Moog product pages]

### 2.2 Eurorack Modular

**Architecture:** VCA is a separate module. Anything can modulate it: LFO, envelope, sequencer, another audio signal, random voltage, joystick, light sensor, etc.

**Auto-pan technique:** Two VCAs fed the same audio. One LFO signal goes to both VCA control inputs, but inverted for one of them. When VCA-L opens, VCA-R closes. The LFO waveform directly shapes the pan motion. This is exactly analogous to the PS1's independent L/R sweep -- the modular just gives you more waveform choices (sine, triangle, random, complex shapes) while the PS1 gives you linear ramp or exponential ramp.

**What musicians expect:**
- Any signal can be a modulation source (no fixed assignment)
- Attenuation/inversion controls between source and destination
- Visual feedback of modulation (LED brightness, scope display)
- Ability to modulate the modulators (meta-modulation)

**Key UX pattern:** Patch cables are the interface. Concepts are exposed physically: one jack = one signal. The musician understands signal flow by tracing cables. There is no "menu" for modulation routing.

**Source:** [CITED: perfectcircuit.com/signal/learning-synthesis-vcas, modwiggler.com/forum/viewtopic.php?t=72838]

### 2.3 Roland Jupiter-8 / JD-800

**Jupiter-8:** Eight-voice analog poly. No per-voice pan modulation. Stereo is achieved by voice allocation across a stereo field, not by modulating individual voice panning.

**JD-800:** Digital, 1991. Has vast modulation possibilities including aftertouch, but notably NO pan modulation. The JD-990 rack version added pan settings. This is instructive -- pan modulation was considered an advanced feature even in the early '90s.

**What musicians expect:** Per-voice pan spread (assign voices across the stereo field) is the baseline. Per-voice pan MODULATION (pan moving during a note) is a premium feature.

**Source:** [CITED: greatsynthesizers.com/en/review/roland-jd-800-best-digital-pad-synthesizer/]

### 2.4 Modern Poly Synths (Prophet Rev2, Hydrasynth)

**Prophet Rev2:** 16-voice analog poly. Has a "Pan Spread" control that distributes voices across the stereo field. The spread is static (set once, voices stay in their assigned positions) -- it's not a per-voice pan envelope.

**Hydrasynth:** 8-voice digital poly. Deep modulation matrix where any source can target any destination, including pan. Per-voice pan modulation via the mod matrix is possible but requires deliberate patching.

**What musicians expect:**
- Pan Spread: single knob, distributes voices automatically
- Pan modulation: via mod matrix, not a dedicated control
- Macro knobs: group multiple parameters under one physical control

**Key UX insight:** Even on synths with deep modulation matrices, per-voice pan modulation is a "wire it yourself" feature, not a front-panel control. This suggests that auto-pan and stereo motion are considered intermediate-to-advanced techniques, not beginner features. SPU-94's control surface should reflect this: make the common gestures (fade in, fade out, simple pan) one-click, but keep the advanced combinations accessible for power users.

**Source:** [CITED: sequential.com/product/prophetrev2/, forum.vital.audio]

### 2.5 Soft Synths (Serum, Vital)

**Vital:** Per-oscillator Level and Pan controls. Four macro knobs assignable to any parameter with independent depth. LFOs can target pan. The interface is visual -- you drag a modulation source (LFO icon) to a destination (pan knob) and the mod routing appears as a colored arc.

**Serum 2:** Macros can influence other modulators (like LFO depth or Chaos amount), meaning a single macro can animate modulation itself. One macro example fades in reverb size, pan spread, and delay feedback simultaneously.

**What musicians expect:**
- Visual modulation routing (drag source to destination)
- Macro knobs for performance control
- Preset-level modulation (modulation is saved per preset, not configured live)
- Per-voice vs. global distinction clear in the interface

**Key UX insight:** The macro concept (one knob controls multiple parameters) maps directly to sweep gesture presets: "Pan Left to Right" is a macro that configures sweep_l=decrease and sweep_r=increase with matched timing.

**Source:** [CITED: borntoproduce.com/blogs/blog/how-to-use-vital-synth-beginners-guide, mind-flux.com/news-1/2025/11/10/linking-modulation-to-performance-macros-in-serum-2]

### 2.6 DAW Effects: Ableton Auto Pan, Logic Tremolo

#### Ableton Auto Pan (Live 12.3: "Auto Pan-Tremolo")
**Parameters:**
- **Amount:** Intensity of the effect (0-100%)
- **Rate:** Speed, in Hz or synced to musical note values
- **Phase:** 0 = tremolo (L and R in phase), 180 = auto-pan (L and R opposite)
- **Shape/Waveform:** Sine, triangle, square, saw, random
- **Offset:** Where in the waveform cycle the effect starts
- **Attack Time:** (new in 12.3) Time for LFO modulation to ramp up -- preserves transients
- **Dynamic Frequency Modulation:** (new in 12.3) Input signal level adjusts LFO speed

**Key UX insight:** The Phase knob is genius. A single control morphs between tremolo (volume pulsing, both channels together) and auto-pan (volume pulsing, channels opposite). This is exactly the relationship between sweep_l and sweep_r: same parameters = tremolo, opposite parameters = pan.

**Source:** [CITED: sonicbloom.net/auto-pan-tremolo/, ableton.com/en/manual/live-audio-effect-reference/]

#### Logic Pro Tremolo
**Parameters:**
- **Rate:** LFO speed (Hz or synced)
- **Depth:** Modulation amount
- **Symmetry:** Adjusts the balance between up-phase and down-phase of the waveform cycle. At 50%, the up and down phases are equal (triangle/square). Moving away from 50% creates saw-like shapes. This is like controlling the ratio between sweep increase time and sweep decrease time.
- **Smoothing:** Rounds the corners of the waveform (sharp transitions vs. smooth)

**Key UX insight:** The Symmetry parameter maps to using different shift values for the increase vs. decrease half-cycles in retriggered sweep. A fast increase and slow decrease (asymmetric) creates the "pumping" tremolo effect.

**Source:** [CITED: support.apple.com/guide/logicpro/tremolo-controls-lgcef266d9be/mac]

### 2.7 Hardware Samplers (MPC, SP-404, Octatrack)

**SP-404 MKII:** Three-stage envelope editor with visual display. Envelopes are drawn as curves on screen. No per-voice pan modulation -- effects are applied globally.

**Octatrack:** Three LFOs per track. Each LFO can target any parameter including pan and volume. The crossfader can blend between scenes (complete parameter snapshots). This is the closest hardware sampler to what SPU-94 could do -- scene morphing is analogous to coordinated sweep presets.

**MPC:** Per-pad volume envelope, per-pad pan position. No per-pad pan modulation (pan is static per pad assignment).

**Key UX insight:** Hardware samplers generally treat pan as a static assignment, not a dynamic modulation target. Volume envelopes exist but are simple (one-shot, not retriggered). SPU-94's sweep-driven auto-pan would be unusual in the sampler category -- a differentiator.

**Source:** [ASSUMED -- based on product documentation summaries]

---

## 3. Recommended Control Surface

Based on the gesture catalog and synth UX survey, here is a recommended control architecture for exposing the PS1 sweep to musicians. The design follows three layers: Gesture Presets (one-click common effects), Musical Controls (knobs with meaningful labels), and Raw Register Access (power-user detail).

### 3.1 Design Philosophy

**Core principle:** The sweep's five raw parameters (mode, direction, phase, shift, step) are meaningless to a musician. "Shift=13, direction=decrease, mode=exponential" should be "Fade Out, ~half a beat." The control surface translates intent into register values.

**The Phase knob insight (from Ableton):** The most powerful single UX idea from the survey is Ableton's Phase parameter that morphs between tremolo and auto-pan. For SPU-94, a single control called "Stereo" or "Spread" could determine the RELATIONSHIP between L and R sweep:
- 0% = both channels identical (volume effect / tremolo)
- 50% = channels offset (partial stereo motion)
- 100% = channels opposite (full auto-pan)

This one control, combined with a Speed control, covers the majority of what musicians will want.

### 3.2 Layer 1: Gesture Presets

One-click configurations that set both sweep_l and sweep_r with coordinated parameters:

| Preset Name | What It Does | L Config | R Config |
|-------------|-------------|----------|----------|
| **Fade In** | Volume rises from 0 to max | increase, linear | increase, linear |
| **Fade Out** | Volume falls from max to 0 | decrease, exponential | decrease, exponential |
| **Pan Left** | Sound moves to left speaker | increase, linear | decrease, linear |
| **Pan Right** | Sound moves to right speaker | decrease, linear | increase, linear |
| **Narrow** | Stereo image collapses to center | (depends on current state) | (depends on current state) |
| **Widen** | Stereo image expands outward | increase, linear | decrease, linear |
| **Phase Flip** | One channel inverts polarity | phase=1, increase | (no change) |
| **Kill** | Fast silence | decrease, linear, shift=7 | decrease, linear, shift=7 |

Each preset has a **Speed** knob (maps to shift/step) and a **Curve** toggle (linear vs. exponential).

### 3.3 Layer 2: Musical Controls

For users who want more than presets but don't want raw registers:

#### Volume Sweep Section
```
[SPEED]     [DEPTH]     [CURVE]     [SPREAD]
 rotary      rotary     lin/exp      rotary
```

- **Speed:** Maps to shift (0-31). Labeled in musical time at the current BPM. "1/4 note", "1 bar", "4 bars", etc. If no BPM reference, display in seconds: "0.1s", "0.5s", "2.0s".
- **Depth:** How far the sweep travels. Full depth = 0 to 0x7FFF (or max to 0). Partial depth starts or stops partway. This requires setting the initial volume carefully.
- **Curve:** Linear or Exponential. Labeled "Linear" (constant rate, like a motorized fader) and "Natural" (exponential, like acoustic decay). Not "exponential" -- that word means nothing to most musicians.
- **Spread:** The relationship between L and R. 0% = identical (mono volume effect), 100% = opposite (full stereo pan). Middle values = partial stereo motion.

#### Direction Section
```
[UP]  [DOWN]  [RETRIGGER]
 btn   btn      toggle
```

- **Up:** Sweep increases volume
- **Down:** Sweep decreases volume
- **Retrigger:** When on, automatically retriggers the sweep when it reaches its boundary, alternating direction. This turns the one-shot sweep into a periodic modulation (tremolo/auto-pan). The retrigger rate is determined by the Speed control.

### 3.4 Layer 3: Raw Register Access (Power User)

Direct access to all five sweep parameters per channel, independently:

```
LEFT SWEEP                      RIGHT SWEEP
[Mode: Lin/Exp]                 [Mode: Lin/Exp]
[Dir: Inc/Dec]                  [Dir: Inc/Dec]
[Phase: Pos/Neg]                [Phase: Pos/Neg]
[Shift: 0-31]                   [Shift: 0-31]
[Step: 0-3]                     [Step: 0-3]
[Active: On/Off]                [Active: On/Off]
```

This panel should be collapsible/hidden by default, available for users who want to experiment with parameters that the musical controls don't expose (like different speeds for L and R, or different modes for L and R).

### 3.5 Visual Feedback

The most important piece: show what the sweep is DOING.

**Dual level meters:** Two vertical bar indicators (L and R) showing current sweep level in real time. These should animate smoothly, showing the ramp as it progresses. When sweep is inactive, they show static volume. When active, they visibly move.

**Stereo position indicator:** A single dot on a horizontal L-R axis showing the perceived stereo position based on the L/R volume ratio. When an auto-pan is running, this dot moves left to right.

**Sweep shape preview:** Before arming a sweep, show a small graph of what the ramp will look like (linear = straight line, exponential = curve). Include a time scale. This is like Logic Pro's waveform display in its Tremolo plugin.

**ADSR + Sweep compound view:** Since ADSR and sweep run concurrently, showing both overlaid on the same time axis lets the musician see how they interact. ADSR is the tonal envelope; sweep is the volume envelope. The product of both is the actual output level.

### 3.6 Re-trigger Architecture

For tremolo and auto-pan modes, the software needs to automatically re-trigger the sweep. This should be implemented as a timer in the mixer/host layer, not in the C core (the core is hardware-faithful; retrigger is an SPU-94 creative extension).

**Options for re-trigger:**
1. **Timer-based:** Fixed interval re-trigger, independent of when the sweep actually reaches its boundary. Simpler, but can drift out of sync with the sweep (especially at slower rates where the sweep might finish before or after the timer fires).
2. **Completion-based:** Monitor the sweep level and re-trigger when it reaches the boundary (0 or 0x7FFF). Produces clean, glitch-free oscillation but requires polling the sweep state.
3. **BPM-synced:** Re-trigger at musical subdivisions of the host tempo. This is what DAW plugins do. The sweep speed (shift/step) is chosen to approximately match the musical interval, and the re-trigger snaps to the beat grid.

Recommendation: **Completion-based** as the default (cleanest result), with an optional BPM-sync mode for DAW integration.

---

## 4. PS1 Historical Usage

### 4.1 What Sony Intended

The nocash documentation describes sweep as "another Volume envelope, additionally to the ADSR volume envelope" and notes that "unlike ADSR, sweep can be used for stereo effects, such as blending from left to right." This is the only usage guidance in the official documentation.

Sony's LibSPU (the official PS1 SDK sound library) wrapped the raw SPU registers in higher-level functions like SpuSetVoiceVolume. The SDK documentation (Psy-Q training materials from 1997) focused on basic volume control and sample playback. Sweep was available but not prominently featured -- it was a "if you know what you're doing" feature.

**Source:** [CITED: problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm, psx.arthus.net/sdk/Psy-Q/DOCS/TRAINING/Summer97/rvsndpg.pdf]

### 4.2 How Game Developers Actually Used It

Based on analysis of PS1 game audio behavior and emulator implementations:

**Common uses:**
- **Fade-in/fade-out of music channels:** The most basic use. Game starts a music track with volume sweep increasing from 0. When transitioning scenes, sweep decreases to 0. This avoided the CPU overhead of manually writing volume registers every frame.
- **Spatial positioning of sound effects:** Enemy footsteps, environmental sounds positioned in the stereo field by setting different L/R volumes. Sweep allowed smooth transitions as game objects moved. Resident Evil's eerie footsteps and directional audio are a well-known example.
- **Voice ducking during dialogue:** Music volume sweeps down when a character speaks, sweeps back up when dialogue ends. This is the PS1 equivalent of sidechain compression for dialogue clarity.
- **Ambience crossfades:** Moving between areas (indoor to outdoor, surface to underwater) with smooth environmental sound transitions via sweep.

**Uncommon/exotic uses:**
- **Stereo widening for music:** Some games used slightly offset L/R volumes on chord pads to create width, but active sweep-driven stereo motion in music tracks was rare.
- **Phase inversion for surround:** The PS1 could output surround-encoded audio by using negative volumes on rear-channel sounds (left-positive, right-negative for surround left). This was documented in Sony's development materials but rarely used because most PS1 games output stereo, not surround.

**What was NOT used:**
- Audio-rate sweep (no game ran sweep at shift 0-6 for AM effects)
- Retriggered sweep for tremolo (games used other techniques for modulation)
- Phase sweep gestures (the negative phase bit was poorly documented and barely tested -- nocash's own notes say "not yet tested")

**Source:** [CITED: soundcy.com/article/what-makes-the-ps1-sound, psx-spx.consoledev.net/soundprocessingunitspu/] [ASSUMED -- specific game usage patterns inferred from emulator behavior and homebrew community documentation]

### 4.3 What This Means for SPU-94

The PS1's volume sweep was a utility feature -- game developers used it for practical audio management (fades, ducking, spatial positioning). They did NOT explore its musical/creative potential. This is the opportunity:

SPU-94 can take a feature that was used for "make this sound fade out" and expose it as a musical instrument control that does tremolo, auto-pan, AM synthesis, phase manipulation, and compound envelopes. The hardware-faithful foundation (one-shot ramp, counter-accumulate timing, independent L/R) is the same; the creative exploitation is new.

This aligns with the project's core philosophy: the PS1 implementation is the bedrock, creative exploitation is the product.

---

## 5. Open Questions

### 5.1 Re-trigger: Core or Host Layer?

The one-shot nature of the sweep is hardware-faithful. Adding automatic retrigger (for tremolo/auto-pan) is a creative extension. Where should it live?

- **Option A: C core.** Add a `retrigger` flag to `spu94_sweep_t`. When the sweep reaches its boundary and retrigger=1, automatically reverse direction and reset counter. Pro: simple, self-contained, no host dependency. Con: this is NOT PS1 behavior -- it adds a feature the hardware didn't have.
- **Option B: Host layer (JUCE plugin / standalone GUI).** The GUI monitors sweep state and calls `spu94_voice_mixer_set_sweep_l/r` again when it detects completion. Pro: keeps C core hardware-faithful. Con: polling adds latency (up to one buffer's worth) and complexity.
- **Option C: Callback from core.** The sweep tick function calls a user-provided callback when it reaches its boundary. The host implements the retrigger logic in the callback. Pro: clean separation, low latency. Con: callbacks in RT-safe code require careful design.

Recommendation: This is a product decision. Anthony should weigh in on whether the C core stays "pure PS1" or whether creative extensions are acceptable in the core.

### 5.2 BPM Sync

Should sweep speed be lockable to musical tempo? The raw shift/step values produce fixed durations that don't correspond to musical note values at any tempo. BPM sync would:
1. Accept a tempo (from DAW host, or user-set)
2. Calculate the nearest shift/step combination to achieve the desired note duration
3. Quantize retrigger timing to the beat grid

This is entirely a host-layer concern (the C core has no concept of BPM), but the mapping math (BPM + note value -> shift/step) should be researched and documented.

### 5.3 Equal-Power Compensation for Auto-Pan

The PS1's linear crossfade creates a volume dip at the stereo midpoint. Should SPU-94 offer an option to compensate with equal-power panning? This would modify both sweep levels during the crossfade to maintain constant perceived volume.

Pro: sounds more professional, matches what DAW auto-pan plugins do.
Con: not hardware-faithful, modifies the sweep level in a way the PS1 never did.

Possible middle ground: offer it as a toggle ("Faithful" vs. "Smooth") in the GUI layer, not the core.

### 5.4 Compound Envelope Visualization

ADSR and sweep both affect the output level but are independent state machines. How should the GUI display their combined effect?

- Option A: Two separate displays (one for ADSR, one for sweep)
- Option B: Overlaid on the same time axis, with the product (ADSR * sweep) shown as a third line
- Option C: Only show the product -- the actual output level -- and let users infer the components

This needs prototyping to determine what's most useful.

### 5.5 Phase Bit Behavior (Low Confidence)

The negative phase bit's exact behavior remains uncertain in the hardware (nocash: "not yet tested," DuckStation: "TODO needs hardware test"). For musical gestures that depend on phase inversion (section 1.4), the creative utility is clear even if the exact hardware implementation is uncertain.

Current implementation follows DuckStation's interpretation (ADR-0059):
- Phase bit ignored when direction=decrease AND mode=exponential
- Otherwise, phase inverts the step sign

This is a reasonable interpretation that enables the musical gestures described above. If hardware tests eventually reveal different behavior, the musical gesture catalog remains valid -- only the register-level implementation would need adjusting.

### 5.6 Sweep as SPU-94's "Echo Physics" Equivalent

Echo Physics (SPU-94R's headline feature) decouples echo speed from room size -- an architectural quirk that becomes a creative feature. Volume sweep has an analogous quirk: it decouples the stereo volume envelope from the ADSR amplitude envelope. No other sampler gives you two concurrent, independent amplitude envelopes per voice where one controls spatial position and the other controls tonal shaping. This could be a headline feature for SPU-94S, positioned as: "The only sampler where the volume and the pan are independent envelopes, because that's how a PS1 motherboard works."

This is a product-positioning question, not a technical question. Worth discussing with Anthony.

---

## Sources

### Primary
- [nocash psxspx: SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) -- sweep register format, phase bit description
- [psx-spx consoledev: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- register layout, sweep behavior
- SPU-94 source code: `spu94_sweep.h`, `spu94_sweep.c`, `spu94_envelope_step.c`, `spu94_voice.h` -- actual implementation
- Phase 37 RESEARCH.md -- engineering-level sweep research

### Secondary
- [Ableton Auto Pan-Tremolo documentation](https://sonicbloom.net/auto-pan-tremolo/) -- Phase parameter UX pattern
- [Logic Pro Tremolo documentation](https://support.apple.com/guide/logicpro/tremolo-controls-lgcef266d9be/mac) -- Symmetry parameter concept
- [Perfect Circuit: Learning Synthesis VCAs](https://www.perfectcircuit.com/signal/learning-synthesis-vcas) -- VCA modulation techniques
- [Sound on Sound: Modulation](https://www.soundonsound.com/techniques/modulation) -- tremolo/AM/ring mod distinctions
- [Mod Wiggler: Panning techniques](https://modwiggler.com/forum/viewtopic.php?t=72838) -- eurorack stereo panning with dual VCAs
- [SoundCy: What Makes the PS1 Sound](https://soundcy.com/article/what-makes-the-ps1-sound) -- PS1 audio character
- [Psy-Q SDK Training Materials](https://psx.arthus.net/sdk/Psy-Q/DOCS/TRAINING/Summer97/rvsndpg.pdf) -- original PS1 development documentation

### Timing Calculations
All timing values in this document were computed from the counter-accumulate formula documented in nocash psx-spx and verified against the implementation in `spu94_envelope_step.c`. The formula: `counter_increment = 0x8000 >> max(0, shift - 11)`, step fires when bit 15 set, step magnitude = `(7 - step_index) << max(0, 11 - shift)` for increase.
