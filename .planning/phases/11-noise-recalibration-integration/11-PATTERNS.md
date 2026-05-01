# Phase 11: Noise Recalibration + Integration - Pattern Map

**Mapped:** 2026-04-30
**Files analyzed:** 12 (8 modified, 2 new source, 2 CMakeLists modified)
**Analogs found:** 12 / 12

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/spu94/spu94_dac_fir.c` | service | transform | itself (existing `spu94_dac_fir_step_8x`) | exact |
| `src/spu94/spu94_dac_noise.c` | service | transform | itself (existing `DAC_NOISE_SHIFT` + `spu94_dac_noise_step`) | exact |
| `src/spu94/spu94_process.c` | controller | request-response | itself (existing DAC section lines 114-124) | exact |
| `src/spu94/spu94_io_chain.c` | service | CRUD | itself (existing `spu94_set_dac_fir_enabled` pattern) | exact |
| `src/spu94/spu94_state_internal.h` | model | config | itself (existing `dac_noise_enabled` field) | exact |
| `include/spu94/spu94.h` | config | config | itself (existing `spu94_set_dac_noise_enabled` declaration) | exact |
| `include/spu94/spu94_dac_fir.h` | config | config | itself (existing `spu94_dac_fir_step_8x` declaration) | exact |
| `include/spu94/spu94_dac_noise.h` | config | config | itself (comment-only update) | exact |
| `tests/unit/dac_noise/test_dac_noise_8x.c` | test | transform | `tests/unit/dac_noise/test_dac_noise_amplitude.c` + `test_dac_noise_spectral.c` | exact |
| `tests/unit/process/test_process_dac_mode_toggle.c` | test | request-response | `tests/unit/process/test_process_dac_toggle_transitions.c` | exact |
| `tests/unit/dac_noise/CMakeLists.txt` | config | config | itself (existing 3-test block) | exact |
| `tests/unit/process/CMakeLists.txt` | config | config | itself (existing `test_process_dac_toggle_transitions` block) | exact |

## Pattern Assignments

### `src/spu94/spu94_dac_fir.c` (service, transform) -- ADD `spu94_dac_fir_step_8x_with_noise`

**Analog:** itself -- the new function is a variant of `spu94_dac_fir_step_8x` at lines 187-253

**Imports pattern** (lines 24-28):
```c
#include "spu94_dac_fir_internal.h"
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_q15.h>
#include <stdint.h>
#include <string.h>
```

Add `#include <spu94/spu94_dac_noise.h>` for the noise state type and step function.

**Core pattern -- Stage 3 loop** (lines 227-247) -- the noise injection point:
```c
    /* Stage 3: 8 evaluations at 352.8kHz. All 8 must execute because each
     * push advances the delay line state affecting future calls. Only the
     * last output survives decimation (DSP-06). */
    int16_t s3_last = 0;
    for (int j = 0; j < 4; j++) {
        dac_fir_push(state->stage3_delay, &state->stage3_idx,
                     s2[j], DAC_FIR_STAGE3_NTAPS);
        (void)dac_fir_stage_apply(state->stage3_delay, state->stage3_idx,
                                  DAC_FIR_STAGE3_NTAPS,
                                  dac_interp_stage3,
                                  dac_fir_stage3_pairs,
                                  DAC_FIR_STAGE3_NPAIRS);

        dac_fir_push(state->stage3_delay, &state->stage3_idx,
                     0, DAC_FIR_STAGE3_NTAPS);
        s3_last = dac_fir_stage_apply(state->stage3_delay, state->stage3_idx,
                                      DAC_FIR_STAGE3_NTAPS,
                                      dac_interp_stage3,
                                      dac_fir_stage3_pairs,
                                      DAC_FIR_STAGE3_NPAIRS);
    }
```

The `_with_noise` variant replaces the `(void)` discard with a noise-addition on EVERY Stage 3 evaluation (both the real-push and zero-push evaluations). Each of the 8 calls to `dac_fir_stage_apply` in Stage 3 gets `q15_add_sat(result, spu94_dac_noise_step(noise))` applied to it. The final `s3_last` value (which includes noise from its own tick) goes through the same `sat_s16((int32_t)s3_last << 3)` gain compensation.

