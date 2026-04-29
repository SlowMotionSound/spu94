# Phase 7: Pipeline Integration - Research

**Researched:** 2026-04-29
**Domain:** C DSP pipeline architecture -- send/return mixer with DAC coloration stage
**Confidence:** HIGH

## Summary

Phase 7 replaces the current single-path `spu94_process` with a send/return mixer architecture. The current flow is: input -> optional ADPCM -> decimator -> reverb -> interpolator -> output, with the JUCE host owning a wet/dry crossfade. The new flow is: input gain -> signal splits into dry bus and patina (ADPCM) bus -> two independent reverb sends (dry send + patina send) -> reverb (100% wet) -> three-fader master mixer (dry/patina/reverb) -> DAC section -> output. All DSP moves into the C core; the JUCE wet/dry crossfade gets deleted.

The implementation extends three proven patterns from the existing codebase: (1) the ADPCM toggle pattern from v1.1 Phase 2 for all new toggles, (2) the ADPCM double-buffer for the 28-sample latency compensation delay on the dry bus, and (3) the per-channel state embedding pattern for DAC FIR and noise modules. The struct budget is comfortable -- current size is 792 bytes, new fields add ~443 bytes (with padding), well under the 16384 cap.

The most subtle integration point is the DAC noise module's LFSR seed requirement. Unlike every other field in `spu94_state`, the noise LFSR cannot be zero-initialized -- zero is an absorbing state that produces silence forever. The `spu94_init` and `spu94_reset` functions use `spu94_zero_bytes` on the entire struct, so they must explicitly call `spu94_dac_noise_init()` after zeroing to plant the non-zero seed. This also creates an opportunity to address CR-01 (WR-02 from the Phase 6 review) by using different seeds per channel.

**Primary recommendation:** Split into three plans: (1) State struct expansion + public API declarations + init/reset fixup for DAC noise seed, (2) Mixer architecture rewrite of spu94_process + JUCE wet/dry deletion, (3) Integration tests covering all mixer paths, DAC section, and latency compensation.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Send/return mixer architecture. Input gain -> signal splits into dry bus and patina (ADPCM) bus -> two independent reverb sends (dry send + patina send) -> reverb (100% wet) -> three-fader master mixer (dry/patina/reverb) -> DAC section -> output.
- **D-02:** All DSP signal flow lives in the C core. JUCE, CLI, and Python are thin wrappers with no DSP logic. The existing JUCE wet/dry crossfade code in PluginProcessor.cpp gets deleted and replaced with straight passthrough of spu94_process output.
- **D-03:** ADPCM position is currently fixed (before the dry split), but avoid hardwiring it -- future milestone may allow repositioning in the signal chain.
- **D-04:** Six controls plus DAC section: input gain, dry fader, patina fader, dry reverb send, patina reverb send, reverb fader.
- **D-05:** All fader/send values use Q15 int16 (0x0000-0x7FFF), matching the existing SPU register value format. No floating-point in the C core. Hosts convert their float knobs to Q15 at the API boundary.
- **D-06:** No parameter smoothing/slew in the C core. Values land as raw register writes -- abrupt changes produce clicks and digital stepping artifacts, which is intentional character.
- **D-07:** Compensate the 28-sample ADPCM block delay with a matching delay buffer on the dry bus, so both arrive at the mixer time-aligned. Default: compensation ON.
- **D-08:** Compensation is toggleable via `spu94_set_latency_comp(state, 1/0)`. When OFF, the 28-sample offset between dry and patina buses creates comb filtering -- musically useful as a creative effect.
- **D-09:** DAC is a section with a master toggle and two sub-toggles. `spu94_set_dac_enabled()` is the parent switch. FIR and noise each have independent sub-toggles.
- **D-10:** Signal order within DAC section: FIR first, then noise added on top. Matches hardware.
- **D-11:** All three toggles ON = faithful PS1 DAC behavior. Users can run just FIR, just noise, or neither.
- **D-12:** DAC section processes the master mixer output -- it colors the final mixed signal, not individual buses.

### Claude's Discretion
- Internal organization of the mixer code within spu94_process.c
- Naming conventions for internal helper functions
- Test structure and organization

### Deferred Ideas (OUT OF SCOPE)
- **Parameter slew/smoothing control** -- M4 real-time lever layer
- **Movable ADPCM insert point** -- future milestone
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DAC-INT-01 | DAC model inserted at 44.1kHz after spu94_fir_chain_step output, toggleable via spu94_set_dac_enabled(), default-off, following the ADPCM toggle pattern | Signal flow analysis confirms DAC section sits at the end of the master mixer output, after interpolation back to 44.1kHz; toggle pattern from spu94_io_chain.c lines 148-178 is the direct template |
| DAC-INT-02 | DAC state contained within spu94_state budget, disable resets filter/noise state cleanly, zero regression on all existing tests with DAC disabled | Current struct is 792 bytes; new fields add ~443 bytes = ~1235 total; cap is 16384 with 15149 headroom; zero-init gives default-off for all new features |
| DAC-INT-03 | All rt_safety gates (rt_no_heap, rt_no_locks, rt_no_syscalls, rt_bench_latency) pass with DAC enabled | DAC FIR and noise modules are already compiled into libspu94 and use no heap/locks/syscalls; Q15 fixed-point math only |

