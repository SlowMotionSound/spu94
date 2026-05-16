# Pitfalls Research — v1.8 PSX Voice Engine

**Domain:** Adding a spec-faithful PS1 SPU 24-voice ADPCM playback engine to the existing libspu94 reverb processor.
**Researched:** 2026-05-16
**Confidence:** HIGH on voice path mechanics (single-counter architecture already shipped and verified in v1.7 post-beta; ADPCM codec already shipped in v1.1; Gaussian table and interpolation formula verified against 3 emulator sources in DEEP-SPU-VOICE-PATH.md). MEDIUM on ADSR timing (nocash ADSR counter formula verified; exact behavior at edge cases — KON race, decay target boundary — is emulator-consensus only, not silicon-confirmed). MEDIUM on SPU RAM collision (addressing rules clear; interaction with mBASE addressing is implementation-specific).

---

## Orientation

The existing codebase already has:
- The ADPCM decode block (`spu94_adpcm_decode_block`) — 28-sample batch, filter state carry-forward, all 5 filter pairs
- The Gaussian table (`spu94_gauss_table[512]`) — 3-way verified (nocash, DuckStation, Mednafen)
- The single-counter voice path for the ADPCM coloration bus — pitch counter driving both sample advancement (bits 12+) and Gaussian index (bits 4-11) simultaneously
- The send/return mixer architecture — voices sum before reverb send

What v1.8 adds: a separate 24-voice playback engine that reads **pre-encoded SPU RAM** (not encode-then-decode) and drives it through its own per-voice ADSR envelope, loop logic, and Gaussian interpolation. The coloration bus stays unchanged.

This document is organized by subsystem. Each pitfall identifies the warning signs, the prevention strategy, and which implementation phase should address it.

Phase labels reference expected v1.8 structure:
- **P-VOICEPATH** — per-voice pitch counter, block decode, Gaussian interpolation
- **P-ADSR** — envelope generation (attack/decay/sustain/release state machine)
- **P-LOOP** — loop flag parsing, start/end address handling
- **P-SPURAM** — 512 KB address space management, collision with reverb work buffer
- **P-KEYING** — KON/KOFF timing, voice state machine transitions
- **P-MIXER** — 24-voice summation, reverb input routing, per-voice volume
- **P-INTEGRATE** — wiring voice engine into existing spu94_process / reverb chain
- **P-VERIFY** — test infrastructure, golden files, regression against ADPCM coloration path

---

## Critical Pitfalls

Mistakes that require a rewrite or produce fundamentally wrong audio.

---

### C1: Conflating the ADPCM coloration bus with the voice engine's decode path

**What goes wrong:**
The existing `spu94_process.c` ADPCM path encodes the live input into ADPCM and immediately decodes it — the codec is a coloration stage, not a playback mechanism. The voice engine decodes **pre-stored** ADPCM blocks from SPU RAM with no encode step. These two paths are architecturally opposite:

| | Coloration bus | Voice engine |
|---|---|---|
| Data source | Live 44.1 kHz input | Pre-encoded blocks in SPU RAM |
| Encode step | YES — brute-force 65-combination search | NO — data is already encoded |
| Decode step | YES — same block decoder | YES — same block decoder |
| State carry | Runs continuously | Carries across loop points |
| Gaussian interp | One shared ring buffer | Per-voice ring buffer |

Developers tempted to "reuse" the coloration path will route voice samples through the encode stage, which degrades quality unnecessarily (encode introduces distortion the real SPU never applies to already-encoded RAM data) and couples voice timing to the block accumulation buffer (`adpcm_buf_pos`), which runs at a different rate than voice pitch.

**Prevention:**
1. The voice engine has its own decode path. It calls `spu94_adpcm_decode_block` directly on 16-byte blocks fetched from SPU RAM — zero encode, decode-only.
2. The `adpcm_buf_pos`, `adpcm_in_buf_l/r`, `adpcm_out_buf_l/r`, and `adpcm_state_l/r` fields in `spu94_state` are coloration-bus state. Voice state lives in a separate `spu94_voice` struct (or per-voice fields) — never in the coloration fields.
3. The per-voice `spu94_adpcm_state` (filter coefficients `old`/`older`) is separate from `state->adpcm_state_l/r`. Each voice carries its own filter state for cross-block continuity.

**Warning signs:**
- Voice decode calls `spu94_adpcm_encode_block` at any point
- The `adpcm_buf_pos` counter is referenced from the voice engine code
- A 24-voice polyphonic patch has audible encode artifacts that don't exist on real PS1 hardware

**Phase:** P-VOICEPATH (architecture definition)

---

### C2: Per-voice Gaussian ring buffer state collision — sharing state across 24 voices

**What goes wrong:**
`spu94_state` currently holds a single set of Gaussian ring buffers (`gauss_ring_l[4]`, `gauss_ring_r[4]`, `gauss_ring_pos`). These belong to the coloration bus — one voice, one buffer. A voice engine with 24 voices needs 24 independent ring buffers. Naively adding the voice engine and reading from the existing ring will mix samples from different voices, producing incoherent output.

Each voice must carry:
- 4-sample ring buffer of decoded samples (the 3-history-from-previous-block plus current)
- Ring write position (which of the 4 slots is the next to be written)
- The single pitch counter (drives BOTH sample advancement AND the Gaussian index — they cannot be separated)

