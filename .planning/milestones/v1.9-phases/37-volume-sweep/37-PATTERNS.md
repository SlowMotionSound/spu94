# Phase 37: Volume Sweep - Pattern Map

**Mapped:** 2026-05-22
**Files analyzed:** 9 new/modified files
**Analogs found:** 9 / 9

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/spu94/spu94_envelope_step.h` | utility (header-only or thin) | transform | `include/spu94/spu94_q15.h` (header pattern), `src/spu94/spu94_adsr.c` (math source) | exact |
| `src/spu94/spu94_envelope_step.c` | utility (implementation) | transform | `src/spu94/spu94_adsr.c` lines 96-251 (extracted from) | exact |
| `include/spu94/spu94_sweep.h` | model (state struct + API) | transform | `include/spu94/spu94_noise.h` | exact |
| `src/spu94/spu94_sweep.c` | service (DSP tick) | transform | `src/spu94/spu94_noise.c` | exact |
| `src/spu94/spu94_adsr.c` | service (DSP tick, MODIFIED) | transform | itself (refactor to call shared helper) | exact |
| `include/spu94/spu94_voice.h` | model (struct + API, MODIFIED) | CRUD | itself (add sweep fields to voice_t, add sweep API to mixer) | exact |
| `src/spu94/spu94_voice.c` | controller (mixer tick, MODIFIED) | request-response | itself (add sweep tick call, add set_sweep API) | exact |
| `tests/unit/voice/test_sweep.c` | test | batch | `tests/unit/voice/test_noise_gen.c` | exact |
| `tests/unit/voice/CMakeLists.txt` | config (MODIFIED) | N/A | itself (add test_sweep target) | exact |

## Pattern Assignments

### `include/spu94/spu94_envelope_step.h` (utility, transform)

**Analog:** `include/spu94/spu94_noise.h`

**Header guard + doc comment pattern** (lines 1-16):
```c
/* include/spu94/spu94_noise.h -- Phase 36 Plan 01
 *
 * Public API for the PS1 SPU global noise generator. [description]
 *
 * [citation and context notes]
 *
 * WARNING: [usage warnings if any]
 */
#ifndef SPU94_NOISE_H
#define SPU94_NOISE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
```

**Struct definition pattern** (lines 27-32):
```c
/* [CITED: nocash psxspx-spu-noise-generator.htm]
 * [CITED: DuckStation spu.cpp Reset() -- seed = 1] */
typedef struct {
    int16_t  level;       /* current noise output (-0x8000..+0x7FFF) */
    int32_t  timer;       /* countdown timer (MUST be signed for < 0 check) */
    uint8_t  shift;       /* 0..15 from SPUCNT[13:10] (NoiseShift) */
    uint8_t  step;        /* 4..7 from SPUCNT[9:8]+4 (NoiseStep) */
} spu94_noise_gen_t;
```

**Function declaration pattern** (lines 37-45):
```c
/* Initialize the noise generator to its reset state.
 * [contract documentation]
 * MUST be called instead of memset (seed=0 is absorbing). */
void spu94_noise_gen_init(spu94_noise_gen_t *ng);

/* Advance the noise generator by one tick (44.1 kHz rate).
 * [detailed behavior documentation]
 * RT-safe: [constraints] */
void spu94_noise_gen_tick(spu94_noise_gen_t *ng);
```

**Footer pattern** (lines 47-51):
```c
#ifdef __cplusplus
}
#endif

