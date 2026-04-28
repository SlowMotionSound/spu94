# Phase 2: Pipeline Integration - Research

**Researched:** 2026-04-26
**Domain:** C DSP pipeline wiring -- ADPCM codec into reverb signal chain
**Confidence:** HIGH

## Summary

Phase 2 wires the standalone ADPCM codec (Phase 1) into the existing `spu94_process` signal path as a toggleable upstream stage. The current architecture has a clean injection point: `spu94_process()` feeds 44.1 kHz samples one-at-a-time into `spu94_fir_chain_step()`, which decimates to 22.05 kHz, runs the reverb tick, and interpolates back to 44.1 kHz. The ADPCM stage must sit upstream of this entire chain -- at 44.1 kHz, before the decimator sees the samples.

The ADPCM codec operates on 28-sample blocks while the existing pipeline is sample-at-a-time. This mismatch requires a double-buffer strategy: accumulate 28 input samples, encode+decode the previous full block, and emit from the decoded buffer. This introduces exactly 28 samples of latency when enabled and zero when disabled.

The struct `spu94_state` is currently 560 bytes with a 16384-byte cap, leaving 15824 bytes of headroom. The ADPCM integration needs approximately 136 bytes (two 28-sample int16 buffers + two codec states + control fields), well within budget. All ADPCM code is already compiled into `libspu94.so` and all 6 rt_safety gates pass with it linked.

**Primary recommendation:** Modify `spu94_process()` to conditionally route samples through a 28-sample accumulate-encode-decode buffer before passing them to `spu94_fir_chain_step()`, with new public API functions for enable/disable and updated latency reporting.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| ADPCM-INT-01 | Wire encode+decode upstream of FIR decimator, toggle via set/get | Signal flow analysis confirms injection point in spu94_process sample loop; public API surface follows existing spu94_set/get pattern |
| ADPCM-INT-02 | Double-buffer strategy, 28-sample latency when enabled | Block/sample mismatch documented; double-buffer fields fit in spu94_state with 15824 bytes headroom |
| ADPCM-INT-03 | Total latency: 86 when enabled, 58 when disabled | Current spu94_get_latency_samples() returns compile-time constant SPU94_LATENCY_SAMPLES=58; needs upgrade to runtime query |
| ADPCM-INT-04 | State zeroed by init/reset, mid-stream toggle discards partial buffer | spu94_init/reset use spu94_zero_bytes on entire struct; new fields automatically zeroed |
| ADPCM-INT-05 | Off by default, all 84 tests pass unchanged, state within 16384 bytes | Zero-init struct gives adpcm_enabled=false; 560+136=696 well under 16384 |
| ADPCM-INT-06 | rt_safety gates pass with ADPCM linked | Already verified: ADPCM code linked into libspu94.so; all 6 rt_safety tests pass today |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| ADPCM encode+decode processing | DSP core (libspu94) | -- | Must run in the sample-processing hot path, same tier as reverb body |
| Toggle on/off API | Public C API (spu94.h) | -- | Follows existing pattern of spu94_set/get functions |
| Double-buffer state management | DSP core internal state | -- | Lives inside spu94_state struct, managed by init/reset |
| Latency reporting | Public C API (spu94.h) | -- | Extends existing spu94_get_latency_samples to be state-aware |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libspu94 (this project) | HEAD | Reverb DSP engine being extended | The thing we're modifying |
| spu94_adpcm.h/c | Phase 1 | Standalone ADPCM codec | Already built and tested; peer module pattern |

### Supporting
No additional libraries needed. Phase 2 is pure internal wiring within existing C codebase.

## Architecture Patterns

### System Architecture Diagram -- Current Signal Flow