**Prevention:**
1. The per-voice struct contains: `uint16_t pitch_counter`, `int16_t gauss_ring[4]`, `uint8_t ring_pos`.
2. The pitch counter AND ring buffer are per-voice — the single-counter architecture documented in DEEP-SPU-VOICE-PATH.md §7.1 applies to EACH voice independently.
3. The coloration-bus fields (`gauss_ring_l`, `gauss_ring_r`, `gauss_ring_pos`, `voice_counter` in `spu94_state`) are untouched by the voice engine.

**Warning signs:**
- Voice engine references `state->gauss_ring_l` or `state->gauss_ring_pos`
- Chords produce frequency-beating artifacts that come and go with note count
- Playing the same note on two voices simultaneously produces the wrong waveform

**Phase:** P-VOICEPATH (architecture definition)

---

### C3: ADSR envelope timing: the counter mechanism produces non-obvious quantization

**What goes wrong:**
The PS1 ADSR does not step volume on every 44.1 kHz tick. It uses a counter that accumulates `CounterIncrement` per tick, and the actual volume step fires only when a specific counter bit transitions. The nocash formula is:

```
CounterIncrement = 0x8000 >> max(0, ShiftValue - 11)
AdsrStep = 7 - StepValue
IF Decreasing XOR PhaseNegative: AdsrStep = NOT AdsrStep
AdsrStep = AdsrStep << max(0, 11 - ShiftValue)
```

Volume only changes when `(Counter += CounterIncrement)` causes bit 15 to be set. This means:
- ShiftValue=0 → CounterIncrement = 0x8000 → bit 15 is set EVERY tick → one step per tick
- ShiftValue=11 → CounterIncrement = 1 → bit 15 is set every 32768 ticks (~0.74 seconds per step)
- ShiftValue=31 → CounterIncrement is effectively 0 → envelope is frozen

The common mistake is implementing ADSR as a direct ramp with sample-based wait counts, which produces the right average rate but wrong timing character. The counter-based system means envelope steps can happen at very irregular intervals within a given rate setting.

**Exponential attack trap:** When envelope mode is exponential AND current level > 0x6000:
- If ShiftValue < 10: `AdsrStep /= 4`
- If ShiftValue >= 11: `CounterIncrement /= 4`
- If ShiftValue == 10: both divide by 4

This is a "fake exponential" — the step rate drops at high levels to simulate the characteristic slowdown of a real exponential curve. Missing this produces attacks that reach full volume too quickly at high levels.

**Exponential decrease formula:** For Release phase (and Decay, and any exponential-decrease Sustain):
```
AdsrStep = AdsrStep * AdsrLevel / 0x8000
```
This multiplies the step by the ratio of current volume to maximum, which produces the characteristic slowing-down of a natural decay. Missing this produces a linear decay that sounds wrong — it audibly trails off too fast at high levels and too slowly near zero.

**Decay target:** Decay descends toward the sustain level, defined as `(SustainLevel + 1) * 0x800`. It does NOT go to zero. Getting this wrong either cuts off the sound abruptly or skips the transition to sustained volume.

**Prevention:**
1. Implement the counter exactly as described — `uint32_t adsr_counter` per voice, accumulated every tick; step fires on bit-15 transition.
2. The "fake exponential" check for attack is a mandatory branch — not an optimization.
3. For exponential decrease: compute `step = step * current_level / 0x8000` using int32 intermediate to avoid overflow.
4. Golden-file tests: verify envelope shapes match emulator output (Mednafen or DuckStation) on known ADSR parameters.

**Warning signs:**
- Attacks reach full volume at the right time but the curve shape sounds wrong (too linear near peak)
- Decays sound like linear ramps instead of natural falloffs
- Sustain is at the wrong level (note that sustain level register value 0 = 0x800, not zero volume)
- Envelope timing that is right at ShiftValue=0 but drifts at higher ShiftValues

**Phase:** P-ADSR

---

### C4: Loop flag semantics — flags live in the ADPCM block header, not in separate metadata

**What goes wrong:**
The loop start and loop end flags are stored in byte 1 of each 16-byte ADPCM block, not in SPU registers or separate metadata. The block layout is:

```
Byte 0: (shift | filter<<4)
Byte 1: flags
  Bit 0: Loop End — when hit, jump to loop-start address; set ENDX flag
  Bit 1: Loop Repeat — if set AND Bit 0 set: jump to loop start and keep playing
           if clear AND Bit 0 set: jump to loop start AND set ADSR level to zero (mute then release)
  Bit 2: Loop Start — copy current address as the loop-start point for this voice
Bytes 2-15: 14 data bytes (28 nibbles = 28 samples)
```

`spu94_adpcm_decode_block` already returns the flag byte. But the existing coloration bus discards it (coloration has no loop logic). The voice engine must read the returned flag byte and act on it.

**Common mistakes:**
1. Ignoring Bit 1 and treating all Loop-End blocks as "end and mute" — misses the repeat case, which is how virtually every looped instrument sound works on PS1.
2. Treating the loop-start address as a SPU register set by the game program. It IS stored in a register (VxREPEAT, `0x1F801C0E + N*10`), but it gets SET automatically when the playback cursor passes a Loop-Start-flagged block. If you only use the programmer-supplied address and ignore the auto-update, looped samples that set their own loop point will loop from the wrong position.
3. Confusing loop-start address updates with loop-end jumps. The loop-start address is updated as the cursor PASSES the flag block (regardless of whether the voice is looping). The jump to loop-start only happens when a loop-end block is reached.
4. Not zeroing ADSR level on Loop-End-without-Repeat. This is how one-shot sounds work on PS1 — the sample ends, ADSR drops to zero, and the voice outputs silence while nominally "playing."