#endif /* SPU94_NOISE_H */
```

---

### `src/spu94/spu94_envelope_step.c` (utility, transform)

**Analog:** `src/spu94/spu94_adsr.c` (the code being extracted)

**Counter-accumulate math -- ATTACK phase** (lines 96-127):
```c
case ADSR_ATTACK: {
    /* CounterIncrement = 0x8000 >> max(0, shift - 11) */
    int shift_amt = adsr_max(0, (int)a->attack_shift - 11);
    counter_increment = (uint32_t)0x8000 >> shift_amt;

    /* AdsrStep = +(7 - attack_step) << max(0, 11 - shift) */
    int step_shift = adsr_max(0, 11 - (int)a->attack_shift);
    step = (int32_t)(7 - a->attack_step) << step_shift;

    /* ADSR-03: fake exponential above 0x6000 */
    if (a->attack_exp && level > 0x6000) {
        counter_increment >>= 2;  /* /= 4 */
    }

    /* Accumulate counter */
    a->counter += counter_increment;

    /* Check bit 15: if set, fire step and clear bit 15 */
    if (a->counter & 0x8000u) {
        a->counter &= ~0x8000u;
        level += step;
    }

    /* Clamp to [0, 0x7FFF] */
    if (level >= 0x7FFF) {
        level = 0x7FFF;
        /* [phase transition logic stays in ADSR, not in shared helper] */
    }
    if (level < 0) level = 0;
    break;
}
```

**Exponential decrease with anti-stall guard** (lines 143-149):
```c
/* Real exponential: step = step * level / 0x8000 (T-28-01) */
int32_t scaled_step = (step * level) / (int32_t)0x8000;
/* Ensure at least -1 step when level > 0 to prevent stalling */
if (scaled_step == 0 && level > 0) scaled_step = -1;
level += scaled_step;
```

**Sustain increase with fake exponential** (lines 174-181):
```c
if (a->sustain_dir == 0) {
    /* Increase */
    step = (int32_t)(7 - a->sustain_step) << step_shift;

    /* Fake exponential for increase above 0x6000 */
    if (a->sustain_exp && level > 0x6000) {
        counter_increment >>= 2;
    }
}
```

**Sustain decrease base-8 formula** (lines 183-184):
```c
/* Decrease: nocash spec uses base 8 for decrease formulas */
step = -((int32_t)(8 - a->sustain_step) << step_shift);
```

**The `adsr_max` helper** (lines 42-44):
```c
/* Helper: max(a, b) for int */
static inline int adsr_max(int a, int b) {
    return (a > b) ? a : b;
}
```

**Key insight for extraction:** The shared helper needs these inputs: `level`, `counter`, `shift`, `step_index`, `decrease`, `exponential`, `phase_negative`. It computes `counter_increment`, `base_step`, applies exponential scaling if needed, performs the counter-accumulate bit-15 check, and returns the updated `level` and `counter`. Phase transitions (ATTACK->DECAY, DECAY->SUSTAIN, etc.) remain in `spu94_adsr_tick()`, NOT in the shared helper.

---

### `include/spu94/spu94_sweep.h` (model, transform)

**Analog:** `include/spu94/spu94_noise.h`

Same header guard + doc comment + struct + function declaration + footer pattern as shown above for `spu94_envelope_step.h`. The sweep struct follows the noise struct pattern exactly: typedef struct with field-by-field doc comments, then init + tick function declarations.

---

### `src/spu94/spu94_sweep.c` (service, transform)

**Analog:** `src/spu94/spu94_noise.c`

**Include + init pattern** (lines 1-18):
```c
/* src/spu94/spu94_noise.c -- Phase 36 Plan 01
 *
 * [description and citations]
 */

#include <spu94/spu94_noise.h>
#include <string.h>