**Gain compensation pattern** (line 252):
```c
    return sat_s16((int32_t)s3_last << 3);
```

This is preserved identically in the new function. The `DAC_NOISE_SHIFT_8X` calibration must account for this 18dB amplification.

---

### `src/spu94/spu94_dac_noise.c` (service, transform) -- ADD `DAC_NOISE_SHIFT_8X`

**Analog:** itself -- the existing `DAC_NOISE_SHIFT` define at line 45

**Constant pattern** (lines 40-45):
```c
/* Amplitude scaling: right-shift raw LFSR output to target ~-90 dB RMS
 * in the audio band after 2nd-order HP shaping. This is a compile-time
 * constant, tunable per D-06 ("treat the level as tunable later").
 * Derivation: RESEARCH.md Noise Amplitude Derivation section.
 * Validated by tests/unit/dac_noise/test_dac_noise_amplitude.c. */
#define DAC_NOISE_SHIFT  14
```

Add a parallel constant immediately after:
```c
#define DAC_NOISE_SHIFT_8X  9  /* For 352.8kHz operation -- empirically tuned */
```

No change to `spu94_dac_noise_step` itself. The `DAC_NOISE_SHIFT_8X` constant is consumed by the new `spu94_dac_fir_step_8x_with_noise` function in `spu94_dac_fir.c` -- it needs to be accessible from there. Two options: (a) define it in the noise .c file and expose via a new internal header or a macro in `spu94_dac_noise.h`, or (b) define it directly in `spu94_dac_fir.c`. Option (a) is cleaner -- the noise module owns its amplitude constants.

**LFSR + HP shaping function** (lines 52-74) -- the step function is called unchanged from the new combined FIR+noise function:
```c
int16_t spu94_dac_noise_step(spu94_dac_noise_state *state) {
    /* Step the Galois LFSR */
    uint32_t lfsr = state->lfsr;
    uint32_t bit = lfsr & 1u;
    lfsr >>= 1;
    if (bit) lfsr ^= DAC_NOISE_LFSR_FEEDBACK;
    state->lfsr = lfsr;

    int16_t x = (int16_t)(((int32_t)(lfsr >> 16) - 32768) >> DAC_NOISE_SHIFT);

    int32_t y = (int32_t)x - 2 * (int32_t)state->x_prev + (int32_t)state->x_prev2;
    state->x_prev2 = state->x_prev;
    state->x_prev = x;

    return sat_s16(y);
}
```

IMPORTANT: The existing `spu94_dac_noise_step` uses `DAC_NOISE_SHIFT=14` (44.1kHz calibration). For 352.8kHz noise injection, a separate step function (e.g., `spu94_dac_noise_step_8x`) using `DAC_NOISE_SHIFT_8X` is needed, OR the combined function in `spu94_dac_fir.c` applies different scaling internally. The RESEARCH.md recommends keeping the shift as a constant and having the 8x-aware function use it.

---

### `src/spu94/spu94_process.c` (controller, request-response) -- MODIFY DAC section

**Analog:** itself -- existing DAC section at lines 114-124

**Current DAC wiring** (lines 114-124):
```c
        /* 7. DAC section: true 8x oversampled interpolation (Phase 10) */
        if (state->dac_enabled) {
            if (state->dac_fir_enabled) {
                out_l = spu94_dac_fir_step_8x(&state->dac_fir_l, out_l);
                out_r = spu94_dac_fir_step_8x(&state->dac_fir_r, out_r);
            }
            if (state->dac_noise_enabled) {
                out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
                out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
            }
        }
```