**Prevention:**
1. Parse the flag byte returned by `spu94_adpcm_decode_block(...)` on every block.
2. On Loop-Start flag: `voice->loop_start_addr = voice->current_addr`.
3. On Loop-End flag: jump to `voice->loop_start_addr`, set ENDX. If Bit 1 clear: set ADSR level to zero.
4. Do NOT end the voice (silence it for reuse) on Loop-End — the voice keeps "running" at zero level. KON must be issued again to reuse it.

**Warning signs:**
- Looped samples play once and stop instead of looping
- Looped samples jump to the wrong position (typically the programmer-supplied address at key-on instead of the address from the in-data Loop-Start flag)
- One-shot samples keep producing a faint residual tone after the sample ends

**Phase:** P-LOOP

---

### C5: ADPCM filter state across loop points — the decoder state must be preserved at the loop-start block

**What goes wrong:**
The ADPCM decoder carries inter-block state: the last two reconstructed samples (`old` and `older` in `spu94_adpcm_state`). This state must be correct at the start of every block. When playback loops from a Loop-End block back to the Loop-Start block, the decoder needs the filter state that was current at the start of the Loop-Start block — not the state at the end of the Loop-End block.

Getting this wrong produces a click or tonal glitch at the loop point because the predictor computes from the wrong history values.

**Prevention:**
1. When the playback cursor passes a Loop-Start-flagged block: snapshot the `spu94_adpcm_state` at that point: `voice->loop_adpcm_state = voice->adpcm_state`.
2. When a Loop-End is reached and the cursor jumps to the loop-start address: restore the decoder state to `voice->loop_adpcm_state`.
3. The snapshot must happen BEFORE the block is decoded — the state at the start of the loop-start block, not after it. Concretely: snapshot immediately after detecting the Loop-Start flag, but after the full block decode (so `loop_adpcm_state` captures the state that will be valid at the START of the NEXT block, i.e., the state after the loop-start block is consumed).

**Practical note:** The existing `spu94_adpcm_decode_block` updates `state->old` and `state->older` in place and returns the flag byte. The per-voice decoder state is a separate `spu94_adpcm_state` instance per voice. After calling decode, if the returned flag byte has bit 2 (Loop-Start) set, snapshot the just-updated state into `voice->loop_adpcm_state`. This is the state that was produced after the loop-start block — it is the correct initial state for when the loop cycles back.

**Warning signs:**
- A click or pitch discontinuity at exactly the loop point
- The click disappears if you change the loop-start address (because the state snapshot changes)
- Click is worse for higher filter indices (filter 4 has stronger prediction; wrong history = larger error)

**Phase:** P-LOOP

---

### C6: SPU RAM address space collision with the reverb work buffer

**What goes wrong:**
The PS1 SPU has 512 KB of shared RAM (address range 0x00000 to 0x7FFFF). This single address space holds:
- Voice sample data (placed by the game at arbitrary addresses)
- The reverb work buffer (mBASE-anchored; v1.0 uses the full buffer at `work_buf` with mBASE=0)

In v1.8, SPU RAM will store actual encoded ADPCM sample data. The reverb work buffer is also backed by memory. If the voice engine places sample data at addresses that overlap the reverb work buffer, the reverb will read sample data as reverb taps and the sample playback will read reverb feedback as sample data — both produce wrong output, often catastrophically.

On the real PS1, the game developer manages this partition (they must not put samples in the reverb buffer region). In v1.8, the partitioning is SPU-94's responsibility.

**Additional risk:** The first 0x1000 bytes of SPU RAM (0x00000-0x00FFF) are reserved on the real hardware for CD audio and SPU internal use. Placing sample data there risks conflict with hardware-internal behavior, though in a software-only implementation this is merely a convention issue.

**Prevention:**
1. The voice engine's SPU RAM is a separately managed byte array — do NOT share the reverb `work_buf`. The reverb work buffer is an opaque allocation in the `spu94_state`; SPU RAM is a separate allocation managed by v1.8 state.
2. Define a clear partition: SPU RAM = 512 KB for voice samples. Reverb work buffer is the SAME bytes but addressed only through `spu94_reverb_body`'s mBASE-relative tap logic. On the real hardware these overlap in the same address space. In v1.8, represent them as separate buffers with a documented relationship (reverb buffer occupies the upper portion of conceptual SPU RAM, starting at mBASE).
3. During v1.8 development with mBASE=0: the reverb buffer starts at SPU RAM address 0. Sample data must be loaded ONLY into addresses above `mBASE + work_buf_size`. Expose a `spu94_voice_spuram_base()` API that returns the first safe sample-data address.
4. Add an assertion: when loading a sample into SPU RAM, verify `start_addr + sample_size_bytes <= SPU_RAM_SIZE` and `start_addr >= spuram_sample_safe_base`.

**Warning signs:**
- Reverb sounds change when samples are loaded (sample data stomping reverb buffer)
- Sample playback sounds different depending on reverb configuration (reverb data aliasing into sample decode)
- Out-of-bounds tap assertions fire from `oob_tap_count` during sample playback