**NOTE:** The CONTEXT.md architecture (D-01 through D-12) substantially expands beyond the original ROADMAP.md DAC-INT requirements. The original requirements described a simple DAC toggle; the actual scope includes the full mixer architecture, latency compensation, and six fader controls. DAC-INT-01/02/03 are satisfied as a subset of the larger architecture.
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Send/return mixer (bus splitting, fader math, mixing) | DSP core (libspu94) | -- | Pure arithmetic on Q15 samples in the hot path |
| DAC coloration (FIR + noise) | DSP core (libspu94) | -- | Must run identically in every consumer (CLI, Python, JUCE) |
| Latency compensation delay buffer | DSP core (libspu94) | -- | State-dependent processing, same tier as ADPCM double-buffer |
| Mixer control API (setters/getters) | Public C API (spu94.h) | -- | Follows existing spu94_set/get pattern |
| Wet/dry crossfade (current) | JUCE host (DELETION) | -- | D-02: this code gets deleted, replaced by passthrough |
| Float-to-Q15 knob conversion | Host tier (JUCE/CLI/Python) | -- | D-05: hosts own the float-to-int16 conversion |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libspu94 (this project) | HEAD | Reverb DSP engine being extended | The thing we're modifying |
| spu94_dac_fir.h/c | Phase 6 | DAC interpolation filter module | Already built and tested; slots into DAC section |
| spu94_dac_noise.h/c | Phase 6 | DAC noise shaping module | Already built and tested; slots into DAC section |
| spu94_q15.h | Phase 1 | Q15 arithmetic primitives (q15_mul_truncate, sat_s16, q15_add_sat) | All mixer fader math uses these |

### Supporting
No additional libraries needed. Phase 7 is pure internal wiring within existing C codebase.

## Architecture Patterns

### System Architecture Diagram -- Current Signal Flow

```
44.1 kHz input (L, R int16)
        |
        v
+-------------------------------------+
| spu94_process()                      |
|   for each sample:                   |
|     l, r = input (NULL -> 0)         |
|         |                            |
|     [if adpcm_enabled]               |
|       ADPCM encode+decode            |
|       (28-sample double-buffer)      |
|         |                            |
|         v                            |
|     spu94_fir_chain_step()           |
|       decimate (44.1->22.05)         |
|       spu94_tick() [reverb body]     |
|       interpolate (22.05->44.1)      |
|         |                            |
|         v                            |
|     lo, ro = output                  |
+-------------------------------------+
        |
        v
44.1 kHz output (L, R int16)
        |
        v  [JUCE ONLY]
+-------------------------------------+
| PluginProcessor::processBlock        |
|   equal-power wet/dry crossfade      |
|   dryGain * input + wetGain * spu    |
+-------------------------------------+
```

### System Architecture Diagram -- After Phase 7

```
44.1 kHz input (L, R int16)
        |
        v
+-------------------------------------+
| spu94_process()                      |
|   for each sample:                   |
|                                      |
|   1. INPUT GAIN                      |
|      l = q15_mul(l, input_gain)      |
|      r = q15_mul(r, input_gain)      |
|                                      |
|   2. ADPCM (fixed position)         |
|      [if adpcm_enabled]              |
|        patina_l,r = ADPCM(l,r)       |
|      [else]                          |
|        patina_l,r = l,r              |
|                                      |
|   3. DRY BUS (latency comp)         |
|      [if latency_comp && adpcm_on]   |
|        dry_l,r = delay_28(l,r)       |
|      [else]                          |
|        dry_l,r = l,r                 |
|                                      |
|   4. REVERB SENDS                    |
|      send = q15_mul(dry, dry_send)   |
|           + q15_mul(patina, pat_send)|
|      feed send into chain_step       |
|        -> decimate -> tick -> interp |
|      rev_l,r = chain_step output     |
|                                      |
|   5. MASTER MIXER                    |
|      out = q15_mul(dry, dry_fader)   |
|          + q15_mul(patina, pat_fader)|
|          + q15_mul(rev, rev_fader)   |
|                                      |
|   6. DAC SECTION                     |
|      [if dac_enabled]                |
|        [if dac_fir_enabled]          |
|          out = dac_fir_step(out)     |
|        [if dac_noise_enabled]        |
|          out += dac_noise_step()     |
|                                      |
|   7. OUTPUT                          |
|      L_out = out_l, R_out = out_r    |
+-------------------------------------+
        |
        v
44.1 kHz output (L, R int16)
  (hosts pass through directly)
```

### Recommended Project Structure