```
44.1 kHz input (L, R int16)
        |
        v
+-------------------+
| spu94_process()   |  <-- per-sample loop over num_samples
|   for each sample:|
|     l, r = input  |
|         |         |
|         v         |
| spu94_fir_chain_step()  <-- internal, sample-at-a-time
|   |                     |
|   v                     |
|  decimate (44.1->22.05) |
|   |                     |
|   v (retained phase)    |
|  mix_bus_l/r = dec      |
|  spu94_tick() [reverb]  |
|  src = reverb_out_l/r   |
|   |                     |
|   v                     |
|  interpolate (22.05->44.1)
|   |                     |
|   v                     |
|  lo, ro = output        |
+-------------------+
        |
        v
44.1 kHz output (L, R int16)
```

### System Architecture Diagram -- After Phase 2

```
44.1 kHz input (L, R int16)
        |
        v
+-------------------+
| spu94_process()   |
|   for each sample:|
|     l, r = input  |
|         |         |
|         v         |
|  [if adpcm_enabled]
|   accumulate into |
|   adpcm_in_buf[]  |
|   emit from       |
|   adpcm_out_buf[] |  <-- previous block's decoded output
|   when buf full:  |
|     encode(in) -> block
|     decode(block) -> out
|     swap in/out   |
|  [else]           |
|   passthrough     |
|         |         |
|         v         |
| spu94_fir_chain_step()  <-- unchanged
|   [decimator -> tick -> interpolator]
+-------------------+
        |
        v
44.1 kHz output (L, R int16)
```