**Phase:** P-SPURAM

---

### C7: Pitch counter overflow and pitch modulation: the pitch clamp is mandatory, not optional

**What goes wrong:**
The real SPU clamps the pitch value to a maximum of 0x3FFF before adding it to the counter (documented in nocash: "Step=MIN(Step,3FFFh)"). This limits playback to at most 4× the nominal 44.1 kHz rate (176.4 kHz equivalent). Without the clamp:
1. The counter can advance by more than 4 samples per tick, requiring the block-decode logic to handle up to 64+ samples consumed in one tick — the current per-tick loop handles up to `pitch >> 12` iterations.
2. Pitch modulation (PMON): when PMON is enabled for a voice, its pitch is multiplied by `(previous_voice_output + 0x8000) >> 15`, mapping the previous voice's amplitude (-32768..32767) to a pitch factor (0..2). An input at INT16_MAX doubles pitch; an input at INT16_MIN produces zero pitch. For a voice already near pitch 0x3FFF, modulation can produce values far above 0x3FFF before the clamp.

**Pitch modulation specific trap:** The formula produces a 32-bit intermediate. The previous voice output is int16 (-32768..32767). Adding 0x8000 gives a range of 0..65535 as uint16. Multiplying by VxPitch (up to 0x3FFF = 16383) gives up to 65535 * 16383 ≈ 1.07 billion, which requires int64 or careful int32 range analysis. nocash says the multiply uses 16x16 arithmetic with truncation — follow that exactly, not a widened version.

**Prevention:**
1. After computing modulated pitch: `step = min(step, 0x3FFF)`. This is not optional; it is documented hardware behavior.
2. For pitch modulation: implement as `step = (VxPitch * ((prev_output + 0x8000) & 0xFFFF)) >> 15`, using int32 for the multiply (inputs fit in 16 bits each). Then apply the 0x3FFF clamp.
3. Voice 0 cannot be pitch-modulated (there is no "previous voice" — the PMON bit for voice 0 is ignored by hardware). Add an explicit `if voice_index == 0: skip modulation` branch.

**Warning signs:**
- High-pitched notes sound different when PMON is enabled vs disabled for voice 0
- Extreme pitch modulation (previous voice near INT16_MAX) causes the counter to advance by >4 samples per tick, producing a loop that reads out-of-bounds from the 4-element ring buffer

**Phase:** P-VOICEPATH

---

### C8: KON/KOFF timing: the voice state must not update mid-tick

**What goes wrong:**
The real SPU processes KON and KOFF requests with approximately one sample period of latency (the CPU writes the register; the SPU acts on it on the next SPU tick, ~1/44100 second later). More critically: KON issued during a voice's current sample tick should not reset the voice's pitch counter or ADSR state until the START of the next tick. If the KON effect is applied mid-tick:
1. A voice playing at sample_index=27 (last sample of a block) that receives KON will appear to decode a new block (correct) but from address 0 instead of the sample's start address (wrong — the address wasn't reset yet).
2. Multiple rapid KON/KOFF writes within a single host buffer will produce undefined voice states.

**Secondary trap:** KON to an already-playing voice resets it from the sample start address. It does NOT resume from where the voice was. This matches real hardware. A developer might assume "re-trigger" semantics that resume the current position.

**Prevention:**
1. KON/KOFF are processed at the beginning of the next sample tick, not immediately when received. Maintain a `pending_kon` and `pending_koff` bitmask (parallel to the existing `pending_mask` in `spu94_state` for reverb registers).
2. At the start of each sample tick: apply `pending_kon` — for each set bit, reset the corresponding voice's pitch counter to 0, reset ADSR state, load start address as current address. Then apply `pending_koff` — transition those voices to Release phase. Clear both masks.
3. KON resets ENDX bit for the voice (ENDX is cleared on KON, set on Loop-End).
4. KON takes priority over KOFF: if both KON and KOFF are pending for the same voice in the same tick, KON wins (voice starts playing).

**Warning signs:**
- Re-triggering a voice while it is playing starts it at the wrong position
- Voice 0's first note is one sample late relative to other voices
- Rapid re-triggers (faster than one buffer) produce voices stuck in an intermediate state

**Phase:** P-KEYING

---

## Significant Pitfalls

Mistakes that produce wrong output but can be fixed without a full rewrite.

---

### S1: 24-voice mixer overflow — the sum must accumulate in int32, not int16

**What goes wrong:**
24 voices each contribute an int16 sample (after ADSR volume scaling). Summing 24 int16 values naively into an int16 output overflows on any loud patch. The correct behavior is to accumulate in int32 (or wider) and then saturate to int16 before sending to output and to the reverb input.

On the real SPU, the hardware can represent intermediate sums up to some internal width before hard-clipping. nocash confirms the saturation: "clipped to -8000h..+7FFFh." The saturation point — whether it clips at the point of the accumulator or at a specific downstream stage — affects character. Getting the clip point wrong by even one operation changes the "pushed into the red" saturation sound.

**Reverb input summing point:** The reverb receives the post-saturation voice mix (voices with the reverb-on flag enabled sum separately, then that sum feeds the reverb input as LeftInput/RightInput). The reverb does NOT receive individual voice signals — it receives the mixed, potentially-saturated voice sum. Getting this wrong changes the reverb character when multiple voices are active.