No new files needed. Modifications to existing files only:
```
include/spu94/spu94.h               # New public API: mixer setters/getters, DAC toggles, latency comp
src/spu94/spu94_state_internal.h     # New fields: faders, sends, delay buffer, DAC state
src/spu94/spu94_process.c            # Complete rewrite of process loop to mixer architecture
src/spu94/spu94_io_chain.c           # New toggle/getter implementations (or spu94_process.c)
src/spu94/spu94_state.c              # Init/reset fixup for DAC noise seed
src/standalone/PluginProcessor.cpp   # Delete wet/dry crossfade, replace with passthrough
src/standalone/PluginProcessor.h     # Delete wetDry atomic member (or repurpose)
tests/unit/process/                  # New integration tests
```

### Pattern 1: Q15 Fader Multiplication
**What:** Each bus fader multiplies a Q15 signal by a Q15 gain value. The existing `q15_mul_truncate` handles this exactly -- it's the same math the reverb body uses for vIIR, vWALL, etc.
**When to use:** Every fader and send level application.
**Example:**
```c
// Source: existing q15_mul_truncate pattern from spu94_q15.h
// Apply dry fader to dry bus
int16_t dry_l_faded = q15_mul_truncate(dry_l, state->dry_fader);
int16_t dry_r_faded = q15_mul_truncate(dry_r, state->dry_fader);
```
[VERIFIED: spu94_q15.h lines 109-111, same function used throughout reverb body]

### Pattern 2: Three-Bus Sum at Master Mixer
**What:** The master mixer sums three buses (dry, patina, reverb), each scaled by its fader. Three Q15 multiplications followed by saturating addition.
**When to use:** The master mixer stage.
**Example:**
```c
// Source: derived from q15 helpers + reverb body mixing pattern
int32_t mix_l = (int32_t)q15_mul_truncate(dry_l, state->dry_fader)
              + (int32_t)q15_mul_truncate(patina_l, state->patina_fader)
              + (int32_t)q15_mul_truncate(rev_l, state->reverb_fader);
int16_t out_l = sat_s16(mix_l);
```
**Note on overflow:** Three Q15 products each in [-32768, 32767]. Sum range is [-98304, 98301], which fits in int32. `sat_s16` clamps to int16 range. This matches the reverb body's hard-clip pattern. [VERIFIED: q15_add_sat and sat_s16 in spu94_q15.h]

### Pattern 3: Reverb Send Mixing
**What:** The reverb input is the sum of two scaled signals: dry bus * dry_send + patina bus * patina_send. This replaces the current direct feed from ADPCM output into chain_step.
**When to use:** Feeding the reverb from the two buses.
**Critical change:** Currently `spu94_fir_chain_step` receives the (post-ADPCM) signal directly. After Phase 7, it receives the send mix instead. The chain_step internals (decimator -> tick -> interpolator) are unchanged.
```c
// Reverb send mix
int32_t send_l = (int32_t)q15_mul_truncate(dry_l, state->dry_send)
               + (int32_t)q15_mul_truncate(patina_l, state->patina_send);
int16_t reverb_in_l = sat_s16(send_l);
// Feed into existing FIR chain
spu94_fir_chain_step(state, reverb_in_l, reverb_in_r, &rev_out_l, &rev_out_r);
```
[VERIFIED: spu94_io_chain.c chain_step_impl accepts any int16 pair as input]

### Pattern 4: 28-Sample Latency Compensation Delay
**What:** A simple ring buffer that delays the dry bus by 28 samples to time-align with the ADPCM output. Same concept as the ADPCM double-buffer, but simpler -- just a circular delay line.
**When to use:** When latency_comp is ON and ADPCM is enabled.
**Example:**
```c
// Source: derived from ADPCM double-buffer pattern in spu94_process.c
// 28-sample stereo ring buffer
if (state->latency_comp && state->adpcm_enabled) {
    int16_t delayed_l = state->delay_buf_l[state->delay_pos];
    int16_t delayed_r = state->delay_buf_r[state->delay_pos];
    state->delay_buf_l[state->delay_pos] = dry_l;
    state->delay_buf_r[state->delay_pos] = dry_r;
    state->delay_pos = (state->delay_pos + 1) % 28;
    dry_l = delayed_l;
    dry_r = delayed_r;
}
```
**Note:** The `% 28` modulo can be replaced with an `if` comparison for performance: `if (++state->delay_pos >= 28) state->delay_pos = 0;` -- same pattern used in the ADPCM accumulator.
[VERIFIED: ADPCM buf_pos pattern in spu94_process.c lines 53-54]

### Pattern 5: DAC Noise Init Fixup
**What:** The DAC noise LFSR MUST be initialized to a non-zero seed. Unlike all other state fields where zero-init is correct, `lfsr = 0` is an absorbing state (silence forever). `spu94_init` and `spu94_reset` both zero the entire struct via `spu94_zero_bytes`, then must explicitly call `spu94_dac_noise_init()` for each channel's noise state.
**When to use:** In `spu94_state.c` init and reset functions.
**Example:**
```c
// In spu94_init, after spu94_zero_bytes(s, sizeof(*s)):
spu94_dac_noise_init(&s->dac_noise_l);
spu94_dac_noise_init(&s->dac_noise_r);

// In spu94_reset, after spu94_zero_bytes(state, sizeof(*state)):
spu94_dac_noise_init(&state->dac_noise_l);
spu94_dac_noise_init(&state->dac_noise_r);
```
**Critical:** Without this, every DAC noise instance starts at lfsr=0 and produces silence. The `spu94_dac_noise_init` header warning documents this explicitly.
[VERIFIED: spu94_dac_noise.h warning comment lines 12-16, spu94_state.c init/reset pattern]