**New pattern** -- mode-aware dispatch using `dac_true_oversample` toggle:
```c
        if (state->dac_enabled) {
            if (state->dac_true_oversample) {
                /* v1.3: true 8x oversampling */
                if (state->dac_fir_enabled && state->dac_noise_enabled) {
                    out_l = spu94_dac_fir_step_8x_with_noise(...);
                    out_r = spu94_dac_fir_step_8x_with_noise(...);
                } else if (state->dac_fir_enabled) {
                    out_l = spu94_dac_fir_step_8x(&state->dac_fir_l, out_l);
                    out_r = spu94_dac_fir_step_8x(&state->dac_fir_r, out_r);
                } else if (state->dac_noise_enabled) {
                    /* noise-only at 352.8kHz: edge case */
                }
            } else {
                /* v1.2: approximate single-rate */
                if (state->dac_fir_enabled) {
                    out_l = spu94_dac_fir_step(&state->dac_fir_l, out_l);
                    out_r = spu94_dac_fir_step(&state->dac_fir_r, out_r);
                }
                if (state->dac_noise_enabled) {
                    out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
                    out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
                }
            }
        }
```

**Imports pattern** (lines 27-34) -- already includes all needed headers:
```c
#include <spu94/spu94.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"
#include <stdint.h>
#include <stddef.h>
```

---

### `src/spu94/spu94_io_chain.c` (service, CRUD) -- ADD toggle + MODIFY latency

**Analog:** itself -- existing toggle pattern at lines 300-326

**Toggle pattern -- exact template to copy** (lines 300-312):
```c
void spu94_set_dac_fir_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->dac_fir_enabled) {
        spu94_dac_fir_init(&state->dac_fir_l);
        spu94_dac_fir_init(&state->dac_fir_r);
    }
    state->dac_fir_enabled = enabled ? 1 : 0;
}

int spu94_get_dac_fir_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_fir_enabled;
}
```

The new `spu94_set_dac_true_oversample` / `spu94_get_dac_true_oversample` pair follows this EXACT shape. No reset-on-disable needed (the toggle selects a processing path, not state).

**Latency pattern -- existing function to modify** (lines 176-179):
```c
uint32_t spu94_get_total_latency_samples(const spu94_state *state) {
    if (state == NULL) return SPU94_LATENCY_SAMPLES;
    return SPU94_LATENCY_SAMPLES +
           (state->adpcm_enabled ? SPU94_ADPCM_BLOCK_SAMPLES : 0u);
}
```

Extended with mode-aware DAC FIR group delay:
```c
#define DAC_FIR_GROUP_DELAY_V12  35u
#define DAC_FIR_GROUP_DELAY_V13  15u

uint32_t spu94_get_total_latency_samples(const spu94_state *state) {
    if (state == NULL) return SPU94_LATENCY_SAMPLES;
    uint32_t lat = SPU94_LATENCY_SAMPLES;
    if (state->adpcm_enabled)
        lat += SPU94_ADPCM_BLOCK_SAMPLES;
    if (state->dac_enabled && state->dac_fir_enabled)
        lat += state->dac_true_oversample
            ? DAC_FIR_GROUP_DELAY_V13
            : DAC_FIR_GROUP_DELAY_V12;
    return lat;
}
```

---

### `src/spu94/spu94_state_internal.h` (model, config) -- ADD field

**Analog:** itself -- existing DAC toggle fields at lines 189-191

**Existing field block** (lines 188-195):
```c
    /* DAC section (D-09 through D-12) */
    uint8_t        dac_enabled;       /* master toggle, 0=off (default) */
    uint8_t        dac_fir_enabled;   /* FIR sub-toggle, 0=off (default) */
    uint8_t        dac_noise_enabled; /* noise sub-toggle, 0=off (default) */
    spu94_dac_fir_state   dac_fir_l;  /* FIR state, L channel */
    spu94_dac_fir_state   dac_fir_r;  /* FIR state, R channel */
    spu94_dac_noise_state dac_noise_l;/* noise state, L channel */
    spu94_dac_noise_state dac_noise_r;/* noise state, R channel */
```

Add new field in the same block, after `dac_noise_enabled`:
```c
    uint8_t        dac_true_oversample; /* 0=v1.2 approx, 1=v1.3 true 8x (Phase 11 CMP-01) */
```

Default value: 1 (v1.3 ON), matching Phase 10's switch to `_step_8x`. Set in `spu94_state.c` during `spu94_init`/`spu94_reset`.

