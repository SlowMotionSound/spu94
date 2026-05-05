# Phase 16: Core Tempo API - Pattern Map

**Mapped:** 2026-05-02
**Files analyzed:** 8 (new/modified files)
**Analogs found:** 8 / 8

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/spu94/spu94.h` (append) | API header | declaration | self (existing toggle/error patterns) | exact |
| `src/spu94/spu94_state_internal.h` (modify) | model | state storage | self (existing field blocks) | exact |
| `src/spu94/spu94_tempo.c` (new) | service | CRUD + event-driven | `src/spu94/spu94_io_chain.c` | role-match |
| `src/spu94/spu94_register_io.c` (modify) | service | request-response | self (existing write path) | exact |
| `src/spu94/CMakeLists.txt` (modify) | config | build | self (existing TU list) | exact |
| `tests/unit/tempo/test_tempo_basic.c` (new) | test | unit | `tests/unit/registers/test_register_io.c` | exact |
| `tests/unit/tempo/test_tempo_snap.c` (new) | test | unit | `tests/unit/registers/test_register_io.c` | exact |
| `tests/unit/tempo/test_tempo_binding.c` (new) | test | unit | `tests/unit/registers/test_register_io.c` | exact |
| `tests/unit/tempo/test_tempo_comb.c` (new) | test | unit | `tests/unit/registers/test_register_io.c` | exact |
| `tests/unit/tempo/CMakeLists.txt` (new) | config | build | `tests/unit/adpcm/CMakeLists.txt` | exact |
| `tests/unit/CMakeLists.txt` (modify) | config | build | self (add_subdirectory list) | exact |

## Pattern Assignments

### `include/spu94/spu94.h` (API header, declarations)

**Analog:** self -- append new section following existing patterns

**Boolean toggle pattern** (lines 225-228):
```c
/** Enable/disable ADPCM coloration upstream of the FIR decimator.
 *  Off by default. NULL state is a no-op. */
void     spu94_set_adpcm_enabled(spu94_state *state, int enabled);

/** Query ADPCM coloration state. NULL state returns 0. */
int      spu94_get_adpcm_enabled(const spu94_state *state);
```

**Error-returning setter pattern** (lines 484, 516):
```c
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id);

spu94_result_t spu94_preset_load(spu94_state *state,
                                 const char *buf, size_t buf_len);
```

**Enum typedef pattern** (lines 53-63):
```c
typedef enum {
    SPU94_OK                 = 0,
    SPU94_CLAMPED            = 1,
    SPU94_UNKNOWN_REG        = 2,
    SPU94_TYPE_MISMATCH      = 3,
    SPU94_INVALID_STATE      = 4,
    SPU94_WORK_BUF_TOO_SMALL = 5,
    SPU94_INVALID_ARG        = 6
} spu94_result_t;
```

**Section header comment pattern** (lines 215-221):
```c
/* -----------------------------------------------------------------------
 * ADPCM coloration stage (M2 Phase 2, ADPCM-INT-01..06)
 *
 * When enabled, spu94_process routes input PCM through ADPCM
 * encode+decode before the FIR decimator, introducing PS1-characteristic
 * quantization noise and filter ringing. Off by default. Adds 28 samples
 * of latency (one ADPCM block) when enabled.
 * ----------------------------------------------------------------------- */
```

---

### `src/spu94/spu94_state_internal.h` (model, state storage)

**Analog:** self -- add new field block at end before `oob_tap_count`

**Field block comment pattern** (lines 160-172):
```c
    /* -----------------------------------------------------------------
     * Phase 7 (DAC-INT / Mixer): send/return mixer state.
     * Six Q15 faders/sends, latency compensation delay buffer,
     * DAC section toggles and module state.
     *
     * Signal flow (D-01): input_gain -> bus split -> dry/patina buses
     * -> reverb sends -> reverb -> three-fader master mixer -> DAC
     * section -> output.
     *
     * All Q15 int16 faders/sends default to 0x0000 (silence) per
     * zero-init convention. Hosts MUST set fader values before
     * expecting audio output (mixer console metaphor).
     * ----------------------------------------------------------------- */
```

**Toggle field pattern** (lines 189-192):
```c
    uint8_t        dac_enabled;       /* master toggle, 0=off (default) */
    uint8_t        dac_fir_enabled;   /* FIR sub-toggle, 0=off (default) */
    uint8_t        dac_noise_enabled; /* noise sub-toggle, 0=off (default) */
    uint8_t        dac_true_oversample; /* 0=v1.2 approx, 1=v1.3 true 8x */
```

**Sizing constraint** (lines 220-222):
```c
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");
```

---

### `src/spu94/spu94_tempo.c` (new service, CRUD + event-driven)

**Analog:** `src/spu94/spu94_io_chain.c` (implementation of toggle setters) + `src/spu94/spu94_presets.c` (static const table + load_preset validation)

**Imports pattern** (from spu94_io_chain.c lines 1-3, spu94_presets.c lines 18-20):
```c
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
```

**Simple boolean toggle implementation** (spu94_io_chain.c lines 341-349):
```c
void spu94_set_dac_true_oversample(spu94_state *state, int enabled) {
    if (state == NULL) return;
    state->dac_true_oversample = enabled ? 1 : 0;
}