### Pattern 6: DAC Toggle with State Reset on Disable
**What:** Follows the ADPCM toggle pattern exactly. Master toggle gates both sub-features. Each toggle resets its module state on disable for clean re-enable.
**When to use:** All four new toggles (DAC master, FIR sub, noise sub, latency comp).
**Example:**
```c
// Source: spu94_set_adpcm_enabled pattern in spu94_io_chain.c lines 148-166
void spu94_set_dac_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->dac_enabled) {
        // Reset FIR state
        spu94_dac_fir_init(&state->dac_fir_l);
        spu94_dac_fir_init(&state->dac_fir_r);
        // Reset noise state (with non-zero seed!)
        spu94_dac_noise_init(&state->dac_noise_l);
        spu94_dac_noise_init(&state->dac_noise_r);
    }
    state->dac_enabled = enabled ? 1 : 0;
}
```
[VERIFIED: spu94_io_chain.c lines 148-166 is the direct template]

### Anti-Patterns to Avoid
- **Floating-point in the C core:** D-05 mandates Q15 int16 for all controls. No `float` or `double` types in any C source file being modified. Hosts convert at the API boundary.
- **DAC section on individual buses:** D-12 says DAC processes the master mixer output, not individual buses. Don't apply DAC FIR/noise to each bus separately.
- **Modifying chain_step_impl internals:** The reverb path (decimate -> tick -> interpolate) is unchanged. Only the wrapper around it changes -- what goes IN (send mix instead of raw signal) and what comes OUT (fed into master mixer instead of directly to output).
- **Hardwiring ADPCM position:** D-03 says avoid hardwiring. The ADPCM stage should be a clear, separable block that could be repositioned in a future milestone. Don't interleave ADPCM logic with mixer logic.
- **Zero-initializing DAC noise state:** LFSR=0 is absorbing. Always use `spu94_dac_noise_init()`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Q15 fader multiplication | Manual shift/saturate | `q15_mul_truncate()` from spu94_q15.h | Already proven in reverb body; handles INT16_MIN edge case |
| Saturating bus sum | Manual clamping | `sat_s16()` from spu94_q15.h | Handles int32->int16 saturation correctly |
| DAC FIR filtering | New filter code | `spu94_dac_fir_step()` from Phase 6 | Three-stage cascade already tested |
| DAC noise generation | New noise code | `spu94_dac_noise_step()` from Phase 6 | LFSR + 2nd-order HP already tested |
| State zeroing | memset | `spu94_zero_bytes()` in spu94_state.c | Existing pattern; avoids `<string.h>` dependency |

**Key insight:** Phase 7 is predominantly WIRING, not new DSP. Every DSP primitive (Q15 math, reverb, ADPCM, DAC FIR, DAC noise) already exists and is tested. The new code is control flow -- bus routing, fader application, and toggle gating.

## Common Pitfalls

### Pitfall 1: DAC Noise LFSR Zero-Init
**What goes wrong:** LFSR initialized to 0 produces silence forever. No noise output, no error, just silent DAC noise.
**Why it happens:** `spu94_init` and `spu94_reset` zero the entire struct. The noise module is the ONLY module where zero-init is incorrect.
**How to avoid:** After every `spu94_zero_bytes` call in init/reset, explicitly call `spu94_dac_noise_init()` for each channel. Add a comment explaining why.
**Warning signs:** DAC noise toggle appears to do nothing; test for noise amplitude shows 0 RMS.

### Pitfall 2: Send/Fader Default Values
**What goes wrong:** All new Q15 fields zero-init to 0x0000, which means all faders and sends are at ZERO. With default values, spu94_process produces silence -- input gain = 0 means no signal enters the mixer.
**Why it happens:** Zero-init is correct for toggles (off) but wrong for faders if you expect audio.
**How to avoid:** Document clearly that hosts MUST set fader values before expecting audio. OR provide a helper function that sets sensible defaults (input_gain = 0x7FFF, dry_fader = 0x7FFF, reverb_fader = 0x7FFF, sends = 0x7FFF, patina_fader = 0x0000). The "zero means silence" behavior is actually correct for a mixer -- it's how a real console works when you haven't pushed the faders up.
**Warning signs:** No audio output after Phase 7 until faders are explicitly set.

### Pitfall 3: JUCE Wet/Dry Deletion Breaking Audio Path
**What goes wrong:** Deleting the JUCE wet/dry crossfade without also setting the C core faders to sensible defaults means the JUCE standalone produces silence.
**Why it happens:** The C core faders default to zero. The JUCE code previously did its own mixing; now it relies on the C core which starts silent.
**How to avoid:** The JUCE initialization code must call the new setter functions to configure default fader positions. The PluginProcessor constructor (or wherever spu94_init is called) must set input_gain, dry_fader, and reverb_fader to 0x7FFF to approximate the previous behavior.
**Warning signs:** JUCE standalone launches but plays silence.