**Static assert pattern** (lines 220-221):
```c
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");
```

Adding a `uint8_t` field will not exceed the 16384-byte limit.

---

### `include/spu94/spu94.h` (config) -- ADD declarations

**Analog:** itself -- existing DAC toggle declarations at lines 292-299

**Declaration pattern** (lines 292-299):
```c
void     spu94_set_dac_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_enabled(const spu94_state *state);

void     spu94_set_dac_fir_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_fir_enabled(const spu94_state *state);

void     spu94_set_dac_noise_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_noise_enabled(const spu94_state *state);
```

Add after `spu94_get_dac_noise_enabled`:
```c
void     spu94_set_dac_true_oversample(spu94_state *state, int enabled);
int      spu94_get_dac_true_oversample(const spu94_state *state);
```

Also update the `spu94_get_total_latency_samples` docstring (line 231-233) to reflect that it now includes DAC FIR group delay when DAC is enabled.

---

### `include/spu94/spu94_dac_fir.h` (config) -- ADD declaration

**Analog:** itself -- existing `spu94_dac_fir_step_8x` declaration at lines 44-46

**Declaration pattern** (lines 44-46):
```c
int16_t spu94_dac_fir_step_8x(spu94_dac_fir_state *state, int16_t input);
```

Add new declaration:
```c
/* Process one 44.1kHz Q15 sample through the 8x cascade with noise
 * injection at 352.8kHz (Phase 11, DSP-05). Noise is added to each of
 * the 8 Stage 3 evaluations before decimation, producing spectrally
 * shaped noise through the reconstruction filter.
 * Mono API -- caller invokes once per channel. */
int16_t spu94_dac_fir_step_8x_with_noise(spu94_dac_fir_state *fir,
                                          spu94_dac_noise_state *noise,
                                          int16_t input);
```

Note: this header needs `#include <spu94/spu94_dac_noise.h>` added for the noise state type, OR a forward declaration of `spu94_dac_noise_state`. The forward-declaration approach avoids adding a dependency to the header. However, since the struct is a typedef (not a tagged struct), a forward declaration is not possible in C. The include is needed.

---

### `include/spu94/spu94_dac_noise.h` (config) -- UPDATE comment

**Analog:** itself -- comment-only change. Update the header comment (lines 1-16) and the `spu94_dac_noise_step` docstring (lines 32-41) to note that at 352.8kHz operation the noise uses a different shift constant (`DAC_NOISE_SHIFT_8X`).

---

### `tests/unit/dac_noise/test_dac_noise_8x.c` (test, transform) -- NEW

**Analog:** `tests/unit/dac_noise/test_dac_noise_amplitude.c` (for amplitude test) + `tests/unit/dac_noise/test_dac_noise_spectral.c` (for spectral test)

**Amplitude test pattern** (test_dac_noise_amplitude.c lines 1-56):
```c
#include "unity.h"
#include <spu94/spu94_dac_noise.h>
#include <math.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void test_noise_rms_near_minus_90dB(void) {
    #define N_SAMPLES  44100
    #define FULL_SCALE 32768.0

    spu94_dac_noise_state st;
    spu94_dac_noise_init(&st, 0xACE1u);

    int64_t sum_sq = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        int16_t out = spu94_dac_noise_step(&st);
        sum_sq += (int64_t)out * (int64_t)out;
    }

    double rms = sqrt((double)sum_sq / (double)N_SAMPLES);
    double rms_dB = 20.0 * log10(rms / FULL_SCALE);

    char msg[256];
    snprintf(msg, sizeof(msg), ...);

    TEST_ASSERT_TRUE_MESSAGE(rms_dB >= -100.0, msg);
    TEST_ASSERT_TRUE_MESSAGE(rms_dB <= -80.0, msg);
}
```

The 8x test variant replaces `spu94_dac_noise_step` with the combined FIR+noise path (`spu94_dac_fir_step_8x_with_noise`). It feeds silence through the cascade and measures the RMS of the output (which should be noise-only). Must include `<spu94/spu94_dac_fir.h>` in addition to the noise header. Run 44100 samples, measure RMS in-band, assert -100 to -80 dB.