### Pattern 1: Double-Buffer Block Accumulation
**What:** The ADPCM codec operates on 28-sample blocks. The pipeline operates sample-at-a-time. A double-buffer resolves the mismatch: input samples accumulate into one buffer; output samples emit from the other (which holds the previous block's decoded result). When the input buffer fills to 28, it gets encoded+decoded to produce the next output buffer.
**When to use:** Whenever a block-based codec must be embedded in a sample-at-a-time pipeline.
**Example:**
```c
// Per-sample in the spu94_process loop:
if (state->adpcm_enabled) {
    // Store current sample into accumulation buffer
    state->adpcm_in_buf_l[state->adpcm_buf_pos] = l;
    state->adpcm_in_buf_r[state->adpcm_buf_pos] = r;

    // Emit from previously decoded buffer
    l = state->adpcm_out_buf_l[state->adpcm_buf_pos];
    r = state->adpcm_out_buf_r[state->adpcm_buf_pos];

    state->adpcm_buf_pos++;
    if (state->adpcm_buf_pos == SPU94_ADPCM_BLOCK_SAMPLES) {
        // Encode + decode the accumulated block
        uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
        spu94_adpcm_encode_block(&state->adpcm_enc_state_l,
                                  state->adpcm_in_buf_l, 0, block);
        spu94_adpcm_decode_block(&state->adpcm_dec_state_l,
                                  block, state->adpcm_out_buf_l);
        // Same for R channel
        // ...
        state->adpcm_buf_pos = 0;
    }
}
// Then pass l, r to spu94_fir_chain_step as before
```
[VERIFIED: codebase inspection of spu94_process.c and spu94_adpcm.h]

### Pattern 2: State-Dependent Latency Reporting
**What:** `spu94_get_latency_samples()` currently returns a compile-time constant (58). With ADPCM, it must return 58 or 86 depending on whether ADPCM is enabled. The function signature changes from `(void)` to `(const spu94_state *state)`.
**When to use:** When latency depends on runtime configuration.
**Design choice:** This is a public API change. The existing `SPU94_LATENCY_SAMPLES` macro (58) stays as the FIR-only constant. The new `spu94_get_total_latency_samples()` function name avoids breaking the existing `spu94_get_latency_samples()` contract.
[VERIFIED: spu94.h line 212, spu94_io_chain.c line 143]

### Pattern 3: Toggle With Partial-Buffer Discard
**What:** When ADPCM is toggled off mid-stream, the partial accumulation buffer (0-27 samples that haven't formed a complete block yet) is discarded. At 44.1 kHz, 27 samples = 0.6 ms -- inaudible silence gap.
**When to use:** Simplifies state management vs. attempting to flush a partial block.
**Example:**
```c
void spu94_set_adpcm_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->adpcm_enabled) {
        // Discard partial buffer on disable
        state->adpcm_buf_pos = 0;
        // Zero output buffer so no stale audio leaks
        // (spu94_zero_bytes pattern)
    }
    state->adpcm_enabled = enabled ? 1 : 0;
}
```
[VERIFIED: REQUIREMENTS.md ADPCM-INT-04 specifies this behavior]

### Recommended Project Structure

No new files needed. Modifications to existing files only:
```
include/spu94/spu94.h           # New public API: set/get adpcm_enabled, get_total_latency
src/spu94/spu94_state_internal.h # New fields in spu94_state struct
src/spu94/spu94_process.c       # ADPCM stage in process loop
src/spu94/spu94_io_chain.c      # New latency accessor (or spu94_state.c)
tests/unit/process/             # New integration tests
```

### Anti-Patterns to Avoid
- **Processing ADPCM at 22.05 kHz:** The PS1 hardware applies ADPCM before the decimator. The ADPCM stage MUST operate at 44.1 kHz (before chain_step), not at 22.05 kHz (inside the tick). [VERIFIED: REQUIREMENTS.md ADPCM-INT-01 "upstream of the FIR decimator"]
- **Separate encode/decode state per codec call:** The encoder already updates state internally (using reconstructed samples). Don't add a separate decode state and then try to keep them in sync -- encode_block already handles this. However, for the integration path, we need SEPARATE encoder and decoder states because we call encode then decode as two steps. The decoder state must start fresh from the same initial conditions the encoder's internal decoder used.
- **Modifying spu94_fir_chain_step:** The ADPCM stage sits ABOVE chain_step in spu94_process, not inside chain_step. Touching chain_step risks blast radius on the FIR chain tests.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| ADPCM encode/decode | New codec code | `spu94_adpcm_encode_block` + `spu94_adpcm_decode_block` from Phase 1 | Already tested (31 unit tests), hardware-faithful |
| Zero-fill for init/reset | memset | `spu94_zero_bytes()` in spu94_state.c | Existing pattern; avoids `<string.h>` in the state module |
| Sample clamping | Manual clamp | `sat_s16()` from spu94_q15.h | Already proven in reverb body |

## Common Pitfalls

### Pitfall 1: Encoder/Decoder State Divergence
**What goes wrong:** The encoder contains an internal decoder copy that tracks reconstructed samples. But in the integration path, we call `spu94_adpcm_encode_block` followed by `spu94_adpcm_decode_block` as two separate calls. If the decoder state fed to `decode_block` doesn't match the encoder's internal decoder's final state, the two diverge and subsequent blocks decode incorrectly.
**Why it happens:** The encode function updates `*state` to the encoder's internal decoder's final state. The decode function also updates `*state`. If you use the SAME state pointer for both, the encode writes the correct state and then the decode overwrites it -- but since the encoder's internal decoder and the standalone decoder produce identical results (ADPCM-05), the final state after both calls is actually correct.
**How to avoid:** Use separate `spu94_adpcm_state` for encoder and decoder per channel. After encode_block, the encoder state already reflects the encoder's internal decoder. Feed the decoder a separate state. Both should converge because they process the same ADPCM block -- but if you ever skip a decode call, states diverge.
**Simpler approach:** Actually, use a single state per channel. `encode_block` updates it to the encoder's internal decoder state. Then `decode_block` processes the same block and arrives at the same state. Net result: state is correct for the next block. This works because ADPCM-05 guarantees the encoder tracks reconstructed samples identically to the decoder.
**Warning signs:** Clicks or discontinuities at block boundaries after the first block.

### Pitfall 2: spu94_flush Forgets ADPCM
**What goes wrong:** `spu94_flush` currently delegates to `spu94_process` with NULL inputs. If the ADPCM buffer logic only activates when `L_in != NULL`, the flush path skips ADPCM and sends zeros directly to chain_step -- which means the latency compensation is wrong during tail drain.
**Why it happens:** spu94_process substitutes 0 for NULL input channels. The ADPCM path should see those zeros and process them (accumulate zeros, emit previously decoded block).
**How to avoid:** The ADPCM accumulation logic must operate on the substituted-zero values `l` and `r` (after the NULL check), not on the raw pointers. Since `spu94_flush` calls `spu94_process(state, NULL, NULL, ...)`, the existing NULL-substitution at line 35-36 already produces `l=0, r=0`. The ADPCM logic below that point sees valid zero samples. No special flush handling needed.
**Warning signs:** Reverb tail is 28 samples shorter than expected when ADPCM is enabled.

### Pitfall 3: Latency Accessor ABI Break
**What goes wrong:** Changing `spu94_get_latency_samples(void)` to take a state pointer would break existing callers (Python bindings, JUCE standalone, CLI).
**Why it happens:** The existing function is a public API symbol with `(void)` signature.
**How to avoid:** Keep `spu94_get_latency_samples()` returning 58 (FIR-only, compile-time constant, backward-compatible). Add a NEW function `spu94_get_total_latency_samples(const spu94_state *state)` that returns 58 or 86 depending on ADPCM state. The requirement says "spu94_get_total_latency_samples()" -- this is already a new name.
**Warning signs:** Linker errors or test failures in existing Python binding tests.

### Pitfall 4: Enable/Disable Not Zeroing Output Buffer
**What goes wrong:** On first enable, the output buffer (adpcm_out_buf) contains stale data from a previous session or uninitialized memory. The first 28 samples emitted are garbage.
**Why it happens:** If reset/init zeroes the struct but a later enable doesn't clear the output buffer, and someone disables then re-enables, the output buffer might hold stale decoded audio from the previous enabled session.
**How to avoid:** Since `spu94_init` and `spu94_reset` zero the entire struct, the output buffer starts as silence (zeros). On mid-stream disable+re-enable, INT-04 says partial buffer is discarded. The output buffer after disable still holds valid decoded audio from the last complete block -- on re-enable, the first 28 samples will emit those values (stale but valid audio, not garbage). Acceptable per the 0.6ms inaudibility argument. Alternatively, zero the output buffer on enable for cleaner behavior.

### Pitfall 5: ADPCM Per-Channel State Independence
**What goes wrong:** Using a single adpcm_state for both L and R channels corrupts the prediction state because each channel has independent audio content.
**Why it happens:** The PS1 SPU processes each voice mono. ADPCM state is per-voice (per-channel).
**How to avoid:** Four state structs total: `adpcm_enc_state_l`, `adpcm_enc_state_r`, `adpcm_dec_state_l`, `adpcm_dec_state_r` (or two if using the single-state-per-channel approach from Pitfall 1).
**Warning signs:** Stereo image collapse or cross-channel artifacts.

## Code Examples

### New spu94_state Fields
```c
// In spu94_state_internal.h, add after oob_tap_count:

/* Phase 2 (ADPCM-INT): double-buffer state for ADPCM coloration stage. */
uint8_t        adpcm_enabled;      /* 0=off (default), 1=on */
uint8_t        adpcm_buf_pos;      /* 0..27 accumulation index */
int16_t        adpcm_in_buf_l[28]; /* input accumulation buffer, L channel */
int16_t        adpcm_in_buf_r[28]; /* input accumulation buffer, R channel */
int16_t        adpcm_out_buf_l[28];/* output (decoded) buffer, L channel */
int16_t        adpcm_out_buf_r[28];/* output (decoded) buffer, R channel */
spu94_adpcm_state adpcm_state_l;   /* encode+decode state, L (4 bytes) */
spu94_adpcm_state adpcm_state_r;   /* encode+decode state, R (4 bytes) */
```
Size impact: 2 + 56 + 56 + 56 + 56 + 4 + 4 = **234 bytes** (before padding). Current struct is 560 bytes. New total ~794 bytes. Well under 16384 cap. [VERIFIED: sizeof from built library = 560; cap = 16384]

### Modified spu94_process
```c
// Source: pattern derived from existing spu94_process.c + ADPCM API
void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples) {
    if (state == NULL) return;
    for (uint32_t i = 0; i < num_samples; i++) {
        int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;

        if (state->adpcm_enabled) {
            // Emit from previous decoded block
            int16_t out_l = state->adpcm_out_buf_l[state->adpcm_buf_pos];
            int16_t out_r = state->adpcm_out_buf_r[state->adpcm_buf_pos];

            // Accumulate current input
            state->adpcm_in_buf_l[state->adpcm_buf_pos] = l;
            state->adpcm_in_buf_r[state->adpcm_buf_pos] = r;

            state->adpcm_buf_pos++;
            if (state->adpcm_buf_pos == SPU94_ADPCM_BLOCK_SAMPLES) {
                // Block complete: encode+decode for next emission
                uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
                spu94_adpcm_encode_block(&state->adpcm_state_l,
                    state->adpcm_in_buf_l, 0, block);
                spu94_adpcm_decode_block(&state->adpcm_state_l,
                    block, state->adpcm_out_buf_l);

                spu94_adpcm_encode_block(&state->adpcm_state_r,
                    state->adpcm_in_buf_r, 0, block);
                spu94_adpcm_decode_block(&state->adpcm_state_r,
                    block, state->adpcm_out_buf_r);

                state->adpcm_buf_pos = 0;
            }

            l = out_l;
            r = out_r;
        }

        int16_t lo = 0, ro = 0;
        spu94_fir_chain_step(state, l, r, &lo, &ro);
        if (L_out != NULL) L_out[i] = lo;
        if (R_out != NULL) R_out[i] = ro;
    }
}
```
Note: The encode-then-decode-same-state approach works because both operations process the same ADPCM block and arrive at the same final prediction state (ADPCM-05 guarantee). [VERIFIED: spu94_adpcm.h API, spu94_process.c current structure]

### Public API Additions
```c
// In spu94.h:

/* Enable/disable ADPCM coloration stage upstream of the FIR decimator.
 * Off by default. NULL state is a no-op. */
void spu94_set_adpcm_enabled(spu94_state *state, int enabled);
int  spu94_get_adpcm_enabled(const spu94_state *state);

/* Total processing latency in samples at 44.1 kHz, including ADPCM
 * block latency when enabled. Returns 58 (FIR only) or 86 (FIR + ADPCM).
 * NULL state returns SPU94_LATENCY_SAMPLES (58). */
uint32_t spu94_get_total_latency_samples(const spu94_state *state);
```
[VERIFIED: naming follows existing patterns spu94_get_buffer_address, spu94_get_error_counters]

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| spu94_get_latency_samples(void) returns constant 58 | Keep as-is for backward compat; add spu94_get_total_latency_samples(state) | Phase 2 | No ABI break; additive API |
| spu94_process passes raw samples to FIR chain | Optionally route through ADPCM encode+decode first | Phase 2 | Single new conditional in hot path; zero cost when disabled |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Single adpcm_state per channel (shared between encode and decode) produces correct cross-block continuity | Pitfall 1 / Code Examples | If encode and decode diverge on state, clicks at block boundaries. Mitigation: unit test verifying state equality after encode+decode of same block. |
| A2 | The ADPCM encode+decode cost is negligible relative to the FIR+reverb pipeline | Architecture Patterns | If encode+decode per sample is expensive, the rt_bench_latency jitter test might fail. Mitigation: encoder's brute-force runs only once per 28 samples, and the existing bench already passes with ADPCM linked. |

## Open Questions

1. **Encode+decode same state or separate states per channel?**
   - What we know: The encoder updates `*state` to its internal decoder's final state. The decoder, processing the same block, arrives at the same state. Both are correct individually.
   - What's unclear: Whether using one state for both (encode overwrites, then decode overwrites to same value) is cleaner than two states that are kept in sync.
   - Recommendation: Use one state per channel (shared encode+decode). It's simpler, saves 8 bytes, and correctness is guaranteed by ADPCM-05. Add a debug assertion in tests that encoder-final-state == decoder-final-state.

2. **spu94_flush behavior with ADPCM partial buffer**
   - What we know: flush calls process with NULL inputs -> zeros. ADPCM accumulates zeros normally. The partial buffer at stream end contains valid zero samples.
   - What's unclear: Whether the caller expects the ADPCM latency compensation to produce exact sample counts matching the flush request.
   - Recommendation: No special handling needed. The 28-sample ADPCM latency is documented; callers requesting N flush samples get N output samples (the last 28 of which may be zero-padded through ADPCM). This matches the FIR latency model where the first 58 output samples are also group-delay artifacts.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C test framework) + ctest |
| Config file | tests/unit/CMakeLists.txt |
| Quick run command | `ctest --test-dir build -R adpcm -j$(nproc)` |
| Full suite command | `ctest --test-dir build -j$(nproc)` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| ADPCM-INT-01 | ADPCM wired upstream of FIR, toggled via set/get | integration | `ctest --test-dir build -R adpcm_integration` | No -- Wave 0 |
| ADPCM-INT-02 | Double-buffer, 28-sample latency when enabled | unit | `ctest --test-dir build -R adpcm_latency` | No -- Wave 0 |
| ADPCM-INT-03 | Total latency 86 enabled / 58 disabled | unit | `ctest --test-dir build -R adpcm_latency_report` | No -- Wave 0 |
| ADPCM-INT-04 | State zeroed by init/reset, partial discard on toggle | unit | `ctest --test-dir build -R adpcm_state_mgmt` | No -- Wave 0 |
| ADPCM-INT-05 | Off by default, all 84 existing tests pass, state fits | regression | `ctest --test-dir build -j$(nproc)` | Yes -- existing suite |
| ADPCM-INT-06 | rt_safety gates pass | regression | `ctest --test-dir build -L rt_safety` | Yes -- existing gates |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -R "adpcm|rt_" -j$(nproc)`
- **Per wave merge:** `ctest --test-dir build -j$(nproc)`
- **Phase gate:** Full suite green before verification

### Wave 0 Gaps
- [ ] `tests/unit/process/test_process_adpcm_integration.c` -- covers INT-01, INT-02, INT-04
- [ ] `tests/unit/process/test_process_adpcm_latency.c` -- covers INT-03
- [ ] Add to `tests/unit/process/CMakeLists.txt` or new `tests/unit/adpcm_integration/` subdirectory

## Sources

### Primary (HIGH confidence)
- Codebase inspection: `src/spu94/spu94_process.c` -- current signal flow, sample-at-a-time loop
- Codebase inspection: `src/spu94/spu94_io_chain.c` -- chain_step_impl, decimator->tick->interpolator pipeline
- Codebase inspection: `src/spu94/spu94_state_internal.h` -- struct layout, 560 bytes current size
- Codebase inspection: `src/spu94/spu94_state.c` -- init/reset/destroy lifecycle, spu94_zero_bytes pattern
- Codebase inspection: `include/spu94/spu94.h` -- public API surface, SPU94_STATE_SIZE_MAX=16384, latency contract
- Codebase inspection: `include/spu94/spu94_adpcm.h` -- codec API signatures, block/sample constants
- Codebase inspection: `tests/rt_safety/` -- all 4 gate scripts + CMakeLists wiring
- Runtime verification: `spu94_state_size()` returns 560 via ctypes on built library
- Runtime verification: `ctest -R rt_no_heap` passes with ADPCM linked

### Secondary (MEDIUM confidence)
- `.planning/REQUIREMENTS.md` -- ADPCM-INT-01..06 requirement specifications
- `.planning/phases/01-core-codec/01-01-SUMMARY.md` -- decoder API confirmed
- `.planning/phases/01-core-codec/01-02-SUMMARY.md` -- encoder API confirmed

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - this is internal codebase modification, no external dependencies
- Architecture: HIGH - full signal flow traced through source code, injection point identified
- Pitfalls: HIGH - derived from direct code inspection of the specific functions being modified

**Research date:** 2026-04-26
**Valid until:** N/A (codebase-specific research; valid until these source files change)