### Pitfall 4: Reverb Send Path Change
**What goes wrong:** The reverb currently receives the post-ADPCM signal directly. After Phase 7, it receives the send mix. If the send levels default to 0, no signal reaches the reverb and the output is dry-only.
**Why it happens:** Zero-init sends = no reverb feed.
**How to avoid:** Same as Pitfall 2 -- document that sends must be set to non-zero for reverb to receive signal.
**Warning signs:** Reverb tail disappears; output sounds completely dry.

### Pitfall 5: Latency Compensation When ADPCM is Disabled
**What goes wrong:** If latency compensation delay runs when ADPCM is disabled, the dry bus gets a gratuitous 28-sample delay for no reason.
**Why it happens:** Delay buffer logic not gated on adpcm_enabled.
**How to avoid:** The delay only runs when BOTH latency_comp=1 AND adpcm_enabled=1. When ADPCM is off, there's no latency to compensate.
**Warning signs:** 28-sample offset between dry input and dry output when ADPCM is off.

### Pitfall 6: spu94_flush Behavior with Mixer Architecture
**What goes wrong:** `spu94_flush` calls `spu94_process` with NULL inputs (zeros). With the mixer, zeros flow through input gain (Q15 mul with 0 input = 0 regardless of gain), into both buses, and through the reverb. This is correct -- the reverb tail decays naturally because the reverb's internal state continues producing output from its work buffer.
**Why it happens:** Not a pitfall if implemented correctly. The flush path works because the reverb body runs on its accumulated state, not on current input.
**How to avoid:** Don't add special flush logic. The existing delegation to spu94_process with NULL inputs handles the mixer correctly.
**Warning signs:** None expected if mixer math is correct for zero inputs.

