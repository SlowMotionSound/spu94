# Phase 7: Pipeline Integration - Pattern Map

**Mapped:** 2026-04-29
**Files analyzed:** 8 (6 modified + 2 new test files + CMakeLists.txt entry)
**Analogs found:** 8 / 8

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/spu94/spu94_process.c` | DSP core | transform (sample-loop rewrite) | itself (current version) | self |
| `src/spu94/spu94_state_internal.h` | model/state | N/A | itself + ADPCM block pattern | self |
| `src/spu94/spu94_state.c` | lifecycle | N/A | itself (init/reset fixup) | self |
| `src/spu94/spu94_io_chain.c` | service/toggle | request-response | `src/spu94/spu94_io_chain.c` lines 148-178 | exact (self) |
| `include/spu94/spu94.h` | public API | N/A | ADPCM API block lines 214-233 | exact (same file) |
| `src/standalone/PluginProcessor.cpp` | host wrapper | request-response | itself lines 158-179 (deletion target) | self |
| `tests/unit/process/test_process_mixer.c` | test | N/A | `tests/unit/process/test_process_adpcm.c` | exact |
| `tests/unit/process/test_process_dac_integration.c` | test | N/A | `tests/unit/process/test_process_adpcm.c` | exact |
| `tests/unit/process/test_process_latency_comp.c` | test | N/A | `tests/unit/process/test_process_adpcm.c` | exact |
| `tests/unit/process/CMakeLists.txt` | config | N/A | itself lines 106-112 | exact |

---

## Pattern Assignments

### `src/spu94/spu94_process.c` (DSP core, sample-loop rewrite)

**Analog:** itself — preserve the outer shell, replace the body

**Imports/header block** (lines 1-28 of current file):
```c
/* src/spu94/spu94_process.c -- [updated phase attribution] */
#include <spu94/spu94.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_dac_fir.h>    /* ADD: Phase 7 */
#include <spu94/spu94_dac_noise.h>  /* ADD: Phase 7 */
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"
#include <stdint.h>
#include <stddef.h>
```

**Function signature and NULL guard** (lines 30-35, keep exactly):
```c
void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples) {
    if (state == NULL) return;
    for (uint32_t i = 0; i < num_samples; i++) {
        int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;
```

**ADPCM double-buffer block** (lines 43-72, keep verbatim — ADPCM stage is separable per D-03):
```c
        /* ADPCM coloration stage (ADPCM-INT-01, INT-02).
         * Double-buffer: emit from previous decoded block while
         * accumulating current input. When buffer fills to 28,
         * encode+decode produces the next output block. */
        if (state->adpcm_enabled) {
            int16_t out_l = state->adpcm_out_buf_l[state->adpcm_buf_pos];
            int16_t out_r = state->adpcm_out_buf_r[state->adpcm_buf_pos];
            state->adpcm_in_buf_l[state->adpcm_buf_pos] = l;
            state->adpcm_in_buf_r[state->adpcm_buf_pos] = r;
            state->adpcm_buf_pos++;
            if (state->adpcm_buf_pos == SPU94_ADPCM_BLOCK_SAMPLES) {
                uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
                spu94_adpcm_encode_block(&state->adpcm_state_l, state->adpcm_in_buf_l, 0, block);
                spu94_adpcm_decode_block(&state->adpcm_state_l, block, state->adpcm_out_buf_l);
                spu94_adpcm_encode_block(&state->adpcm_state_r, state->adpcm_in_buf_r, 0, block);
                spu94_adpcm_decode_block(&state->adpcm_state_r, block, state->adpcm_out_buf_r);
                state->adpcm_buf_pos = 0;
            }
            /* patina bus = ADPCM-colored signal */
            patina_l = out_l;
            patina_r = out_r;
        } else {
            patina_l = l;
            patina_r = r;
        }
```

**New stages — use this skeleton** (replaces lines 74-82):
```c
        /* 1. Input gain (D-04, D-05) */
        l = q15_mul_truncate(l, state->input_gain);
        r = q15_mul_truncate(r, state->input_gain);

        /* 2. ADPCM stage -> patina bus (block above, placed here after gain) */
        int16_t patina_l, patina_r;
        /* ... ADPCM double-buffer block ... */

        /* 3. Dry bus with latency compensation (D-07, D-08) */
        int16_t dry_l = l, dry_r = r;
        if (state->latency_comp && state->adpcm_enabled) {
            int16_t delayed_l = state->delay_buf_l[state->delay_pos];
            int16_t delayed_r = state->delay_buf_r[state->delay_pos];
            state->delay_buf_l[state->delay_pos] = l;
            state->delay_buf_r[state->delay_pos] = r;
            if (++state->delay_pos >= 28) state->delay_pos = 0;
            dry_l = delayed_l;
            dry_r = delayed_r;
        }

        /* 4. Reverb sends: sum of dry and patina sends (D-01) */
        int16_t send_l = sat_s16((int32_t)q15_mul_truncate(dry_l,    state->dry_send)
                               + (int32_t)q15_mul_truncate(patina_l, state->patina_send));
        int16_t send_r = sat_s16((int32_t)q15_mul_truncate(dry_r,    state->dry_send)
                               + (int32_t)q15_mul_truncate(patina_r, state->patina_send));

        /* 5. Reverb: unchanged chain internals; only the input changes (D-01) */
        int16_t rev_l = 0, rev_r = 0;
        spu94_fir_chain_step(state, send_l, send_r, &rev_l, &rev_r);

        /* 6. Master mixer: three-bus sum, int32 accumulation + sat_s16 (D-01) */
        int16_t out_l = sat_s16(
            (int32_t)q15_mul_truncate(dry_l,    state->dry_fader)
          + (int32_t)q15_mul_truncate(patina_l, state->patina_fader)
          + (int32_t)q15_mul_truncate(rev_l,    state->reverb_fader));
        int16_t out_r = sat_s16(
            (int32_t)q15_mul_truncate(dry_r,    state->dry_fader)
          + (int32_t)q15_mul_truncate(patina_r, state->patina_fader)
          + (int32_t)q15_mul_truncate(rev_r,    state->reverb_fader));

        /* 7. DAC section (D-09 through D-12): master output only */
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

**spu94_flush** (lines 85-92, keep verbatim — D-02 delegation contract unchanged):
```c
void spu94_flush(spu94_state *state,
                 int16_t *L_out, int16_t *R_out,
                 uint32_t num_samples) {
    spu94_process(state, NULL, NULL, L_out, R_out, num_samples);
}
```

**Key constraints:**
- `q15_mul_truncate` and `sat_s16` are from `spu94_q15.h` — already included via `spu94.h`
- `q15_add_sat` is also from `spu94_q15.h`
- No `float` or `double` in this file (D-05)
- `spu94_fir_chain_step` internals are UNCHANGED — only what feeds it changes

---

### `src/spu94/spu94_state_internal.h` (model/state, struct expansion)

**Analog:** the ADPCM block at lines 139-156 (embedding pattern, comment style, field grouping)

**ADPCM embedding pattern to copy** (lines 139-156):
```c
    /* -----------------------------------------------------------------
     * Phase 2 (ADPCM-INT): double-buffer state for ADPCM coloration
     * stage. ...
     * ----------------------------------------------------------------- */
    uint8_t            adpcm_enabled;       /* 0=off (default), 1=on */
    uint8_t            adpcm_buf_pos;       /* 0..27 accumulation index */
    int16_t            adpcm_in_buf_l[28];  /* input accumulation, L */
    int16_t            adpcm_in_buf_r[28];  /* input accumulation, R */
    int16_t            adpcm_out_buf_l[28]; /* decoded output, L */
    int16_t            adpcm_out_buf_r[28]; /* decoded output, R */
    spu94_adpcm_state  adpcm_state_l;       /* encode+decode state, L (4 bytes) */
    spu94_adpcm_state  adpcm_state_r;       /* encode+decode state, R (4 bytes) */
```

**New block to add** (append before the `oob_tap_count` tail block, following same comment and grouping style):
```c
    /* -----------------------------------------------------------------
     * Phase 7 (DAC-INT / Mixer): send/return mixer state.
     * Six Q15 faders/sends, latency compensation delay buffer,
     * DAC section toggles and module state.
     * New includes needed at top of this header:
     *   #include <spu94/spu94_dac_fir.h>
     *   #include <spu94/spu94_dac_noise.h>
     * ----------------------------------------------------------------- */

    /* Mixer controls -- Q15 int16, range [0x0000, 0x7FFF] (D-05) */
    int16_t        input_gain;        /* applied before bus split */
    int16_t        dry_fader;         /* dry bus level at master mixer */
    int16_t        patina_fader;      /* patina (ADPCM) bus level at master mixer */
    int16_t        dry_send;          /* dry bus -> reverb send level */
    int16_t        patina_send;       /* patina bus -> reverb send level */
    int16_t        reverb_fader;      /* reverb return level at master mixer */

    /* Latency compensation (D-07, D-08) */
    uint8_t        latency_comp;      /* 1=on (D-07 default), 0=off */
    uint8_t        delay_pos;         /* ring buffer write position, 0..27 */
    int16_t        delay_buf_l[28];   /* 28-sample delay, L channel */
    int16_t        delay_buf_r[28];   /* 28-sample delay, R channel */

    /* DAC section (D-09 through D-12) */
    uint8_t        dac_enabled;       /* master toggle, 0=off (default) */
    uint8_t        dac_fir_enabled;   /* FIR sub-toggle, 0=off (default) */
    uint8_t        dac_noise_enabled; /* noise sub-toggle, 0=off (default) */
    spu94_dac_fir_state   dac_fir_l;  /* FIR state, L channel */
    spu94_dac_fir_state   dac_fir_r;  /* FIR state, R channel */
    spu94_dac_noise_state dac_noise_l;/* noise state, L channel */
    spu94_dac_noise_state dac_noise_r;/* noise state, R channel */
```

**Required new includes** at top of this header (after existing includes):
```c
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
```

**_Static_assert** (line 181, keep — it gates the budget automatically):
```c
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");
```
Expected size after addition: ~792 + ~443 = ~1235 bytes. Well under 16384 cap.

---

### `src/spu94/spu94_state.c` (lifecycle, init/reset fixup)

**Analog:** itself — surgical additions to `spu94_init` and `spu94_reset`

**spu94_zero_bytes helper** (lines 48-53, do not touch):
```c
static void spu94_zero_bytes(void *dst, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) {
        p[i] = 0u;
    }
}
```

**spu94_init — add after spu94_zero_bytes call** (after line 84):
```c
    spu94_zero_bytes(s, sizeof(*s));

    /* D-07: latency_comp defaults ON (zero-init gives 0; explicit set needed). */
    s->latency_comp = 1;

    /* DAC noise LFSR cannot be zero-initialized: lfsr=0 is an absorbing
     * state (silence forever). Must plant non-zero seed after zero-fill.
     * See spu94_dac_noise.h warning. Per WR-02 fix: different seeds per
     * channel to decorrelate L/R noise. */
    spu94_dac_noise_init(&s->dac_noise_l);  /* seed 0xACE1 in L */
    spu94_dac_noise_init(&s->dac_noise_r);  /* seed 0x1ECA in R (WR-02) */

    s->work_buf       = (unsigned char *)work_buf;
    /* ... rest of init unchanged ... */
```

**spu94_reset — add after spu94_zero_bytes call** (after line 117):
```c
    spu94_zero_bytes(state, sizeof(*state));

    /* Same post-zero fixups as spu94_init (DAC noise seed, latency_comp). */
    state->latency_comp = 1;
    spu94_dac_noise_init(&state->dac_noise_l);
    spu94_dac_noise_init(&state->dac_noise_r);

    state->work_buf       = saved_work;
    /* ... rest of reset unchanged ... */
```

**Required new include** at top of `spu94_state.c`:
```c
#include <spu94/spu94_dac_noise.h>
```

---

### `src/spu94/spu94_io_chain.c` (service/toggle, new toggle implementations)

**Analog:** `spu94_set_adpcm_enabled` / `spu94_get_adpcm_enabled` (lines 148-171) — the direct template for every new toggle

**Exact template to copy** (lines 148-171):
```c
/* ADPCM-INT-01: toggle coloration stage. */
void spu94_set_adpcm_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->adpcm_enabled) {
        /* ADPCM-INT-04: discard partial accumulation buffer on disable.
         * Zero output buffer so no stale audio leaks on re-enable. */
        state->adpcm_buf_pos = 0;
        for (int j = 0; j < SPU94_ADPCM_BLOCK_SAMPLES; j++) {
            state->adpcm_out_buf_l[j] = 0;
            state->adpcm_out_buf_r[j] = 0;
        }
        /* Reset codec state so re-enable starts clean */
        state->adpcm_state_l.old = 0;
        state->adpcm_state_l.older = 0;
        state->adpcm_state_r.old = 0;
        state->adpcm_state_r.older = 0;
    }
    state->adpcm_enabled = enabled ? 1 : 0;
}

int spu94_get_adpcm_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->adpcm_enabled;
}
```

**Apply this pattern to all new toggles.** Example adaptation for DAC master toggle:
```c
void spu94_set_dac_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->dac_enabled) {
        /* Reset both FIR channels to silence on disable */
        spu94_dac_fir_init(&state->dac_fir_l);
        spu94_dac_fir_init(&state->dac_fir_r);
        /* Reset noise state — must use init, not zero-fill (LFSR absorbing-state) */
        spu94_dac_noise_init(&state->dac_noise_l);
        spu94_dac_noise_init(&state->dac_noise_r);
    }
    state->dac_enabled = enabled ? 1 : 0;
}

int spu94_get_dac_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_enabled;
}
```

**Apply the same pattern for:** `spu94_set/get_dac_fir_enabled`, `spu94_set/get_dac_noise_enabled`, `spu94_set/get_latency_comp`.

**Fader setters — simpler pattern** (no state reset needed, raw register write per D-06):
```c
void spu94_set_dry_fader(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->dry_fader = level;
}

int16_t spu94_get_dry_fader(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dry_fader;
}
```
Apply same pattern for: `input_gain`, `patina_fader`, `dry_send`, `patina_send`, `reverb_fader`.

**Required new includes** at top of `spu94_io_chain.c`:
```c
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
```

---

### `include/spu94/spu94.h` (public API additions)

**Analog:** the ADPCM API block (lines 214-233) — exact comment style, grouping, and naming convention

**ADPCM block to model** (lines 214-233):
```c
/* -----------------------------------------------------------------------
 * ADPCM coloration stage (M2 Phase 2, ADPCM-INT-01..06)
 *
 * When enabled, spu94_process routes input PCM through ADPCM
 * encode+decode before the FIR decimator, introducing PS1-characteristic
 * quantization noise and filter ringing. Off by default. Adds 28 samples
 * of latency (one ADPCM block) when enabled.
 * ----------------------------------------------------------------------- */

/** Enable/disable ADPCM coloration upstream of the FIR decimator.
 *  Off by default. NULL state is a no-op. */
void     spu94_set_adpcm_enabled(spu94_state *state, int enabled);

/** Query ADPCM coloration state. NULL state returns 0. */
int      spu94_get_adpcm_enabled(const spu94_state *state);
```

**New API section to add** (copy this exact layout after the ADPCM block):
```c
/* -----------------------------------------------------------------------
 * Send/return mixer controls (Phase 7, D-01 through D-06)
 *
 * Six Q15 fader/send values control the mixer architecture:
 *   input_gain:    scales input before bus split
 *   dry_fader:     dry bus level at master mixer
 *   patina_fader:  patina (ADPCM) bus level at master mixer
 *   dry_send:      dry bus contribution to reverb input
 *   patina_send:   patina bus contribution to reverb input
 *   reverb_fader:  reverb return level at master mixer
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
 * ON by default (D-07). OFF creates intentional comb filtering (creative use).
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
 * All three on = faithful PS1 DAC behavior (D-11).
 * Default: all off.
 * ----------------------------------------------------------------------- */

void     spu94_set_dac_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_enabled(const spu94_state *state);

void     spu94_set_dac_fir_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_fir_enabled(const spu94_state *state);

void     spu94_set_dac_noise_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_noise_enabled(const spu94_state *state);
```

---

### `src/standalone/PluginProcessor.cpp` (host wrapper, wet/dry deletion)

**Analog:** itself — surgical deletion + replacement of lines 158-179

**Delete this block** (lines 158-179 — the equal-power crossfade):
```cpp
    // Equal-power crossfade: dry input vs SPU wet output (D-02, STANDALONE-06).
    const float wet = wetDry.load(std::memory_order_relaxed);
    const float wetGain = std::sqrt(wet);
    const float dryGain = std::sqrt(1.0f - wet);

    auto* outL = buffer.getWritePointer(0);
    auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < samplesToProcess; ++i)
    {
        const float dryL = tmpL_in[i] / 32768.0f;
        const float dryR = tmpR_in[i] / 32768.0f;
        const float spuL = tmpL_out[i] / 32768.0f;
        const float spuR = tmpR_out[i] / 32768.0f;

        outL[i] = dryL * dryGain + spuL * wetGain;
        if (outR) outR[i] = dryR * dryGain + spuR * wetGain;
    }
```

**Replace with straight passthrough** (the C core mixer now owns all mixing):
```cpp
    auto* outL = buffer.getWritePointer(0);
    auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < samplesToProcess; ++i)
    {
        outL[i] = tmpL_out[i] / 32768.0f;
        if (outR) outR[i] = tmpR_out[i] / 32768.0f;
    }
```

**prepareToPlay — add default fader initialization** after `spu94_load_preset` call (line 66), to avoid Pitfall 3 (silence after deletion):
```cpp
    spu94_load_preset(spu, SPU94_PRESET_HALL);

    // D-05: C core now owns mixing. Set sensible defaults so audio
    // is audible immediately (faders default to 0x0000 = silence).
    spu94_set_input_gain(spu,   0x7FFF);
    spu94_set_dry_fader(spu,    0x7FFF);
    spu94_set_reverb_fader(spu, 0x7FFF);
    // patina_fader and sends stay at 0 until ADPCM is enabled.
```

**The `inputLevel` float in `PluginProcessor`** (line 145-151): the existing host-side input gain loop that applies `inGain` before feeding `spu94_process` should be removed too, since `input_gain` in the C core replaces it. Wire the `inputLevel` atomic directly to `spu94_set_input_gain()` via Q15 conversion instead:
```cpp
    // Instead of applying inGain in the fill loop, push to C core:
    spu94_set_input_gain(spu, static_cast<int16_t>(
        inputLevel.load(std::memory_order_relaxed) * 0x7FFF));
```

---

### `tests/unit/process/test_process_mixer.c` (new test, integration)
### `tests/unit/process/test_process_dac_integration.c` (new test, integration)
### `tests/unit/process/test_process_latency_comp.c` (new test, integration)

**Analog:** `tests/unit/process/test_process_adpcm.c` (lines 1-434) — exact structure to copy

**File header pattern** (lines 1-20):
```c
/* tests/unit/process/test_process_TOPIC.c -- Phase 7 Plan XX
 *
 * Integration tests for [topic] in spu94_process.
 * Covers requirements [DAC-INT-01 etc.]:
 *
 *   ...
 *
 * Pattern: same setUp/tearDown as test_process_adpcm.c.
 * Includes spu94_state_internal.h for direct struct inspection.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include "spu94_state_internal.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
```

**Shared fixtures** (lines 27-43, copy exactly):
```c
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state = NULL;

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_b[SPU94_WORK_BUF_MAX_BYTES];

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
}

void tearDown(void) {
    state = NULL;
}
```

**Toggle test pattern** (lines 57-69, copy for each new toggle):
```c
static void test_TOGGLE_set_get(void) {
    spu94_set_TOGGLE(state, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_TOGGLE(state));

    spu94_set_TOGGLE(state, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_TOGGLE(state));

    /* Non-zero normalizes to 1 */
    spu94_set_TOGGLE(state, 42);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_TOGGLE(state));
}
```

**NULL safety test pattern** (lines 73-84):
```c
static void test_TOGGLE_null_safety(void) {
    spu94_set_TOGGLE(NULL, 1);  /* no crash */
    TEST_ASSERT_EQUAL_INT(0, spu94_get_TOGGLE(NULL));
}
```

**State-size-under-cap test** (lines 413-415, always include):
```c
static void test_state_size_under_cap(void) {
    TEST_ASSERT_TRUE(spu94_state_size() <= SPU94_STATE_SIZE_MAX);
}
```

**main block pattern** (lines 420-434):
```c
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_X_off_by_default);
    RUN_TEST(test_X_set_get);
    RUN_TEST(test_X_null_safety);
    /* ... topic-specific tests ... */
    RUN_TEST(test_state_size_under_cap);
    return UNITY_END();
}
```

---

### `tests/unit/process/CMakeLists.txt` (config addition)

**Analog:** the ADPCM entry at the bottom of the file (lines 102-112):
```cmake
add_executable(test_process_adpcm test_process_adpcm.c)
target_link_libraries(test_process_adpcm PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_adpcm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_adpcm COMMAND test_process_adpcm)
set_tests_properties(test_process_adpcm PROPERTIES LABELS "process;adpcm_integration")
```

**Apply same pattern for each new test** (note: `src/spu94` include is required for `spu94_state_internal.h`):
```cmake
add_executable(test_process_mixer test_process_mixer.c)
target_link_libraries(test_process_mixer PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_mixer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_mixer COMMAND test_process_mixer)
set_tests_properties(test_process_mixer PROPERTIES LABELS "process;mixer")

add_executable(test_process_dac_integration test_process_dac_integration.c)
target_link_libraries(test_process_dac_integration PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_dac_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_dac_integration COMMAND test_process_dac_integration)
set_tests_properties(test_process_dac_integration PROPERTIES LABELS "process;dac_integration")

add_executable(test_process_latency_comp test_process_latency_comp.c)
target_link_libraries(test_process_latency_comp PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_latency_comp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_latency_comp COMMAND test_process_latency_comp)
set_tests_properties(test_process_latency_comp PROPERTIES LABELS "process;mixer")
```

---

## Shared Patterns

### Q15 Arithmetic (all C files being modified)
**Source:** `include/spu94/spu94_q15.h` (transitively included via `spu94.h`)
**Apply to:** `spu94_process.c` (all fader/send math), fader setter implementations

Three functions used by the mixer:
- `q15_mul_truncate(a, b)` — multiply two Q15 values, return Q15 (truncate remainder)
- `sat_s16(x)` — clamp int32 to int16 range (hard clip)
- `q15_add_sat(a, b)` — add two int16 with saturation

### Toggle with State Reset on Disable
**Source:** `src/spu94/spu94_io_chain.c` lines 148-166
**Apply to:** all four new toggles — `spu94_set_dac_enabled`, `spu94_set_dac_fir_enabled`, `spu94_set_dac_noise_enabled`, `spu94_set_latency_comp`

Pattern: `if (!enabled && state->field)` guard before resetting sub-state, then `state->field = enabled ? 1 : 0`.

### NULL Guard on All Public Functions
**Source:** `src/spu94/spu94_io_chain.c` lines 149, 168 and `src/spu94/spu94_state.c` lines 66, 99
**Apply to:** every new setter and getter function

```c
/* Setters */
if (state == NULL) return;

/* Getters */
if (state == NULL) return 0;   /* int getters */
if (state == NULL) return 0;   /* int16_t getters */
```

### Zero-Fill Convention (init and reset)
**Source:** `src/spu94/spu94_state.c` lines 48-53 (`spu94_zero_bytes`), lines 84 and 117
**Apply to:** `spu94_state.c` init/reset fixups

The `spu94_zero_bytes` helper (no libc, no memset) is the only approved zeroing mechanism. Always call it before any post-zero fixups.

### LFSR Non-Zero Init (DAC noise only)
**Source:** `include/spu94/spu94_dac_noise.h` lines 11-16 (warning comment)
**Apply to:** `spu94_state.c` (init and reset), `spu94_io_chain.c` (DAC toggle disable handler)

`spu94_dac_noise_init()` MUST be called after every `spu94_zero_bytes` that covers the noise state fields. Zero is an absorbing state — no noise output, no error, silent failure.

---

## No Analog Found

All files have close analogs in the codebase. No novel patterns required.

---

## Metadata

**Analog search scope:** `src/spu94/`, `include/spu94/`, `src/standalone/`, `tests/unit/process/`
**Files read:** 12 source files
**Pattern extraction date:** 2026-04-29