**Prevention:**
1. Voice mixer accumulates into `int32_t sum_l = 0, sum_r = 0`. For each voice: `sum_l += voice_output_l; sum_r += voice_output_r`.
2. Reverb input is computed in parallel from the reverb-flagged subset: `int32_t rev_sum_l = 0` for voices with `reverb_enable` set.
3. Saturate both sums to int16 BEFORE writing to output or feeding the reverb input.
4. The per-voice scaling (volume left/right applied) is done BEFORE accumulation, not as a final output scale. Each voice contributes `q15_mul_truncate(sample, vol_l)` to the left sum.

**Warning signs:**
- Loud chords produce a crunch that clips asymmetrically (accumulated beyond int16 capacity mid-loop)
- Reverb character changes when adding more voices (reverb getting unsaturated pre-sum instead of saturated post-mix)
- Adding a voice at zero amplitude changes the reverb output (non-reverb voice leaking into reverb sum)

**Phase:** P-MIXER

---

### S2: Per-voice volume — negative values mean phase inversion, not silent

**What goes wrong:**
The SPU voice volume registers (VxVOLL, VxVOLR) are signed int16. A negative value does NOT mean "attenuated below zero." It means "phase-inverted (polarity flipped) audio at the corresponding amplitude." Volume of -0x7FFF is the same loudness as +0x7FFF but with inverted polarity.

A developer might clamp negative volume values to zero (treating negative as "fully quiet"), which silences what should be audible inverted-phase content. This matters for:
- Flanging effects where the game routes the same sound through two voices with opposite polarity for comb filtering
- Any game that uses negative volumes for stereo imaging tricks
- The test signal for ADSR — ADSR applies to volume; if volume is negative, the envelope ramps a negative value

**Volume sweep mode is a separate concern:** The volume register can also be in "Sweep mode" (bit 15 set), which turns the register into a real-time sweep controller rather than a static value. In sweep mode, the hardware gradually changes the volume over time according to sweep rate/direction parameters. For v1.8, sweep mode can be implemented as a future enhancement, but the data path must not corrupt static-volume voices when sweep mode is not implemented (read bit 15, if set treat as zero volume with a FIXME rather than misinterpreting the sweep parameters as a static value).

**Prevention:**
1. Per-voice volume multiply: `output_l = q15_mul_truncate(adsr_scaled_sample, voice_vol_l)`. The `q15_mul_truncate` already handles signed values correctly via the existing `sat_s16` discipline — negative volume produces phase-inverted output automatically.
2. Do NOT clamp volume values to [0, 0x7FFF] before the multiply.
3. Check bit 15 of volume registers at voice setup: if set, volume is in sweep mode. Log a warning and treat as 0 for now. Do not attempt to extract static volume from sweep-mode bits.

**Warning signs:**
- A game-sourced sample that should have stereo panning sounds center or mono (sign-clamped L/R imbalance)
- Test patterns with negative volume produce silence instead of inverted audio
- Volume sweep mode produces garbage audio (bit 15 incorrectly treated as sign for a Q15 value)

**Phase:** P-MIXER

---

### S3: Gaussian interpolation ring buffer orientation at startup — the cold-start problem

**What goes wrong:**
When a voice keys on, its 4-element Gaussian ring buffer contains stale data from any previous use of that voice slot (or zero if the voice was never used). The real SPU has undefined history at voice key-on; emulators initialize the ring to zero. Playing note-onset audio through a ring buffer initialized to zero produces a brief soft-attack artifact at the very start of each note (the Gaussian window includes 1-3 samples of silence as history). This is documented PS1 behavior, not a bug.

The actual pitfall: after a loop jump, the ring buffer contains samples from the END of the previous loop iteration (correct), but the decoder state (`adpcm_state`) also was reset to the loop-point snapshot. If the ring buffer is NOT cleared at the loop jump (correct — it should not be cleared), but the filter state IS restored (correct), then there is a one-block transition where the ring has samples from filter-state-context-A but the decoder is running with filter-state-context-B. The result is a brief discontinuity.

The solution is counterintuitive: at the loop point, restore the filter state snapshot AND continue filling the ring with new samples. The ring transitions naturally — old samples (from the end of the previous loop) are in positions 0-2, and new samples (decoded with the restored state) start going into position 3 and onwards. Over 3 ticks, the old history is pushed out. This matches the 3-sample history carry that DuckStation implements across block boundaries.

**Prevention:**
1. On KON: clear the ring buffer to zero. The soft-attack transient is authentic.
2. On loop jump: do NOT clear the ring buffer. Restore `adpcm_state` to `loop_adpcm_state`. The ring transitions naturally over 3 samples.
3. The 3-sample transition zone at the loop point is why the loop-start filter state snapshot matters so much (C5 above) — the ring must be filled with samples decoded using the same filter state that was active when the loop-start block was first encountered.

**Warning signs:**
- Loop points produce an audible discontinuity that disappears if you add a buffer of silence before the loop point
- KON produces a click instead of a clean attack (ring not cleared at key-on)
- Long-looping samples that are identical to the original drift in pitch over multiple loop iterations (filter state accumulating error across loops)

**Phase:** P-LOOP, P-VOICEPATH

---

### S4: The "single counter" discipline must apply per voice — do not accumulate a global pitch counter