void spu94_noise_gen_init(spu94_noise_gen_t *ng) {
    if (ng == NULL) return;
    memset(ng, 0, sizeof(*ng));
    ng->level = 1;    /* seed = 1 (NOT 0 -- zero is absorbing) */
    ng->step  = 4;    /* minimum step */
}
```

**Tick function pattern** (lines 20-48):
```c
void spu94_noise_gen_tick(spu94_noise_gen_t *ng) {
    if (ng == NULL) return;

    /* [CITED: source]
     * [step-by-step algorithm implementation] */
    /* ... */
}
```

For sweep, the tick function will call `spu94_envelope_step()` instead of implementing its own counter-accumulate math. This is the architectural distinction -- noise has its own LFSR math, but sweep delegates to the shared envelope helper.

---

### `src/spu94/spu94_adsr.c` (MODIFIED -- refactor to call shared helper)

**Analog:** itself (current implementation at lines 82-256)

**Current per-phase inline math** will be replaced with calls to the shared helper. Each ADSR phase case currently does:
1. Compute `counter_increment` from shift
2. Compute `step` from step_index (base 7 for increase, base 8 for decrease)
3. Optional exponential scaling
4. Counter accumulate + bit-15 check
5. Clamp + phase transition

After refactor, steps 1-4 move into `spu94_envelope_step()`. Each case becomes:
```c
case ADSR_ATTACK: {
    spu94_envelope_state_t es = { .level = a->level, .counter = a->counter };
    int stepped = spu94_envelope_step(&es,
        a->attack_shift, a->attack_step,
        0 /* increase */, a->attack_exp ? 1 : 0 /* exponential */,
        0 /* phase_negative=0 for ADSR */);
    a->level = es.level;
    a->counter = es.counter;
    level = (int32_t)a->level;

    /* Phase transition logic (stays here, not in helper) */
    if (level >= 0x7FFF) {
        level = 0x7FFF;
        a->phase = ADSR_DECAY;
        a->counter = 0;
    }
    break;
}
```

**Golden-file regression gate:** `test_adsr.c` (all 12 existing tests) MUST pass bit-identically after refactor before any sweep code is written.

---

### `include/spu94/spu94_voice.h` (MODIFIED -- add sweep fields + API)

**Analog:** itself

**Add sweep fields to `spu94_voice_t`** -- follow the pattern of `spu94_adsr_state_t adsr` on line 57:
```c
spu94_adsr_state_t adsr;     /* Phase 28: per-voice ADSR envelope state */
```

New fields go after `adsr`:
```c
spu94_sweep_t sweep_l;       /* Phase 37: per-voice left volume sweep */
spu94_sweep_t sweep_r;       /* Phase 37: per-voice right volume sweep */
```

**Add mixer API for sweep** -- follow the `set_non` / `set_pmon` pattern (lines 174-175, 167-168):
```c
/* Set or clear the NON (noise on) bit for a given voice.
 * [doc comment]
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx,
    int enabled);
```

New sweep API follows this exact signature pattern but configures sweep parameters instead of a bitmask bit.

---

### `src/spu94/spu94_voice.c` (MODIFIED -- tick integration + sweep API)

**Analog:** itself

**Voice tick integration point** -- sweep tick goes at the TOP of `spu94_voice_tick()`, before STEP 1 (ADPCM decode). Lines 91-103 show the current tick entry:
```c
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      uint8_t gauss_bypass,
                      int16_t noise_level,
                      uint8_t non_enabled,
                      int16_t *out_l, int16_t *out_r) {
    if (v == NULL || out_l == NULL || out_r == NULL) return;

    if (v->active == 0) {
        *out_l = 0;
        *out_r = 0;
        return;
    }
```

New STEP 0 goes after the active check, before STEP 1:
```c
    /* ---------------------------------------------------------------
     * STEP 0 -- Sweep tick (SWEEP-05: updates vol_l/vol_r directly)
     * --------------------------------------------------------------- */
    if (v->sweep_l.active) {
        spu94_sweep_tick(&v->sweep_l);
        v->vol_l = v->sweep_l.level;
    }
    if (v->sweep_r.active) {
        spu94_sweep_tick(&v->sweep_r);
        v->vol_r = v->sweep_r.level;
    }
