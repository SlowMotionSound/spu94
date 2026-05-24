# Phase 36: Noise Generator (NON) - Pattern Map

**Mapped:** 2026-05-22
**Files analyzed:** 6 new/modified files
**Analogs found:** 6 / 6

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/spu94/spu94_noise.h` | model (header) | transform | `include/spu94/spu94_dac_noise.h` | exact |
| `src/spu94/spu94_noise.c` | model (impl) | transform | `src/spu94/spu94_dac_noise.c` | exact |
| `include/spu94/spu94_voice.h` | model (header, modified) | CRUD | self (add fields + API) | exact |
| `src/spu94/spu94_voice.c` | controller (impl, modified) | transform | self (PMON integration in mixer_tick) | exact |
| `tests/unit/voice/test_noise_gen.c` | test (new) | batch | `tests/unit/voice/test_voice_tick.c` | role-match |
| `docs/DECISIONS.md` | config (modified) | N/A | self (ADR-0057) | exact |

## Pattern Assignments

### `include/spu94/spu94_noise.h` (model header, transform)

**Analog:** `include/spu94/spu94_dac_noise.h`

**File structure pattern** (lines 1-66):
```c
/* include/spu94/spu94_dac_noise.h -- Phase 6 Plan 02, updated Phase 11
 *
 * Public API for the AK4309 delta-sigma noise model. [description...]
 *
 * WARNING: spu94_dac_noise_init() MUST be called before first use.
 * memset(state, 0, sizeof(*state)) is NOT equivalent -- it sets the
 * LFSR to zero, which is an absorbing state (output = silence forever).
 */
#ifndef SPU94_DAC_NOISE_H
#define SPU94_DAC_NOISE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t lfsr;      /* 32-bit Galois LFSR state */
    int16_t  x_prev;    /* x[n-1] for 2nd-order HP shaping */
    int16_t  x_prev2;   /* x[n-2] for 2nd-order HP shaping */
} spu94_dac_noise_state;