**Spectral test pattern** (test_dac_noise_spectral.c lines 1-96):
```c
static double goertzel_mag_sq(const int16_t *samples, int N, int k) {
    double w = 2.0 * 3.14159265358979323846 * (double)k / (double)N;
    double coeff = 2.0 * cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < N; i++) {
        s0 = (double)samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}
```

The 8x spectral test verifies that noise through the cascade is NOT flat white noise -- it should show spectral shaping from the lowpass reconstruction filter. Compare power at a low frequency (e.g., 2kHz) vs a high frequency (e.g., 18kHz). After cascade filtering, high-frequency noise is attenuated relative to the 44.1kHz-only case.

**main() pattern** (common to all dac_noise tests):
```c
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_name_here);
    return UNITY_END();
}
```

---

### `tests/unit/process/test_process_dac_mode_toggle.c` (test, request-response) -- NEW

**Analog:** `tests/unit/process/test_process_dac_toggle_transitions.c` (lines 1-331)

**Fixtures pattern** (lines 22-43):
```c
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_a[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_a[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state_a = NULL;

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_b[SPU94_WORK_BUF_MAX_BYTES];

void setUp(void) {
    state_a = spu94_init(state_buf_a, sizeof state_buf_a,
                         work_buf_a, sizeof work_buf_a);
    TEST_ASSERT_NOT_NULL(state_a);
}

void tearDown(void) {
    state_a = NULL;
}
```

**Helper pattern** (lines 48-53):
```c
static void set_unity_passthrough(spu94_state *s) {
    spu94_set_input_gain(s,   0x7FFF);
    spu94_set_dry_fader(s,    0x7FFF);
    spu94_set_reverb_fader(s, 0x7FFF);
    spu94_set_dry_send(s,     0x7FFF);
}
```

**Includes pattern** (lines 12-19):
```c
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include "spu94_state_internal.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
```

**Test function pattern** -- A/B comparison (test_dac_toggle_transitions.c lines 67-112):
```c
static void test_dac_on_off_on_bit_identical(void) {
    const uint32_t N = 256;
    int16_t input[256], dirty_input[256];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
        dirty_input[i] = (int16_t)(((i * 6271 + 5678) % 30000) - 15000);
    }
    /* ... two states, compare output ... */
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_l, out_b_l, N);
}
```

New test file should cover:
1. `test_dac_true_oversample_set_get` -- set/get round-trip + NULL safety (follow `test_latency_comp_set_get` pattern from `test_process_latency_comp.c` lines 60-70)
2. `test_dac_true_oversample_default` -- assert default is 1 (v1.3 on)
3. `test_v12_v13_produce_different_output` -- process same input through v1.2 and v1.3 modes, assert outputs differ
4. `test_v12_regression` -- v1.2 mode output bit-matches Phase 10 v1.2 path behavior
5. `test_latency_reports_correct_for_mode` -- check `spu94_get_total_latency_samples` returns different values for v1.2 vs v1.3 mode with DAC FIR enabled

**Latency test pattern** (from test_process_latency_comp.c lines 53-55):
```c
static void test_latency_comp_default_on(void) {
    TEST_ASSERT_EQUAL_INT(1, spu94_get_latency_comp(state));
}
```

---

### `tests/unit/dac_noise/CMakeLists.txt` (config) -- ADD entries

**Analog:** itself -- existing 3-test block (lines 1-18)

**CMake pattern for test with math dependency** (lines 10-13):
```cmake
add_executable(test_dac_noise_spectral test_dac_noise_spectral.c)
target_link_libraries(test_dac_noise_spectral PRIVATE unity spu94_static m)
add_test(NAME dac_noise_spectral COMMAND test_dac_noise_spectral)
set_tests_properties(dac_noise_spectral PROPERTIES LABELS "dac_noise")
```