**What goes wrong:**
The coloration bus uses a single `voice_counter` in `spu94_state` — one counter for the one coloration voice. Extending this by adding `voice_counter[24]` to `spu94_state` is tempting and functional, but structurally wrong for 24-voice polyphony: it puts per-voice mutable state in the global state struct, making it impossible to add/remove voices cleanly and bloating the state struct size (which is already near `SPU94_STATE_SIZE_MAX` — see the static assert in `spu94_state_internal.h`).

The correct structure is a `spu94_voice_t` struct containing all per-voice state, with a `spu94_voice_t voices[24]` array either in `spu94_state` or managed separately.

**State size constraint:** `spu94_state` has a `SPU94_STATE_SIZE_MAX` compile-time assertion. Adding 24 voices worth of state (each voice needs: pitch_counter, current_addr, loop_start_addr, adpcm_state, loop_adpcm_state, gauss_ring[4], ring_pos, adsr_state, adsr_level, vol_l, vol_r, reverb_on flag) will grow the struct significantly. Plan: either bump `SPU94_STATE_SIZE_MAX` intentionally (and update the comment in `spu94.h`) or allocate voice state separately from the main state struct.

**Prevention:**
1. Define `spu94_voice_t` with all per-voice fields. No per-voice fields go directly into `spu94_state`.
2. The `voice_counter` field in `spu94_state` is the coloration-bus counter — rename it to `patina_counter` if this causes confusion, or just never reference it from the voice engine code.
3. Before adding voice state, check current `sizeof(spu94_state)` vs `SPU94_STATE_SIZE_MAX`. Budget: each voice needs approximately 60-80 bytes. 24 voices = ~1.5-2 KB. Add this to the current struct size and verify it fits.

**Warning signs:**
- Build fails with "spu94_state grew beyond SPU94_STATE_SIZE_MAX" immediately on adding voice array
- Voice state for voice N gets written into voice N+1's fields (adjacency aliasing in a flat array)
- Coloration bus behavior changes when switching between voice presets (shared counter desync)

**Phase:** P-VOICEPATH, P-INTEGRATE

---

### S5: Block decode triggering order — decode must happen BEFORE interpolation, not after counter advance

**What goes wrong:**
The single-counter architecture's per-tick order is strictly:
1. If `has_samples == false`: decode the next ADPCM block (all 28 samples), copy 3 history samples from the previous block.
2. Compute Gaussian interpolation from the current buffer (read current samples)
3. THEN advance the pitch counter
4. THEN check if the counter crossed a block boundary (flag for NEXT tick decode)

Reversing steps 1-2 and 3-4 means the voice reads from stale samples on the tick after a block boundary, causing a one-tick artifact. This is verified behavior from DuckStation's `SampleVoice()` function (documented in DEEP-SPU-VOICE-PATH.md §5.1).

**The subtlety:** "Decode if needed" is triggered by the counter crossing a block boundary at the END of the PREVIOUS tick, not by the counter crossing at the start of the current tick. The flag `has_samples` is cleared at the end of the tick where the boundary is crossed, triggering decode at the start of the next tick before any interpolation.

**Prevention:**
1. The per-voice tick function follows this order rigidly: check/decode → interpolate → advance → check boundary.
2. `has_samples` (or equivalent `block_valid` flag) is the gate: if false at tick start, decode immediately.
3. After advancing the counter, if `sample_index >= 28`: subtract 28 (not reset), set `has_samples = false`, advance to next block address.