/* [function declarations with full docblock comments] */
int16_t spu94_dac_noise_step(spu94_dac_noise_state *state);
void spu94_dac_noise_init(spu94_dac_noise_state *state, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_DAC_NOISE_H */
```

**Key conventions to copy:**
- Include guard: `SPU94_NOISE_H` (matches `SPU94_DAC_NOISE_H`)
- Only `#include <stdint.h>` -- no project headers in the public API header
- `extern "C"` wrapper for C++ compatibility
- WARNING comment about init vs memset (same absorbing-state issue: seed=0 is absorbing)
- Struct typedef naming: `spu94_noise_gen_t` (follows `spu94_dac_noise_state` pattern but uses `_t` suffix per RESEARCH.md)
- Function naming: `spu94_noise_gen_init`, `spu94_noise_gen_tick` (matches `spu94_dac_noise_init`, `spu94_dac_noise_step`)

---

### `src/spu94/spu94_noise.c` (model implementation, transform)

**Analog:** `src/spu94/spu94_dac_noise.c`

**Imports pattern** (lines 30-33):
```c
#include <spu94/spu94_dac_noise.h>
#include <spu94/spu94_q15.h>
#include <string.h>
```

New file should follow:
```c
#include <spu94/spu94_noise.h>
#include <string.h>
```
Note: `spu94_q15.h` is NOT needed for the noise generator -- the LFSR output wraps naturally at 16 bits via `uint16_t` cast. No saturation arithmetic is required.

**Init pattern** (lines 60-63):
```c
void spu94_dac_noise_init(spu94_dac_noise_state *state, uint32_t seed) {
    memset(state, 0, sizeof(*state));
    state->lfsr = seed ? seed : DAC_NOISE_LFSR_SEED;
}
```

New file should follow same memset-then-set-nonzero pattern:
```c
void spu94_noise_gen_init(spu94_noise_gen_t *ng) {
    memset(ng, 0, sizeof(*ng));
    ng->level = 1;    // seed = 1 (NOT 0)
    ng->step  = 4;    // minimum step
}
```

**Tick/step function pattern** (lines 65-87):
```c
int16_t spu94_dac_noise_step(spu94_dac_noise_state *state) {
    /* Step the Galois LFSR */
    uint32_t lfsr = state->lfsr;
    uint32_t bit = lfsr & 1u;
    lfsr >>= 1;
    if (bit) lfsr ^= DAC_NOISE_LFSR_FEEDBACK;
    state->lfsr = lfsr;
    // ... shaping ...
    return sat_s16(y);
}
```

Key pattern: read state into local, operate on local, write back to struct. The noise generator tick should follow the same local-variable convention.

**File-scope defines pattern** (lines 34-58):
```c
#define DAC_NOISE_LFSR_FEEDBACK  0x80200003u
#define DAC_NOISE_LFSR_SEED      0xACE1u
#define DAC_NOISE_SHIFT  14
```

No file-scope defines are needed for the SPU noise generator -- all magic numbers (0x20000, tap positions) should be inline in the tick function with nocash pseudocode line references as comments.

---

### `include/spu94/spu94_voice.h` (model header, modified -- add fields + API)

**Analog:** self -- PMON integration from Phase 35

**Mixer struct field additions** (lines 119-131, showing where to add):
```c
typedef struct {
    spu94_voice_t voices[24];
    uint8_t       voice_ram[SPU94_SPU_RAM_BYTES];
    spu94_voice_t pending_config[24];
    uint32_t      pending_kon;
    uint32_t      pending_koff;
    uint32_t      eon_flags;
    uint32_t      pmon_flags;          /* PMON-01 -- added Phase 35 */
    // NEW: add non_flags and noise_gen here, following pmon_flags
    int16_t       master_vol_l;
    int16_t       master_vol_r;
    uint8_t       enabled;
    uint8_t       gauss_bypass;
} spu94_voice_mixer_t;
```

Add these fields after `pmon_flags` (line 126):
- `uint32_t non_flags;` -- NON-04: bit N set = voice N outputs noise
- `spu94_noise_gen_t noise_gen;` -- NON-08: single global noise generator

The header must also `#include <spu94/spu94_noise.h>` alongside the existing `#include <spu94/spu94_adsr.h>` (line 22).

**Bitmask API pattern** -- copy `spu94_voice_mixer_set_pmon` declaration (lines 160-161):
```c
spu94_result_t spu94_voice_mixer_set_pmon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled);
```

New `set_non` follows the same signature pattern. Also add `set_noise_freq`:
```c
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx,
    int enabled);
spu94_result_t spu94_voice_mixer_set_noise_freq(spu94_voice_mixer_t *m,
    uint8_t shift, uint8_t step_raw);
```

**voice_tick signature change** (lines 97-100):
```c
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      uint8_t gauss_bypass,
                      int16_t *out_l, int16_t *out_r);
```

Add two new parameters after `gauss_bypass`:
```c
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      uint8_t gauss_bypass,
                      int16_t noise_level,    /* NON: global noise value */
                      uint8_t non_enabled,     /* NON: 1 = use noise */
                      int16_t *out_l, int16_t *out_r);
```

---

### `src/spu94/spu94_voice.c` (controller, modified -- NON branch + mixer integration)

**Analog:** self -- PMON integration in `spu94_voice_mixer_tick` (lines 482-527)

**Bitmask API implementation** -- copy `spu94_voice_mixer_set_pmon` (lines 391-407):
```c
spu94_result_t spu94_voice_mixer_set_pmon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    if (enabled) {
        m->pmon_flags |= (1u << voice_idx);
    } else {
        m->pmon_flags &= ~(1u << voice_idx);
    }

    return SPU94_OK;
}
```

New `set_non` follows identical pattern with `m->non_flags`. `set_noise_freq` validates shift (0-15) and step_raw (0-3), then sets `m->noise_gen.shift` and `m->noise_gen.step = step_raw + 4`.

**Mixer tick integration point** -- noise tick + NON per-voice routing (lines 482-527):
```c
    /* S1 / MIX-01: accumulate 24 voices in int32 to prevent overflow */
    int32_t dry_sum_l = 0, dry_sum_r = 0;
    int32_t rev_sum_l = 0, rev_sum_r = 0;
    for (int v = 0; v < 24; v++) {
        if (!m->voices[v].active) continue;

        /* PMON pitch modulation ... */
        uint16_t saved_pitch = m->voices[v].pitch;
        if ((m->pmon_flags & (1u << v)) && v > 0) {
            // ... PMON code ...
        }

        int16_t vl = 0, vr = 0;
        spu94_voice_tick(&m->voices[v],
                         m->voice_ram, SPU94_SPU_RAM_BYTES,
                         m->gauss_bypass,
                         &vl, &vr);
        // ...
    }
```

Insert noise tick BEFORE the voice loop (after pending KON/KOFF, before accumulator init):
```c
    // NON-08: tick noise generator ONCE before voice loop
    spu94_noise_gen_tick(&m->noise_gen);
```

Modify the `spu94_voice_tick` call inside the loop to pass NON parameters:
```c
        uint8_t non_enabled = (m->non_flags & (1u << v)) ? 1 : 0;

        spu94_voice_tick(&m->voices[v],
                         m->voice_ram, SPU94_SPU_RAM_BYTES,
                         m->gauss_bypass,
                         m->noise_gen.level,  // NON-05: same for all voices
                         non_enabled,
                         &vl, &vr);
```

**Voice tick NON branch point** -- insert after STEP 1 (ADPCM decode, line 196), before STEP 2 (Gaussian interpolation, line 204):
```c
    /* STEP 2 — Gaussian interpolation OR noise substitution */
    {
        int16_t gauss_out;
        if (non_enabled) {
            /* NON-04/NON-05: substitute global noise level for Gauss output.
             * ADPCM decode above still ran -- loop flags, ENDX are side effects. */
            gauss_out = noise_level;
        } else {
            /* Existing Gaussian interpolation code (unchanged) */
            const uint8_t gi = (uint8_t)((v->pitch_counter >> 4) & 0xFF);
            // ... existing gauss code ...
        }
        // Steps 2.5 onward are UNCHANGED
    }
```

**Mixer init** -- add `noise_gen` initialization (after line 316 memset):
```c
    spu94_noise_gen_init(&m->noise_gen);
```

---

### `tests/unit/voice/test_noise_gen.c` (test, new file)

**Analog:** `tests/unit/voice/test_voice_tick.c` -- PMON test section (lines 1269-1318)

**Test file structure** (lines 1-24, 1853-1911):
```c
/* tests/unit/voice/test_voice_tick.c
 * Phase 27: Unity unit tests for spu94_voice_tick.
 */

#include "unity.h"
#include <spu94/spu94_voice.h>
#include <spu94/spu94_adsr.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>
#include <spu94/spu94_q15.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// ... test functions ...

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_some_function);
    // ...
    return UNITY_END();
}
```

New `test_noise_gen.c` should include:
```c
#include "unity.h"
#include <spu94/spu94_noise.h>
#include <stdint.h>
```

**Test helper pattern** -- from PMON (lines 1281-1290):
```c
static void make_loud_sample(uint8_t *ram, uint32_t num_blocks) {
    for (uint32_t b = 0; b < num_blocks; b++) {
        memset(ram + b * 16, 0, 16);
        ram[b * 16 + 0] = 0x00;
        ram[b * 16 + 1] = 0x00;
        for (int j = 2; j < 16; j++) {
            ram[b * 16 + j] = 0x77;
        }
    }
}
```

**Mixer test setup pattern** -- from PMON (lines 1296-1300):
```c
void test_pmon_silent_modulator_halves_pitch(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    // ...
}
```

Note: a file-scope `static spu94_voice_mixer_t s_test_mixer;` is used in test_voice_tick.c (search shows it at line ~760 area). The new test file for standalone noise_gen unit tests does NOT need the mixer -- it directly operates on `spu94_noise_gen_t`. The NON integration tests that go into `test_voice_tick.c` DO use the mixer pattern.

**CMakeLists.txt registration** -- from `tests/unit/voice/CMakeLists.txt` (lines 1-3):
```cmake
add_executable(test_voice_tick test_voice_tick.c)
target_link_libraries(test_voice_tick PRIVATE unity spu94_static)
add_test(NAME voice_tick_unit COMMAND test_voice_tick)
```

New entry:
```cmake
add_executable(test_noise_gen test_noise_gen.c)
target_link_libraries(test_noise_gen PRIVATE unity spu94_static)
add_test(NAME noise_unit COMMAND test_noise_gen)
```

---

### `docs/DECISIONS.md` (config, modified -- ADR-0058)

**Analog:** self -- ADR-0057 (lines 33-113)

**ADR entry format:**
```markdown
## ADR-0058: [title]

**Status:** Accepted
**Date:** 2026-05-22
**Phase:** 36 (Noise Generator NON)
**Requirement:** NON-01, NON-06, NON-09

**Context:**
[paragraph describing the ambiguity]

**Decision:**
[what SPU-94 does]

**Consequences:**
- [bullet points of tradeoffs]

**Sources:**
- [citation list]

---
```

Prepend before ADR-0057 (at line 33), per the discipline note: "New entries are prepended at the top of this file."

---

## Shared Patterns

### Bitmask Flag API
**Source:** `src/spu94/spu94_voice.c` lines 391-407 (`set_pmon`)
**Apply to:** `set_non` in spu94_voice.c

Pattern: validate `(m == NULL || voice_idx < 0 || voice_idx >= 24)`, return `SPU94_INVALID_ARG`. Then set/clear bit via `|=` / `&= ~`. Return `SPU94_OK`.

```c
spu94_result_t spu94_voice_mixer_set_pmon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;
    if (enabled) {
        m->pmon_flags |= (1u << voice_idx);
    } else {
        m->pmon_flags &= ~(1u << voice_idx);
    }
    return SPU94_OK;
}
```

### Per-Voice Flag Check in Mixer Tick
**Source:** `src/spu94/spu94_voice.c` lines 501-508 (PMON flag check)
**Apply to:** NON flag check in mixer_tick voice loop

```c
        if ((m->pmon_flags & (1u << v)) && v > 0) {
            // ... per-voice conditional logic ...
        }
```

NON equivalent (no `v > 0` guard needed -- all 24 voices can be NON):
```c
        uint8_t non_enabled = (m->non_flags & (1u << v)) ? 1 : 0;
```

### LFSR Init (non-zero seed required)
**Source:** `include/spu94/spu94_dac_noise.h` lines 18-20 (WARNING comment)
**Apply to:** `spu94_noise.h` header comment

```c
/* WARNING: spu94_dac_noise_init() MUST be called before first use.
 * memset(state, 0, sizeof(*state)) is NOT equivalent -- it sets the
 * LFSR to zero, which is an absorbing state (output = silence forever). */
```

Same warning applies to `spu94_noise_gen_init` -- seed=0 is absorbing for the SPU noise LFSR too.

### Error Return Convention
**Source:** `src/spu94/spu94_voice.c` lines 325-356 (mixer_key_on)
**Apply to:** All new API functions in spu94_voice.c

```c
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;
    // ... logic ...
    return SPU94_OK;
```

### Include Path Convention
**Source:** `src/spu94/spu94_dac_noise.c` line 30
**Apply to:** `spu94_noise.c`

```c
#include <spu94/spu94_noise.h>
```

Angle brackets with `spu94/` prefix (not quotes, not relative paths). This is the project convention for all `include/spu94/` headers.

### Unity Test Convention
**Source:** `tests/unit/voice/test_voice_tick.c` lines 23-24
**Apply to:** `test_noise_gen.c`

```c
void setUp(void) {}
void tearDown(void) {}
```

Empty setUp/tearDown -- each test function initializes its own state. No shared fixtures.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All files have strong analogs in the existing codebase |

## Metadata

**Analog search scope:** `include/spu94/`, `src/spu94/`, `tests/unit/voice/`, `docs/`
**Files scanned:** 8 analog files read (spu94_dac_noise.h, spu94_dac_noise.c, spu94_voice.h, spu94_voice.c, test_voice_tick.c, CMakeLists.txt, DECISIONS.md, spu94_noise.h check)
**Pattern extraction date:** 2026-05-22