New entry follows same shape:
```cmake
add_executable(test_dac_noise_8x test_dac_noise_8x.c)
target_link_libraries(test_dac_noise_8x PRIVATE unity spu94_static m)
add_test(NAME dac_noise_8x_amplitude COMMAND test_dac_noise_8x)
set_tests_properties(dac_noise_8x_amplitude PROPERTIES LABELS "dac_noise")
```

Note: links against `m` (libm) because test uses `sqrt`, `log10`, `cos`.

---

### `tests/unit/process/CMakeLists.txt` (config) -- ADD entry

**Analog:** itself -- existing toggle test block (lines 142-148)

**CMake pattern for process integration test** (lines 142-148):
```cmake
add_executable(test_process_dac_toggle_transitions test_process_dac_toggle_transitions.c)
target_link_libraries(test_process_dac_toggle_transitions PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_dac_toggle_transitions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_dac_toggle_transitions COMMAND test_process_dac_toggle_transitions)
set_tests_properties(test_process_dac_toggle_transitions PROPERTIES LABELS "process;dac_integration")
```

New entry follows same shape:
```cmake
add_executable(test_process_dac_mode_toggle test_process_dac_mode_toggle.c)
target_link_libraries(test_process_dac_mode_toggle PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_dac_mode_toggle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_dac_mode_toggle COMMAND test_process_dac_mode_toggle)
set_tests_properties(test_process_dac_mode_toggle PROPERTIES LABELS "process;dac_integration")
```

---

## Shared Patterns

### Toggle set/get (applies to `spu94_io_chain.c`, `spu94.h`, `spu94_state_internal.h`)
**Source:** `src/spu94/spu94_io_chain.c` lines 300-326
**Apply to:** New `dac_true_oversample` toggle

Every toggle in the codebase follows this exact 3-part shape:
1. **State field:** `uint8_t` in `spu94_state_internal.h` DAC section
2. **Setter:** NULL-guard, optional state-reset-on-disable, `enabled ? 1 : 0` assignment
3. **Getter:** NULL-guard returns 0, otherwise returns field value
4. **Public declaration:** `void spu94_set_X(spu94_state *, int)` + `int spu94_get_X(const spu94_state *)`

For `dac_true_oversample`: NO reset-on-disable needed (switching mode does not corrupt state; both paths share delay lines).

### Q15 saturation arithmetic (applies to `spu94_dac_fir.c` noise injection)
**Source:** `src/spu94/spu94_dac_noise.c` line 73 and `src/spu94/spu94_process.c` line 121
**Apply to:** Noise injection inside Stage 3 loop

```c
/* From spu94_process.c line 121 -- noise addition uses q15_add_sat */
out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
```

### Unity test harness (applies to both new test files)
**Source:** all existing test files
**Apply to:** `test_dac_noise_8x.c`, `test_process_dac_mode_toggle.c`

```c
#include "unity.h"
void setUp(void) {}
void tearDown(void) {}
/* ... test functions ... */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_name);
    return UNITY_END();
}
```

### CMake test registration (applies to both CMakeLists.txt modifications)
**Source:** existing entries in both CMakeLists.txt files
**Apply to:** New test executable entries

```cmake
add_executable(<target> <source.c>)
target_link_libraries(<target> PRIVATE unity spu94_static [m] [spu94_warnings])
[target_include_directories(<target> PRIVATE ${CMAKE_SOURCE_DIR}/include [${CMAKE_SOURCE_DIR}/src/spu94])]
add_test(NAME <test_name> COMMAND <target>)
set_tests_properties(<test_name> PROPERTIES LABELS "<label>")
```

Process tests additionally need `spu94_warnings` and `target_include_directories` pointing to `src/spu94` (for `spu94_state_internal.h`). DAC noise tests link `m` (libm) and do NOT need internal header access.

---

## No Analog Found

No files in this phase lack a close analog. Every file to create or modify has an exact template in the existing codebase.

## Metadata

**Analog search scope:** `src/spu94/`, `include/spu94/`, `tests/unit/dac_noise/`, `tests/unit/process/`, `tests/unit/dac_fir/`
**Files scanned:** 30+ source files across src, include, and tests directories
**Pattern extraction date:** 2026-04-30