### Pitfall 7: Three-Bus Sum Overflow in int16
**What goes wrong:** If all three buses contribute non-zero values and the sum exceeds int16 range, you get hard clipping (which is correct per PS1 behavior).
**Why it happens:** Three scaled Q15 signals summed. Each can be up to 32767; sum can be up to ~98000.
**How to avoid:** Use int32 for the summation, then `sat_s16()` for the final result. This is INTENTIONAL -- the hard clip is authentic PS1 behavior (same pattern as the reverb body's mix bus).
**Warning signs:** None -- this is desired behavior. Test should verify it.

### Pitfall 8: Phase 6 Review Findings (CR-01, WR-02)
**What goes wrong:** The Phase 6 code review found three issues. CR-01 (int32 overflow risk in DAC FIR accumulator) should be addressed during integration to prevent UB. WR-02 (deterministic LFSR seed) is addressable by passing different seeds per channel.
**Why it happens:** Phase 6 modules were built standalone; integration is the natural time to address review findings.
**How to avoid:** (CR-01) Widen the FIR accumulator multiply to int64. (WR-02) Modify `spu94_dac_noise_init` to accept a seed parameter, or call it twice with different seeds. API change to noise init header is needed either way for the seed parameter.
**Warning signs:** CR-01 is undefined behavior under specific coefficient/input combinations. WR-02 means identical noise on L and R channels (mono-correlated noise).

## Code Examples

### New spu94_state Fields
```c
// In spu94_state_internal.h, add new block:
// Source: derived from ADPCM field pattern + CONTEXT.md D-01 through D-12

/* -----------------------------------------------------------------
 * Phase 7 (DAC-INT / Mixer): send/return mixer state.
 * Six Q15 faders/sends, latency compensation delay buffer,
 * DAC section toggles and module state.
 * ----------------------------------------------------------------- */

/* Mixer controls -- Q15 int16, range [0x0000, 0x7FFF] (D-05) */
int16_t        input_gain;       /* applied before bus split */
int16_t        dry_fader;        /* dry bus level at master mixer */
int16_t        patina_fader;     /* patina (ADPCM) bus level at master mixer */
int16_t        dry_send;         /* dry bus -> reverb send level */
int16_t        patina_send;      /* patina bus -> reverb send level */
int16_t        reverb_fader;     /* reverb return level at master mixer */

/* Latency compensation (D-07, D-08) */
uint8_t        latency_comp;     /* 1=on (default), 0=off */
uint8_t        delay_pos;        /* ring buffer write position, 0..27 */
int16_t        delay_buf_l[28];  /* 28-sample delay, L channel */
int16_t        delay_buf_r[28];  /* 28-sample delay, R channel */

/* DAC section (D-09 through D-12) */
uint8_t        dac_enabled;      /* master toggle, 0=off (default) */
uint8_t        dac_fir_enabled;  /* FIR sub-toggle, 0=off (default) */
uint8_t        dac_noise_enabled;/* noise sub-toggle, 0=off (default) */
spu94_dac_fir_state   dac_fir_l; /* FIR state, L channel */
spu94_dac_fir_state   dac_fir_r; /* FIR state, R channel */
spu94_dac_noise_state dac_noise_l;/* noise state, L channel */
spu94_dac_noise_state dac_noise_r;/* noise state, R channel */
```
Size impact: 12 (faders) + 2 (comp toggle/pos) + 112 (delay bufs) + 3 (DAC toggles) + 298 (DAC FIR 2ch) + 16 (DAC noise 2ch) = ~443 bytes. Current struct is 792. New total ~1235, well under 16384 cap. The existing _Static_assert gates this automatically. [VERIFIED: sizeof(spu94_state) = 792 from runtime query]

### Public API Additions
```c
// In spu94.h -- new mixer API section:

/* -----------------------------------------------------------------------
 * Send/return mixer controls (Phase 7, D-01 through D-06)
 *
 * Six Q15 fader/send values control the mixer architecture:
 *   input_gain:   scales input before bus split
 *   dry_fader:    dry bus level at master mixer
 *   patina_fader: patina (ADPCM) bus level at master mixer
 *   dry_send:     dry bus contribution to reverb input
 *   patina_send:  patina bus contribution to reverb input
 *   reverb_fader: reverb return level at master mixer
 *
 * All values are Q15 int16 in range [0x0000, 0x7FFF].
 * Default: all zero (silence). Hosts must set before expecting audio.
 * No parameter smoothing -- values land immediately (D-06).
 * ----------------------------------------------------------------------- */

void     spu94_set_input_gain(spu94_state *state, int16_t gain);
int16_t  spu94_get_input_gain(const spu94_state *state);

void     spu94_set_dry_fader(spu94_state *state, int16_t level);
int16_t  spu94_get_dry_fader(const spu94_state *state);

void     spu94_set_patina_fader(spu94_state *state, int16_t level);
int16_t  spu94_get_patina_fader(const spu94_state *state);

void     spu94_set_dry_send(spu94_state *state, int16_t level);
int16_t  spu94_get_dry_send(const spu94_state *state);

void     spu94_set_patina_send(spu94_state *state, int16_t level);
int16_t  spu94_get_patina_send(const spu94_state *state);

void     spu94_set_reverb_fader(spu94_state *state, int16_t level);
int16_t  spu94_get_reverb_fader(const spu94_state *state);

/* -----------------------------------------------------------------------
 * Latency compensation (Phase 7, D-07, D-08)
 *
 * When ADPCM is enabled, it introduces a 28-sample block delay.
 * Latency compensation adds a matching 28-sample delay to the dry bus
 * so both arrive at the master mixer time-aligned.
 * ON by default. OFF creates intentional comb filtering (creative effect).
 * Only active when ADPCM is also enabled.
 * ----------------------------------------------------------------------- */

void     spu94_set_latency_comp(spu94_state *state, int enabled);
int      spu94_get_latency_comp(const spu94_state *state);

/* -----------------------------------------------------------------------
 * DAC coloration section (Phase 7, D-09 through D-12)
 *
 * Master toggle + two independent sub-toggles.
 * When master is off, no DAC processing runs.
 * When master is on, FIR and noise each have independent sub-toggles.
 * All three on = faithful PS1 DAC behavior.
 * Default: all off.
 * ----------------------------------------------------------------------- */

void     spu94_set_dac_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_enabled(const spu94_state *state);

void     spu94_set_dac_fir_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_fir_enabled(const spu94_state *state);

void     spu94_set_dac_noise_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_noise_enabled(const spu94_state *state);
```
[VERIFIED: naming follows existing spu94_set/get_adpcm_enabled pattern]

### Modified spu94_process (Skeleton)
```c
// Source: pattern derived from current spu94_process.c + CONTEXT.md D-01
void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples) {
    if (state == NULL) return;
    for (uint32_t i = 0; i < num_samples; i++) {
        int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;

        /* 1. Input gain */
        l = q15_mul_truncate(l, state->input_gain);
        r = q15_mul_truncate(r, state->input_gain);

        /* 2. ADPCM coloration -> patina bus */
        int16_t patina_l, patina_r;
        if (state->adpcm_enabled) {
            /* ... existing ADPCM double-buffer logic ... */
            patina_l = /* decoded output */;
            patina_r = /* decoded output */;
        } else {
            patina_l = l;
            patina_r = r;
        }

        /* 3. Dry bus with latency compensation */
        int16_t dry_l = l, dry_r = r;
        if (state->latency_comp && state->adpcm_enabled) {
            /* 28-sample ring buffer delay */
            int16_t delayed_l = state->delay_buf_l[state->delay_pos];
            int16_t delayed_r = state->delay_buf_r[state->delay_pos];
            state->delay_buf_l[state->delay_pos] = l;
            state->delay_buf_r[state->delay_pos] = r;
            if (++state->delay_pos >= 28) state->delay_pos = 0;
            dry_l = delayed_l;
            dry_r = delayed_r;
        }

        /* 4. Reverb sends: mix dry and patina sends */
        int16_t send_l = sat_s16((int32_t)q15_mul_truncate(dry_l, state->dry_send)
                               + (int32_t)q15_mul_truncate(patina_l, state->patina_send));
        int16_t send_r = sat_s16((int32_t)q15_mul_truncate(dry_r, state->dry_send)
                               + (int32_t)q15_mul_truncate(patina_r, state->patina_send));

        /* 5. Reverb: unchanged chain_step internals */
        int16_t rev_l = 0, rev_r = 0;
        spu94_fir_chain_step(state, send_l, send_r, &rev_l, &rev_r);

        /* 6. Master mixer: three-bus sum */
        int16_t out_l = sat_s16(
            (int32_t)q15_mul_truncate(dry_l, state->dry_fader)
          + (int32_t)q15_mul_truncate(patina_l, state->patina_fader)
          + (int32_t)q15_mul_truncate(rev_l, state->reverb_fader));
        int16_t out_r = sat_s16(
            (int32_t)q15_mul_truncate(dry_r, state->dry_fader)
          + (int32_t)q15_mul_truncate(patina_r, state->patina_fader)
          + (int32_t)q15_mul_truncate(rev_r, state->reverb_fader));

        /* 7. DAC section */
        if (state->dac_enabled) {
            if (state->dac_fir_enabled) {
                out_l = spu94_dac_fir_step(&state->dac_fir_l, out_l);
                out_r = spu94_dac_fir_step(&state->dac_fir_r, out_r);
            }
            if (state->dac_noise_enabled) {
                out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
                out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
            }
        }

        if (L_out != NULL) L_out[i] = out_l;
        if (R_out != NULL) R_out[i] = out_r;
    }
}
```

### JUCE Wet/Dry Deletion
```cpp
// In PluginProcessor.cpp processBlock, REPLACE lines 158-178:
// DELETE: the equal-power crossfade block
// REPLACE WITH: straight passthrough of SPU output
auto* outL = buffer.getWritePointer(0);
auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;
for (int i = 0; i < samplesToProcess; ++i) {
    outL[i] = tmpL_out[i] / 32768.0f;
    if (outR) outR[i] = tmpR_out[i] / 32768.0f;
}
```
[VERIFIED: PluginProcessor.cpp lines 158-178 contain the wet/dry crossfade to delete]

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| JUCE host owns wet/dry crossfade | C core owns all mixing via send/return architecture | Phase 7 | Every consumer (CLI, Python, JUCE) gets identical mixing behavior |
| Single-path: ADPCM -> reverb -> output | Three-bus mixer: dry + patina + reverb, independently faded | Phase 7 | Users can blend dry/colored/reverb independently |
| DAC FIR and noise modules standalone | DAC section integrated as post-mixer coloration | Phase 7 | Complete signal chain in C core |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The JUCE standalone is the only host with wet/dry crossfade code to delete | Architecture Patterns | CLI and Python may also have mixing logic; need to grep verify |
| A2 | latency_comp defaults to ON (1) even though zero-init gives 0 | Code Examples | Need explicit init in spu94_init to set latency_comp=1 after zero-fill, OR document that hosts must enable it |
| A3 | Input gain of 0x0000 (zero-init default) producing silence is acceptable behavior | Pitfalls | Could frustrate users expecting audio after init; but matches mixer console metaphor |
| A4 | WR-01 (noise quantization coarseness from Phase 6 review) is deferred | Pitfalls | If the 4-value quantization produces audible tonal artifacts, it needs fixing during integration rather than later |

## Open Questions

1. **Default fader values: zero-init silence vs. preset defaults?**
   - What we know: D-05 says Q15 int16 format. Zero-init gives all-silent mixer. A real mixing console starts with faders down.
   - What's unclear: Whether hosts are expected to always set faders explicitly (mixer console model) or whether spu94_init should set sensible defaults (convenience model).
   - Recommendation: Zero-init is correct (mixer console model). Document clearly that hosts must set faders. Optionally provide a helper `spu94_set_mixer_defaults()` that sets input_gain, dry_fader, and reverb_fader to 0x7FFF.

2. **latency_comp default: D-07 says "default ON" but zero-init gives 0 (OFF)**
   - What we know: D-07 explicitly says "Default: compensation ON."
   - What's unclear: How to reconcile with zero-init convention.
   - Recommendation: Set `latency_comp = 1` explicitly in `spu94_init` after the zero-fill, same place where DAC noise seeds are planted. This breaks the "zero-init = correct" convention but D-07 is a locked decision.

3. **Phase 6 CR-01 (int32 overflow): fix during integration or defer?**
   - What we know: The review found potential UB in dac_fir accumulator multiplication. The current coefficients don't trigger it, but the type signature allows it.
   - What's unclear: Whether this should be fixed now (touching Phase 6 code during Phase 7) or tracked separately.
   - Recommendation: Fix during integration. It's a one-line change (int32 -> int64 multiply) and prevents UB. Natural time to address since we're touching the init/reset paths anyway.

4. **Phase 6 WR-02 (deterministic noise seed): fix during integration?**
   - What we know: Both L and R channels use the same LFSR seed 0xACE1, producing identical (mono-correlated) noise.
   - What's unclear: Whether to add a seed parameter to the init function (API change) or use a different fixed seed per channel.
   - Recommendation: Add seed parameter to `spu94_dac_noise_init(state, seed)` and use different seeds per channel (e.g., 0xACE1 for L, 0x1ECA for R). This is an API change to the Phase 6 header but it's internal (no external consumers yet).

5. **Should the JUCE inputLevel (input gain) be removed or repurposed?**
   - What we know: JUCE currently has `inputLevel` as a host-side float that scales input before SPU. The C core now has `input_gain` as Q15.
   - What's unclear: Whether the JUCE inputLevel maps directly to the new C core input_gain, or whether both should exist.
   - Recommendation: Map the JUCE inputLevel directly to `spu94_set_input_gain()`. The host converts float [0.0, 1.0] to Q15 [0x0000, 0x7FFF] at the API boundary per D-05. This is a Phase 8 (I/O Surface) concern, but the JUCE deletion in Phase 7 should leave a clean passthrough with the input gain conversion wired up.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C test framework) + ctest |