```

**Volume apply at STEP 3** (lines 264-265) -- unchanged, uses swept vol_l/vol_r:
```c
*out_l = q15_mul_truncate(gauss_out, v->vol_l);
*out_r = q15_mul_truncate(gauss_out, v->vol_r);
```

**KON handler** (lines 39-77) -- deactivate sweep on key_on, following pattern of ADSR reset at line 71:
```c
/* Phase 28: reset ADSR to attack (level=0, counter=0) */
spu94_adsr_key_on(&v->adsr);
```

Add after ADSR key_on:
```c
/* Phase 37/SWEEP-07: KON sets fixed volume, deactivates sweep */
v->sweep_l.active = 0;
v->sweep_r.active = 0;
```

**Mixer bitmask API pattern** -- `set_non` (lines 423-438) is the template for `set_sweep_l` / `set_sweep_r`:
```c
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    /* T-36-01: validate voice_idx 0..23 */
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    /* NON-04: set or clear non_flags bit. All 24 voices can be NON. */
    if (enabled) {
        m->non_flags |= (1u << voice_idx);
    } else {
        m->non_flags &= ~(1u << voice_idx);
    }

    return SPU94_OK;
}
```

Sweep API differs: instead of a bitmask toggle, it takes sweep configuration parameters and writes them to the voice's sweep_l/sweep_r struct directly. But validation + return pattern is identical.

---

### `tests/unit/voice/test_sweep.c` (test, batch)

**Analog:** `tests/unit/voice/test_noise_gen.c`

**File structure pattern** (lines 1-16, 207-216):
```c
/* tests/unit/voice/test_noise_gen.c
 * Phase 36 Plan 01: Unity unit tests for spu94_noise_gen (SPU global LFSR).
 *
 * Tests cover:
 *   [requirement ID list]
 */

#include "unity.h"
#include <spu94/spu94_noise.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}
```

**Individual test pattern** (lines 22-30):
```c
/* ---------------------------------------------------------------
 * NON-01: After init, level == 1 (seed), timer == 0, step == 4, shift == 0
 * --------------------------------------------------------------- */
void test_lfsr_seed_one(void) {
    spu94_noise_gen_t ng;
    spu94_noise_gen_init(&ng);

    TEST_ASSERT_EQUAL_INT16(1, ng.level);
    TEST_ASSERT_EQUAL_INT32(0, ng.timer);
    TEST_ASSERT_EQUAL_UINT8(0, ng.shift);
    TEST_ASSERT_EQUAL_UINT8(4, ng.step);
}
```

**Main runner pattern** (lines 207-216):
```c
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lfsr_seed_one);
    RUN_TEST(test_lfsr_deterministic_sequence);
    RUN_TEST(test_timer_decrement_and_reload);
    RUN_TEST(test_double_reload);
    RUN_TEST(test_frequency_varies_with_shift);
    RUN_TEST(test_parity_from_pre_shift_level);
    return UNITY_END();
}
```

**Test naming convention:** `test_<feature_under_test>` with requirement ID in the comment block.

**Test assertion style:** Uses `TEST_ASSERT_EQUAL_INT16`, `TEST_ASSERT_EQUAL_UINT8`, `TEST_ASSERT_EQUAL_HEX16_MESSAGE`, `TEST_ASSERT_TRUE_MESSAGE`. Message variants used when the assertion value alone is not self-documenting.

---

### `tests/unit/voice/CMakeLists.txt` (MODIFIED -- add test_sweep target)

**Analog:** itself

**Test target pattern** (lines 1-3):
```cmake
add_executable(test_voice_tick test_voice_tick.c)
target_link_libraries(test_voice_tick PRIVATE unity spu94_static)
add_test(NAME voice_tick_unit COMMAND test_voice_tick)
```

New entry follows same 3-line pattern:
```cmake
add_executable(test_sweep test_sweep.c)
target_link_libraries(test_sweep PRIVATE unity spu94_static)
add_test(NAME sweep_unit COMMAND test_sweep)
```

---

## Shared Patterns

### NULL Guard
**Source:** Every function in `spu94_adsr.c`, `spu94_noise.c`, `spu94_voice.c`
**Apply to:** All new functions (`spu94_envelope_step`, `spu94_sweep_init`, `spu94_sweep_tick`, mixer API functions)
```c
void spu94_noise_gen_init(spu94_noise_gen_t *ng) {
    if (ng == NULL) return;
    /* ... */
}
```

### Init via memset + explicit field overrides
**Source:** `spu94_noise.c` lines 13-18, `spu94_voice.c` lines 31-37
**Apply to:** `spu94_sweep_init`, `spu94_envelope_state` init
```c
void spu94_noise_gen_init(spu94_noise_gen_t *ng) {
    if (ng == NULL) return;
    memset(ng, 0, sizeof(*ng));
    ng->level = 1;    /* seed = 1 (NOT 0 -- zero is absorbing) */
    ng->step  = 4;    /* minimum step */
}
```

### Mixer API validation + return pattern
**Source:** `spu94_voice.c` lines 423-438 (`set_non`)
**Apply to:** All new mixer sweep API functions
```c
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;
    /* ... */
    return SPU94_OK;
}
```

### Include path convention
**Source:** `spu94_adsr.c` line 38, `spu94_noise.c` line 10
**Apply to:** All new .c files
```c
#include <spu94/spu94_adsr.h>
```
Uses angle-bracket `<spu94/...>` include style, NOT quotes.

### RT-safety comment convention
**Source:** `spu94_voice.h` lines 99, `spu94_adsr.h` lines 14
**Apply to:** All new headers and source files
```c
/* RT-safety: no malloc, no locks, no syscalls, no fopen/printf. */
```

### ADR document format
**Source:** `docs/DECISIONS.md` lines 1-30
**Apply to:** ADR-0059 (negative-phase sweep uncertainty)
```markdown
## ADR-0059: [title]