**Warning signs:**
- A periodic single-sample artifact at every block boundary (28-sample period at 44.1 kHz pitch)
- The artifact is pitch-dependent (at half speed the period is 56 samples; at double speed it's 14 samples)

**Phase:** P-VOICEPATH

---

### S6: WAV intake: encoding on load vs real-time encode — don't repeat the coloration-bus pattern

**What goes wrong:**
The coloration bus encodes live audio in real-time (28 samples at a time, triggering when `adpcm_buf_pos` reaches 28). The voice engine must encode WAV files at load time, not during playback. A developer pattern-matching from the coloration bus might trigger encode during voice playback, which adds latency (brute-force 65-combination search per 28 samples) and introduces encode artifacts that grow with play time (because encode errors accumulate when encoding already-encoded data).

For v1.8, the intake pipeline is: WAV file → encode to ADPCM → write encoded blocks to SPU RAM → voice plays from SPU RAM decode-only. The encode happens ONCE, at load time, on a non-audio thread. The audio thread only decodes.

**Prevention:**
1. `spu94_voice_load_wav()` (or equivalent): reads WAV, encodes to ADPCM blocks (using `spu94_adpcm_encode_block`), writes 16-byte blocks to a caller-provided SPU RAM buffer. Returns encoded byte count.
2. The audio-thread voice path never calls `spu94_adpcm_encode_block`. Decode only.
3. The encode is front-loaded: before playback begins, all samples are in SPU RAM in their final encoded form.

**Warning signs:**
- Audio callback exhibits latency spikes at block boundaries (encode happening in audio thread)
- Sample sounds change slightly with each playback (re-encoding accumulated error)
- The voice engine `nm -u` output includes `spu94_adpcm_encode_block` as a referenced symbol

**Phase:** P-INTEGRATE, P-SPURAM

---

## Minor Pitfalls

Known traps with straightforward prevention.

---

### M1: The reverb input during voice engine operation — vLIN and vRIN are still voice-mix scalars

**What goes wrong:**
The reverb `vLIN` and `vRIN` registers scale the input fed into the reverb unit. In the existing processor, the "reverb input" is the `send_l/send_r` output of the dry/patina mixer. When the voice engine runs, the correct input to the reverb is the sum of reverb-flagged voice outputs (the voice-mixer's reverb-send subset). This sum must be fed through the same send/return path — it is NOT a separate parallel input.

If voice outputs bypass the existing `send_l/send_r` path and are written directly into the `mix_bus_l/r` mailbox (the reverb body's input), the vLIN/vRIN scaling is skipped and the dry/patina send logic is bypassed, changing the reverb character relative to the real SPU.

**Prevention:**
The voice mixer output replaces or augments the `send_l/send_r` that currently comes from the dry+patina buses. The exact architecture (whether voices replace the existing dry bus, or are added as a third bus feeding the reverb send) is a design decision, but the vLIN/vRIN scaling path in `spu94_reverb_body` must remain intact.

**Phase:** P-INTEGRATE

---

### M2: ADSR sustain level 0 is NOT silence — it is 0x800 (minimum audible level)

**What goes wrong:**
nocash defines sustain level as `(N+1) * 0x800` where N is the 4-bit register value (0..15). So N=0 → sustain level = 0x800 (2048, not zero). A developer who reads the sustain level as `N * 0x800` will produce notes that fade to silence instead of holding at a soft sustain.

**Prevention:**
Sustain level decoding: `uint16_t sl = ((sustain_level_reg & 0xF) + 1) << 11` (equivalent to `* 0x800`). Add a test case: sustain level 0 must hold at a non-zero volume after the decay phase.

**Phase:** P-ADSR

---

### M3: ENDX register must be cleared on KON, not on KOFF

**What goes wrong:**
The ENDX register bit for a voice is set when the voice reaches a Loop-End block. It is CLEARED when KON is issued for that voice. It is NOT cleared by KOFF (a voice in release can still have ENDX set from a previous loop-end). Getting this wrong causes ENDX polling logic to think a voice is still available for reuse when it is still playing release.

**Prevention:**
On KON: `endx_register &= ~(1 << voice_index)`. On KOFF: do not touch ENDX. On Loop-End: `endx_register |= (1 << voice_index)`.

**Phase:** P-KEYING

---

### M4: Noise generator — the PSX noise generator is NOT white noise

**What goes wrong:**
Some voices can be set to noise mode (NOISE bit in SPU channel attribute register). The real SPU uses a linear feedback shift register (LFSR) clocked at rates defined by the noise clock register. The noise output is band-limited by the LFSR feedback polynomial, not truly flat-spectrum. Using a standard PRNG or `rand()` produces statistically different noise that will not pass a witness comparison.

For v1.8 MVP: noise mode can be stubbed as silence (or a simple LFSR placeholder) with a FIXME. Do not use `rand()` in the voice path.

**Phase:** P-VOICEPATH (stub), future cleanup

---

### M5: The 22.05 kHz reverb rate vs the 44.1 kHz voice rate — the reverb runs at half rate

**What goes wrong:**
The existing reverb (`spu94_tick`) runs at 22.05 kHz (once per two 44.1 kHz ticks, via the FIR decimation chain). Voice playback runs at 44.1 kHz. The voice mixer output (at 44.1 kHz) must be properly handled before being fed to the 22.05 kHz reverb input. In the existing architecture, the `spu94_fir_chain_step` handles this decimation — the voice mixer output must feed the same path, not bypass it.

**Prevention:**
The voice mixer sum feeds `send_l/send_r` in `spu94_process`, which already feeds `spu94_fir_chain_step`. The voice engine slots into the existing mixer architecture — it does not call `spu94_tick` directly.

**Phase:** P-INTEGRATE

---

### M6: Block address arithmetic — SPU RAM uses byte addresses, but ADPCM blocks are 16 bytes

**What goes wrong:**
SPU RAM addresses in nocash documentation are byte addresses. Each ADPCM block is 16 bytes. When advancing to the next block, the current address must increment by 16 (not by 1, not by 28). If a sample has 100 blocks, the valid block addresses are: `start`, `start+16`, `start+32`, ..., `start+1584`. Using sample-count increments (+=28) or nibble increments (+=1) produces wrong addresses.

**Prevention:**
Block advance: `voice->current_addr += 16`. All SPU RAM accesses are `spu_ram[voice->current_addr + byte_offset]` where byte_offset is 0..15.

**Phase:** P-VOICEPATH

---

## Integration Pitfalls — Voice Engine + Existing libspu94

| Integration point | Common mistake | Correct approach |
|---|---|---|
| ADPCM codec reuse | Route voice decode through coloration-bus encode+decode | Call `spu94_adpcm_decode_block` directly from voice path — decode only |
| Gaussian table | Assume spu94_gauss_table is instantiated once — yes, it is const in .rodata; it IS shared (that is fine; it is read-only) | Reference `spu94_gauss_table` from the voice path directly; it is a shared read-only constant |
| Gauss ring buffer | Use `state->gauss_ring_l/r` from the coloration bus | Each voice has its own `gauss_ring[4]` in `spu94_voice_t` |
| Pitch counter | Add `voice_counter[24]` to `spu94_state` | Use `spu94_voice_t.pitch_counter` per voice |
| Reverb routing | Feed voice sum directly to `mix_bus_l/r` | Voice sum feeds `send_l/send_r` path which feeds `spu94_fir_chain_step` |
| rt_safety | Add voice state allocation in per-sample callback | Allocate voice state array in init/prepareToPlay; zero heap in audio path |
| State struct | Add 24 voice structs to `spu94_state` without checking size | Measure, then bump `SPU94_STATE_SIZE_MAX` with an explicit comment |
| Golden files | Expect existing coloration-bus goldens to still pass | Existing goldens pass ONLY if voice engine defaults off; add separate voice-engine golden set |

---

## Phase-to-Pitfall Reference

| Phase | Pitfalls addressed | Severity |
|---|---|---|
| P-VOICEPATH | C1 (ADPCM path conflation), C2 (ring buffer isolation), C7 (pitch clamp/modulation), S4 (per-voice counter), S5 (decode-before-interpolate order), M4 (noise), M6 (block address arithmetic) | Critical, Critical, Critical, Significant, Significant, Minor, Minor |
| P-ADSR | C3 (counter mechanism, exponential modes, decay target), M2 (sustain level 0) | Critical, Minor |
| P-LOOP | C4 (flag semantics), C5 (filter state at loop point), S3 (ring buffer at loop jump) | Critical, Critical, Significant |
| P-SPURAM | C6 (address space collision), S6 (load-time vs real-time encode) | Critical, Significant |
| P-KEYING | C8 (KON/KOFF timing), M3 (ENDX behavior) | Critical, Minor |
| P-MIXER | S1 (24-voice sum overflow), S2 (negative volume = phase inversion), M1 (vLIN/vRIN path) | Significant, Significant, Minor |
| P-INTEGRATE | C1 (codec path), C2 (ring isolation), S4 (state struct size), S6 (encode path), M5 (22.05 kHz reverb rate) | Cross-cutting; identified in respective critical pitfalls |
| P-VERIFY | All — verify each pitfall's prevention via targeted tests | — |

---

## Recovery Costs

| Pitfall | Recovery if discovered late | Cost |
|---|---|---|
| C1: Codec path conflation | Rewire voice path to bypass encode; isolate state | MEDIUM — code surgery but no data loss |
| C2: Ring buffer sharing | Allocate per-voice rings; move state to `spu94_voice_t` | MEDIUM-HIGH — struct refactor |
| C3: ADSR counter wrong | Replace rate tables / counter logic | MEDIUM — ADSR module is isolated |
| C4: Loop flag wrong | Fix flag parsing; re-test all looped samples | LOW — flag byte parsing is a few lines |
| C5: Filter state at loop | Add `loop_adpcm_state` snapshot and restore | LOW — few lines, but requires test with looped samples |
| C6: SPU RAM collision | Separate RAM allocation; enforce partition | HIGH — affects addressing throughout |
| C7: Pitch clamp missing | Add one-line clamp | LOW — but may require finding all pitch advance sites |
| C8: KON/KOFF mid-tick | Add pending mask; defer application to tick boundary | LOW-MEDIUM |
| S1: Mixer overflow | Change accumulator to int32 | LOW |
| S2: Negative volume clamp | Remove the incorrect clamp | LOW |
| S4: Global counter array | Refactor to `spu94_voice_t` | MEDIUM-HIGH — all voice state access sites |
| S5: Decode order wrong | Swap decode/advance order in tick loop | LOW |
| S6: Real-time encode | Move encode to load path | MEDIUM — requires load API design |

---

## Sources

### PRIMARY (HIGH confidence — verified against codebase and spec)

- `/home/ubuntu-studio/Desktop/PSX Reverb/src/spu94/spu94_adpcm.c` — existing ADPCM decoder; flag byte return; filter state carry
- `/home/ubuntu-studio/Desktop/PSX Reverb/src/spu94/spu94_gauss.c` — 512-entry Gauss table; 3-way verified (nocash, DuckStation, Mednafen)
- `/home/ubuntu-studio/Desktop/PSX Reverb/src/spu94/spu94_process.c` — single-counter voice architecture already shipped; ring buffer orientation; Gaussian index extraction
- `/home/ubuntu-studio/Desktop/PSX Reverb/src/spu94/spu94_state_internal.h` — current state struct fields; SPU94_STATE_SIZE_MAX constraint
- `/home/ubuntu-studio/Desktop/PSX Reverb/.planning/research/DEEP-SPU-VOICE-PATH.md` — single-counter architecture verification across DuckStation, Mednafen, PlayStation1Vsts; Gaussian edge cases; block boundary behavior; sections 1-10

### PRIMARY (HIGH confidence — spec)

- [nocash psx-spx SPU page](https://psx-spx.consoledev.net/soundprocessingunitspu/) — ADSR counter formula, loop flag bit definitions, KON/KOFF register descriptions, pitch clamp (Step=MIN(Step,3FFFh)), sustain level formula ((N+1)*0x800), ENDX clear-on-KON behavior

### SECONDARY (MEDIUM confidence — emulator consensus)

- DuckStation `spu.cpp` — `SampleVoice()` processing order (decode before interpolate, counter advance after); `DecodeBlock()` 3-sample history carry; `Voice::ProcessAdsr()` counter mechanism
- Mednafen `beetle-psx spu.c` — `SPU_RunDecoder()` block decode triggers; ADSR phase transitions
- PlayStation1Vsts `Spu.cpp` — `stepVoice()` loop flag handling; ENDX behavior

---

*Pitfalls research for: v1.8 PSX Voice Engine milestone*
*Researched: 2026-05-16*