int spu94_get_dac_true_oversample(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_true_oversample;
}
```

**Toggle with side-effect cleanup** (spu94_io_chain.c lines 151-168):
```c
void spu94_set_adpcm_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->adpcm_enabled) {
        /* ADPCM-INT-04: discard partial accumulation buffer on disable. */
        state->adpcm_buf_pos = 0;
        for (int j = 0; j < SPU94_ADPCM_BLOCK_SAMPLES; j++) {
            state->adpcm_out_buf_l[j] = 0;
            state->adpcm_out_buf_r[j] = 0;
        }
        state->adpcm_state_l.old = 0;
        state->adpcm_state_l.older = 0;
        state->adpcm_state_r.old = 0;
        state->adpcm_state_r.older = 0;
    }
    state->adpcm_enabled = enabled ? 1 : 0;
}
```

**Error-returning setter with validation** (spu94_presets.c lines 548-569):
```c
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id) {
    if (state == NULL) return SPU94_INVALID_STATE;
    if ((int)id < 0 || (int)id >= (int)SPU94_PRESET__COUNT) {
        return SPU94_INVALID_ARG;
    }
    const size_t required = spu94_preset_min_work_buf_size(id);
    if (state->work_buf_size < required) {
        return SPU94_WORK_BUF_TOO_SMALL;
    }
    const spu94_preset_t *p = &spu94_presets[id];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        const int16_t raw = p->regs[r];
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            (void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
        } else {
            (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
        }
    }
    return SPU94_OK;
}
```

**Static const lookup table** (spu94_presets.c lines 22-25, pattern for subdivision table):
```c
_Static_assert(sizeof(((const spu94_preset_t *)0)->regs) / sizeof(int16_t)
               == SPU94_REG__COUNT,
               "spu94_preset_t.regs length must equal SPU94_REG__COUNT");
```

---

### `src/spu94/spu94_register_io.c` (modify -- write interception hook)

**Analog:** self -- the TICK_LATCHED write path to hook into

**Write interception point** (lines 112-144):
```c
spu94_result_t spu94_set_reg_u16(spu94_state *state, spu94_reg_t reg, uint16_t value) {
    if (state == (spu94_state *)0) {
        return SPU94_INVALID_STATE;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return SPU94_UNKNOWN_REG;
    }
    if (spu94_reg_type(reg) != SPU94_REG_TYPE_U16) {
        return SPU94_TYPE_MISMATCH;
    }

    int16_t stored = (int16_t)value;

    spu94_write_policy_t policy = spu94_write_policy_table[reg];
    if (policy == SPU94_WRITE_IMMEDIATE) {
        state->reg_values[reg]     = stored;
        state->pending_values[reg] = stored;
        state->pending_mask &= ~(UINT64_C(1) << reg);
        if (reg == SPU94_REG_mBASE) {
            spu94_mbase_on_write(state, value);
        }
    } else {
        /* TICK_LATCHED: stage into pending; do NOT touch reg_values[]. */
        state->pending_values[reg] = stored;
        state->pending_mask |= (UINT64_C(1) << reg);
    }
    return SPU94_OK;
}
```

**Hook-call pattern** (line 134-136 -- the mBASE side-effect call is the model for adding tempo binding-state transition):
```c
        if (reg == SPU94_REG_mBASE) {
            spu94_mbase_on_write(state, value);
        }
```

**Cross-TU extern declaration** (lines 80-86):
```c
/* Defined in spu94_write_policy.c (Plan 03 Task 2). */
extern const spu94_write_policy_t spu94_write_policy_table[SPU94_REG__COUNT];

/* Defined in spu94_write_policy.c (Plan 03 Task 2 stub; Plan 04 replaces). */
void spu94_mbase_on_write(struct spu94_state *state, uint16_t new_mbase);
```

---

### `src/spu94/CMakeLists.txt` (config, build -- add new TU)

**Analog:** self -- existing TU registration

**Pattern** (lines 5-26):
```cmake
add_library(spu94_obj OBJECT
    spu94_state.c
    spu94_registers.c
    spu94_register_io.c
    spu94_write_policy.c
    spu94_pending.c
    spu94_reverb.c
    spu94_tick.c
    spu94_buffer.c
    spu94_fir_coef.c
    spu94_fir.c
    spu94_io_chain.c
    spu94_process.c
    spu94_presets.c
    spu94_preset_io.c
    spu94_adpcm.c
    spu94_adpcm_encode.c
    vag.c
    spu94_dac_fir.c
    spu94_dac_fir_coef.c
    spu94_dac_noise.c
)
```

New TU `spu94_tempo.c` appends to this OBJECT list.

---

### `tests/unit/tempo/CMakeLists.txt` (new config, build)

**Analog:** `tests/unit/adpcm/CMakeLists.txt`

**Pattern** (full file):
```cmake
add_executable(test_adpcm_decode test_adpcm_decode.c)
target_link_libraries(test_adpcm_decode PRIVATE unity spu94_static)
add_test(NAME adpcm_decode_unit COMMAND test_adpcm_decode)