**Status:** Accepted
**Date:** 2026-05-22
**Phase:** 37 (Volume Sweep)
**Requirement:** SWEEP-09, SWEEP-10

**Context:**
[what ambiguity existed]

**Decision:**
[what SPU-94 does]

**Consequences:**
[tradeoffs, test obligations]

**Sources:**
[citations]
```

### Counter-accumulate mechanism (the core shared math)
**Source:** `spu94_adsr.c` lines 96-127 (attack), 129-166 (decay), 168-211 (sustain), 213-246 (release)
**Apply to:** `spu94_envelope_step.c` (extracted), `spu94_sweep.c` (consumer)

The common algorithm across all 4 ADSR phases and sweep:
```c
/* 1. CounterIncrement from shift */
int shift_amt = adsr_max(0, (int)shift - 11);
uint32_t counter_increment = (uint32_t)0x8000 >> shift_amt;

/* 2. BaseStep from step_index */
int step_shift = adsr_max(0, 11 - (int)shift);
int32_t step;
if (decrease) {
    step = -((int32_t)(8 - step_index) << step_shift);
} else {
    step = (int32_t)(7 - step_index) << step_shift;
}

/* 3. Fake exponential increase slowdown above 0x6000 */
if (exponential && !decrease && level > 0x6000) {
    counter_increment >>= 2;
}

/* 4. Counter accumulate + bit-15 check */
counter += counter_increment;
if (counter & 0x8000u) {
    counter &= ~0x8000u;

    /* 5. Exponential decrease: scale step by level */
    if (exponential && decrease) {
        int32_t scaled_step = (step * level) / (int32_t)0x8000;
        if (scaled_step == 0 && level > 0) scaled_step = -1;  /* anti-stall */
        level += scaled_step;
    } else {
        level += step;
    }
}

/* 6. Clamp (boundaries depend on caller: ADSR uses [0, 0x7FFF],
 *    sweep uses context-dependent boundaries based on phase_negative) */
```

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All files have exact analogs in the existing codebase |

Every new file maps directly to an existing pattern. The sweep module mirrors the noise module structure; the shared envelope helper is extracted verbatim from ADSR; the test file follows the noise test structure; the voice integration follows established STEP patterns.

## Metadata

**Analog search scope:** `include/spu94/`, `src/spu94/`, `tests/unit/voice/`, `docs/`
**Files scanned:** 14 source files read, 5 directory listings
**Pattern extraction date:** 2026-05-22