| Config file | tests/unit/CMakeLists.txt |
| Quick run command | `ctest --test-dir build -R "mixer\|dac_int" -j$(nproc)` |
| Full suite command | `ctest --test-dir build -j$(nproc)` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DAC-INT-01 | DAC toggle API, default-off, state reset on disable | unit | `ctest --test-dir build -R dac_integration` | No -- Wave 0 |
| DAC-INT-02 | DAC state in spu94_state budget, zero regression with DAC off | integration | `ctest --test-dir build -j$(nproc)` | Partial -- existing suite covers regression |
| DAC-INT-03 | rt_safety gates pass with DAC enabled | regression | `ctest --test-dir build -L rt_safety` | Yes -- existing gates |
| (D-01) | Mixer architecture: three-bus routing correct | integration | `ctest --test-dir build -R mixer` | No -- Wave 0 |
| (D-07) | Latency compensation delay aligns buses | unit | `ctest --test-dir build -R latency_comp` | No -- Wave 0 |
| (D-09) | DAC master/sub toggle hierarchy | unit | `ctest --test-dir build -R dac_toggle` | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -R "mixer\|dac\|rt_" -j$(nproc)`
- **Per wave merge:** `ctest --test-dir build -j$(nproc)`
- **Phase gate:** Full suite green before verification

### Wave 0 Gaps
- [ ] `tests/unit/process/test_process_mixer.c` -- covers mixer bus routing, fader math, three-bus sum
- [ ] `tests/unit/process/test_process_dac_integration.c` -- covers DAC toggle hierarchy, FIR+noise integration
- [ ] `tests/unit/process/test_process_latency_comp.c` -- covers latency compensation delay buffer
- [ ] Add targets to `tests/unit/process/CMakeLists.txt`

## Sources

### Primary (HIGH confidence)
- Codebase inspection: `src/spu94/spu94_process.c` -- current signal flow, ADPCM integration pattern
- Codebase inspection: `src/spu94/spu94_io_chain.c` -- chain_step_impl, toggle pattern (lines 148-178)
- Codebase inspection: `src/spu94/spu94_state_internal.h` -- struct layout, 792 bytes current size
- Codebase inspection: `src/spu94/spu94_state.c` -- init/reset zero-fill pattern
- Codebase inspection: `include/spu94/spu94.h` -- public API surface, SPU94_STATE_SIZE_MAX=16384
- Codebase inspection: `include/spu94/spu94_dac_fir.h` -- FIR module API, state struct layout
- Codebase inspection: `include/spu94/spu94_dac_noise.h` -- noise module API, LFSR seed warning
- Codebase inspection: `src/spu94/spu94_dac_noise.c` -- LFSR init, seed constant, zero-absorbing-state
- Codebase inspection: `src/standalone/PluginProcessor.cpp` -- wet/dry crossfade at lines 158-178
- Codebase inspection: `include/spu94/spu94_q15.h` -- q15_mul_truncate, sat_s16, q15_add_sat
- Runtime verification: `spu94_state_size()` returns 792 via ctypes on built library
- v1.1 Phase 2 precedent: `.planning/milestones/v1.1-phases/02-pipeline-integration/` -- RESEARCH, PLAN 01, PLAN 02

### Secondary (MEDIUM confidence)
- `.planning/phases/07-pipeline-integration/07-CONTEXT.md` -- locked decisions D-01 through D-12
- `.planning/phases/06-dac-core-implementation/06-REVIEW.md` -- CR-01, WR-01, WR-02 findings
- `.planning/REQUIREMENTS.md` -- DAC-INT-01, DAC-INT-02, DAC-INT-03 specifications

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all components already exist in the codebase, no external dependencies
- Architecture: HIGH - full signal flow traced through source; mixer architecture precisely defined by CONTEXT.md decisions; v1.1 Phase 2 provides direct precedent for toggle/state/API patterns
- Pitfalls: HIGH - derived from direct code inspection; DAC noise zero-init pitfall confirmed by reading spu94_dac_noise.h warning comment and spu94_state.c init pattern

**Research date:** 2026-04-29
**Valid until:** N/A (codebase-specific research; valid until these source files change)