add_executable(test_adpcm_encode test_adpcm_encode.c)
target_link_libraries(test_adpcm_encode PRIVATE unity spu94_static)
add_test(NAME adpcm_encode_unit COMMAND test_adpcm_encode)
```

---

### `tests/unit/tempo/test_tempo_basic.c` (new test, unit)

**Analog:** `tests/unit/registers/test_register_io.c`

**Test file header + imports** (lines 1-25):
```c
/* tests/unit/registers/test_register_io.c
 * Phase 2 Plan 03 Task 1 (TDD RED): exercise the engine-layer typed
 * register-IO accessors with signedness validation...
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>

void setUp(void) {}
void tearDown(void) {}
```

**State allocation helper** (lines 31-36):
```c
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[1024];

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}
```

**Assertion patterns** (lines 38-58):
```c
void test_set_get_i16_roundtrip_vIIR(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_i16(s, SPU94_REG_vIIR, -32000));
    TEST_ASSERT_EQUAL_INT16(-32000, spu94_get_reg_i16(s, SPU94_REG_vIIR));
}

void test_set_get_u16_roundtrip_dAPF1_pending(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_dAPF1, 0x1234u));
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, spu94_get_reg_u16_pending(s, SPU94_REG_dAPF1));

    spu94_tick(s);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, spu94_get_reg_u16(s, SPU94_REG_dAPF1));
}
```

**Coverage map comment style** (from test_adpcm_decode.c lines 14-32):
```c
/* COVERAGE MAP -- TEMPO-TEST-01 known-vector checklist
 *   bpm set/get roundtrip   : test_set_get_tempo
 *   bpm=0 rejected          : test_set_tempo_zero_rejected
 *   null state no-op        : test_set_tempo_null_state
 *   ...
 */
```

---

## Shared Patterns

### NULL-Safety Guard
**Source:** `src/spu94/spu94_io_chain.c` (lines 151, 341) and `src/spu94/spu94_register_io.c` (lines 92-95, 112-115)
**Apply to:** All new API functions (tempo setters/getters, subdivision functions)
```c
/* void-returning setters: */
void spu94_set_X(spu94_state *state, ...) {
    if (state == NULL) return;
    ...
}

/* Value-returning getters: */
int spu94_get_X(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->field;
}

/* Error-code-returning setters: */
spu94_result_t spu94_set_Y(spu94_state *state, ...) {
    if (state == NULL) return SPU94_INVALID_STATE;
    ...
    return SPU94_OK;
}
```

### Error Code Usage
**Source:** `include/spu94/spu94.h` (lines 53-63)
**Apply to:** `spu94_set_tempo`, `spu94_set_subdivision`
```c
/* Use existing SPU94_INVALID_ARG for:
 *   - bpm == 0
 *   - subdivision out of range
 *   - overflow (samples > UINT16_MAX)
 *   - comb delay exceeds buffer geometry
 * Use SPU94_INVALID_STATE for:
 *   - state == NULL
 * Return SPU94_OK on success.
 */
```

### Register Write via Engine Layer
**Source:** `src/spu94/spu94_register_io.c` (lines 112-144)
**Apply to:** `spu94_tempo.c` when snapping registers to subdivision values
```c
/* All register mutations go through spu94_set_reg_u16.
 * This honors TICK_LATCHED policy automatically.
 * Cast return to (void) when calling internally (per spu94_load_preset pattern). */
(void)spu94_set_reg_u16(state, target_hw_reg, (uint16_t)computed_samples);
```

### Build Registration
**Source:** `src/spu94/CMakeLists.txt` (lines 5-26) and `tests/unit/CMakeLists.txt` (lines 13-24)
**Apply to:** New `spu94_tempo.c` and `tests/unit/tempo/`
```cmake
# In src/spu94/CMakeLists.txt -- append to OBJECT list:
    spu94_tempo.c

# In tests/unit/CMakeLists.txt -- append:
add_subdirectory(tempo)
```

### Test State Provisioning
**Source:** `tests/unit/registers/test_register_io.c` (lines 31-36)
**Apply to:** All `tests/unit/tempo/test_tempo_*.c` files
```c
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[SPU94_WORK_BUF_MAX_BYTES];  /* tempo tests may need full buffer for preset loads */

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}
```

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All files have strong analogs in the existing codebase |

## Metadata

**Analog search scope:** `include/spu94/`, `src/spu94/`, `tests/unit/`
**Files scanned:** 5 analog files read in full or targeted ranges
**Pattern extraction date:** 2026-05-02
