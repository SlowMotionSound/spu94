# Phase 20: Macro Engine + Safety Core - Pattern Map

**Mapped:** 2026-05-03
**Files analyzed:** 12 new/modified files
**Analogs found:** 12 / 12

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/spu94/spu94_macro.h` | config (public header) | request-response | `include/spu94/spu94.h` (tempo section) | exact |
| `src/spu94/spu94_macro.c` | service (engine) | transform | `src/spu94/spu94_tempo.c` | exact |
| `src/spu94/spu94_safety.c` | middleware (guard) | request-response | `src/spu94/spu94_register_io.c` | exact |
| `src/spu94/spu94_state_internal.h` | model (state struct) | CRUD | self (append fields) | exact |
| `include/spu94/spu94.h` | config (public header) | request-response | self (append result codes + API) | exact |
| `src/spu94/CMakeLists.txt` | config (build) | N/A | self (append source files) | exact |
| `tests/unit/CMakeLists.txt` | config (build) | N/A | self (append subdirectory) | exact |
| `tests/unit/macro/CMakeLists.txt` | config (build) | N/A | `tests/unit/tempo/CMakeLists.txt` | exact |
| `tests/unit/macro/test_macro_group.c` | test | request-response | `tests/unit/tempo/test_tempo_basic.c` | exact |
| `tests/unit/macro/test_macro_gang.c` | test | request-response | `tests/unit/tempo/test_tempo_basic.c` | exact |
| `tests/unit/macro/test_macro_derive.c` | test | request-response | `tests/unit/tempo/test_tempo_basic.c` | exact |
| `tests/unit/macro/test_macro_range.c` | test | request-response | `tests/unit/tempo/test_tempo_basic.c` | exact |
| `tests/unit/safety/CMakeLists.txt` | config (build) | N/A | `tests/unit/tempo/CMakeLists.txt` | exact |
| `tests/unit/safety/test_safety_stability.c` | test | request-response | `tests/unit/tempo/test_tempo_basic.c` | exact |
| `tests/unit/safety/test_safety_bounds.c` | test | request-response | `tests/unit/tempo/test_tempo_basic.c` | exact |

## Pattern Assignments

### `include/spu94/spu94_macro.h` (public header)

**Analog:** `include/spu94/spu94.h` (tempo section, lines 526-644)

**Header guard and extern-C pattern** (`include/spu94/spu94.h` lines 1-6):
```c
#ifndef SPU94_H
#define SPU94_H

#ifdef __cplusplus
extern "C" {
#endif
```

**Enum definition pattern** (lines 527-544, 546-558):
```c
typedef enum {
    SPU94_SUB_1_1          = 0,
    SPU94_SUB_1_1_DOTTED   = 1,
    /* ... */
    SPU94_SUBDIVISION__COUNT = 15
} spu94_subdivision_t;

typedef enum {
    SPU94_TEMPO_REG_dAPF1  = 0,
    /* ... */
    SPU94_TEMPO_REG__COUNT = 10
} spu94_tempo_reg_t;
```

**Public function declaration pattern** (lines 569-598):
```c
/** Store BPM in engine state. Valid range: 1-65535.
 *  BPM=0 returns SPU94_INVALID_ARG. NULL state returns SPU94_INVALID_STATE.
 *  On BPM change, all grid-bound registers in active sync groups are resnapped. */
spu94_result_t spu94_set_tempo(spu94_state *state, uint16_t bpm);

/** Retrieve current BPM. Returns 0 if no tempo set or state is NULL. */
uint16_t       spu94_get_tempo(const spu94_state *state);
```

**Conventions to follow:**
- `spu94_macro.h` is a NEW public header (like `spu94_registers.h`, `spu94_adpcm.h`)
- Include only `<stdint.h>` and `<stddef.h>` from freestanding headers (C99 conformance)
- Forward-declare `spu94_state` or include `<spu94/spu94_registers.h>` (which typedefs it)
- Append-only enums with `__COUNT` sentinel
- Doxygen-style `/** */` comments on every public function
- The umbrella `spu94.h` should `#include <spu94/spu94_macro.h>` after the result-code enum

---

### `src/spu94/spu94_macro.c` (macro engine implementation)

**Analog:** `src/spu94/spu94_tempo.c`

**Imports pattern** (lines 1-27):
```c
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
```

**File header comment pattern** (lines 1-18):
```c
/* src/spu94/spu94_tempo.c -- Phase 16 Plans 01 + 02
 *
 * Tempo-synced delay taps: subdivision table, BPM state management,
 * validity checking, binding state tracking, register writes, auto-resnap,
 * and write-interception for binding state transitions. Integer-only
 * computation at 22,050 Hz internal rate with truncation matching
 * PS1 MIPS R3000A integer division (tight/forward character).
 *
 * TEMPO-01: spu94_set_tempo / spu94_get_tempo
 * TEMPO-02: spu94_set_subdivision (full -- writes to hardware registers)
 * ...
 */
```

**Compile-time constant table pattern** (lines 38-59):
```c
typedef struct {
    uint8_t numerator;
    uint8_t denominator;
} spu94_subdivision_ratio_t;

static const spu94_subdivision_ratio_t spu94_subdivision_table[SPU94_SUBDIVISION__COUNT] = {
    [SPU94_SUB_1_1]          = {1,  1},   /* whole note */
    /* ... */
};
```

**Re-entrancy guard pattern** (lines 62-73, used at lines 169-222, 280-310):
```c
/* Re-entrancy guard: when the engine writes to registers via
 * spu94_set_reg_u16, the hook should not trigger its own transitions. */
state->tempo_writing = 1;
/* ... register writes ... */
state->tempo_writing = 0;
```

**Pending-aware value reader pattern** (lines 145-150):
```c
static uint16_t get_latest_u16(const spu94_state *state, spu94_reg_t reg)
{
    if (state->pending_mask & (UINT64_C(1) << reg))
        return (uint16_t)state->pending_values[reg];
    return (uint16_t)state->reg_values[reg];
}
```

**Public function with NULL-guard and enum-range-check pattern** (lines 156-164, 261-276):
```c
spu94_result_t spu94_set_tempo(spu94_state *state, uint16_t bpm)
{
    if (state == NULL) return SPU94_INVALID_STATE;
    if (bpm == 0) return SPU94_INVALID_ARG;
    /* ... */
    return SPU94_OK;
}

spu94_result_t spu94_set_subdivision(spu94_state *state,
                                     spu94_tempo_reg_t reg,
                                     spu94_subdivision_t subdivision)
{
    if (state == NULL) return SPU94_INVALID_STATE;
    if (state->tempo_bpm == 0) return SPU94_INVALID_ARG;
    if ((int)reg < 0 || (int)reg >= SPU94_TEMPO_REG__COUNT) return SPU94_INVALID_ARG;
    if ((int)subdivision < 0 || (int)subdivision >= SPU94_SUBDIVISION__COUNT)
        return SPU94_INVALID_ARG;
    /* ... */
}
```

**Write-interception hook pattern** (lines 383-396):
```c
/* Prototype declared here so -Wmissing-prototypes is satisfied (the extern
 * declaration lives in spu94_register_io.c for the cross-TU call). */
void spu94_tempo_on_reg_write(spu94_state *state, spu94_reg_t reg);

void spu94_tempo_on_reg_write(spu94_state *state, spu94_reg_t reg)
{
    if (state->tempo_writing) return;  /* our own write -- don't transition */
    int idx = hw_reg_to_tempo_idx(reg);
    if (idx < 0) return;  /* not a tempo-tracked register */
    if (state->tempo_bind_state[idx] == SPU94_BIND_GRID) {
        state->tempo_bind_state[idx] = SPU94_BIND_PROPORTIONAL;
        state->tempo_bind_ref_bpm[idx] = state->tempo_bpm;
    }
}
```

**Reverse lookup (hardware reg to subsystem index) pattern** (lines 99-110):
```c
static int hw_reg_to_tempo_idx(spu94_reg_t reg)
{
    switch (reg) {
        case SPU94_REG_dAPF1:  return 0;
        case SPU94_REG_dAPF2:  return 1;
        /* ... */
        default: return -1;
    }
}
```

---

### `src/spu94/spu94_safety.c` (safety enforcement layer)

**Analog:** `src/spu94/spu94_register_io.c`

**Imports pattern** (lines 1-30):
```c
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
```

**Cross-TU extern declaration pattern** (lines 80-91):
```c
/* Defined in spu94_write_policy.c (Plan 03 Task 2). */
extern const spu94_write_policy_t spu94_write_policy_table[SPU94_REG__COUNT];

/* Defined in spu94_tempo.c (Phase 16 Plan 02).
 * Called from spu94_set_reg_u16 for d-prefix registers to transition
 * grid-bound registers to proportional state on manual write (D-06). */
void spu94_tempo_on_reg_write(struct spu94_state *state, spu94_reg_t reg);
```

**Signedness classification via packed bitmask pattern** (lines 40-74):
```c
static const uint64_t spu94_reg_is_u16_mask =
    (UINT64_C(1) << SPU94_REG_mBASE)   |
    (UINT64_C(1) << SPU94_REG_dAPF1)   |
    /* ... */
    (UINT64_C(1) << SPU94_REG_mRAPF2);

spu94_reg_type_t spu94_reg_type(spu94_reg_t reg) {
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return SPU94_REG_TYPE_I16;
    }
    return (spu94_reg_is_u16_mask & (UINT64_C(1) << reg))
        ? SPU94_REG_TYPE_U16
        : SPU94_REG_TYPE_I16;
}
```

**Typed setter body with NULL guard and policy routing** (lines 97-155):
```c
spu94_result_t spu94_set_reg_i16(spu94_state *state, spu94_reg_t reg, int16_t value) {
    if (state == (spu94_state *)0) {
        return SPU94_INVALID_STATE;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return SPU94_UNKNOWN_REG;
    }
    if (spu94_reg_type(reg) != SPU94_REG_TYPE_I16) {
        return SPU94_TYPE_MISMATCH;
    }
    /* ... write logic ... */
    return SPU94_OK;
}
```

**INT16_MIN-safe absolute value pattern** (`src/spu94/spu94_reverb.c` lines 155-156):
```c
/* Widen to int32/int64 BEFORE negating to avoid INT16_MIN UB */
int64_t l_abs = (Lin_wide < 0) ? -(int64_t)Lin_wide : (int64_t)Lin_wide;
```

The safety layer should use this project-standard widening approach:
```c
static inline int32_t abs_i32_safe(int16_t v) {
    return (v < 0) ? -(int32_t)v : (int32_t)v;
}
```

---

### `src/spu94/spu94_state_internal.h` (state struct extension)

**Analog:** Self -- append new fields following the existing section-comment pattern.

**Section comment pattern** (lines 198-215):
```c
    /* -----------------------------------------------------------------
     * Phase 16 (Tempo-Synced Taps, TEMPO-01..04): BPM state, per-register
     * binding tracking, and sync group toggles.
     *
     * 10 tempo-tracked registers: 6 d-prefix (reflections) + 4 virtual
     * comb delays. Each stores binding state (fixed/grid/proportional),
     * subdivision index, and reference BPM for proportional scaling.
     * Two independent sync group toggles control auto-resnap on BPM change.
     * ----------------------------------------------------------------- */
    uint16_t       tempo_bpm;             /* current BPM, 0 = unset */
    uint8_t        tempo_writing;         /* re-entrancy guard for tempo snap writes */
    uint8_t        reflection_sync;       /* 1 = d-prefix resnaps on BPM change */
    uint8_t        comb_sync;             /* 1 = virtual combs resnap on BPM change */

    /* Per-register binding state (indexed by spu94_tempo_reg_t) */
    uint8_t        tempo_bind_state[SPU94_TEMPO_REG__COUNT];
    uint8_t        tempo_bind_sub[SPU94_TEMPO_REG__COUNT];
    uint16_t       tempo_bind_ref_bpm[SPU94_TEMPO_REG__COUNT];
```

**Conventions:**
- New fields go BEFORE the `oob_tap_count` tail sentinel (or at the tail per D-17 convention)
- Block comments describe phase number, requirement IDs, and purpose
- Per-field inline comments describe range and semantics
- `_Static_assert` at bottom (line 240-241) automatically validates the budget

---

### `include/spu94/spu94.h` (result code and API additions)

**Analog:** Self -- append to the `spu94_result_t` enum and add new API sections.

**Result code append pattern** (lines 53-63):
```c
typedef enum {
    SPU94_OK                 = 0,
    SPU94_CLAMPED            = 1,
    SPU94_UNKNOWN_REG        = 2,
    SPU94_TYPE_MISMATCH      = 3,
    /* Appended 2026-04-24 (ADR-0022): mutation-time argument validation. */
    SPU94_INVALID_STATE      = 4,
    SPU94_WORK_BUF_TOO_SMALL = 5,
    SPU94_INVALID_ARG        = 6
} spu94_result_t;
```

New codes (e.g., `SPU94_STABILITY_CLAMPED = 7`, `SPU94_ADDRESS_CLAMPED = 8`) append with a comment noting the phase and date.

**API section pattern** (lines 519-644 -- tempo section):
```c
/* -----------------------------------------------------------------------
 * Tempo-synced delay taps (Phase 16, TEMPO-01..04)
 *
 * [multi-line description of capability]
 * ----------------------------------------------------------------------- */
```

The new macro engine API section follows this same block-comment + declaration style. Alternatively, `#include <spu94/spu94_macro.h>` keeps the umbrella clean.

---

### `src/spu94/CMakeLists.txt` (library build -- source list addition)

**Analog:** Self -- append source files to the `spu94_obj` OBJECT library.

**Pattern** (lines 5-27):
```cmake
add_library(spu94_obj OBJECT
    spu94_state.c
    spu94_registers.c
    spu94_register_io.c
    # ... existing files ...
    spu94_tempo.c
)
```

Append `spu94_macro.c` and `spu94_safety.c` to this list.

---

### `tests/unit/CMakeLists.txt` (test root -- subdirectory addition)

**Analog:** Self -- append `add_subdirectory()` calls.

**Pattern** (lines 13-26):
```cmake
add_subdirectory(q15)
add_subdirectory(state)
# ... existing dirs ...
add_subdirectory(tempo)
```

Append `add_subdirectory(macro)` and `add_subdirectory(safety)`.

---

### `tests/unit/macro/CMakeLists.txt` (test build)

**Analog:** `tests/unit/tempo/CMakeLists.txt`

**Full pattern** (lines 1-20):
```cmake
add_executable(test_tempo_basic test_tempo_basic.c)
target_link_libraries(test_tempo_basic PRIVATE unity spu94_static)
add_test(NAME test_tempo_basic COMMAND test_tempo_basic)

add_executable(test_tempo_snap test_tempo_snap.c)
target_link_libraries(test_tempo_snap PRIVATE unity spu94_static)
add_test(NAME test_tempo_snap COMMAND test_tempo_snap)
```

Repeat for each `test_macro_*.c` and `test_safety_*.c` file:
- `add_executable(<test_name> <source_file>)`
- `target_link_libraries(<test_name> PRIVATE unity spu94_static)`
- `add_test(NAME <test_name> COMMAND <test_name>)`

---

### `tests/unit/macro/test_macro_group.c` (and all test files)

**Analog:** `tests/unit/tempo/test_tempo_basic.c`

**Imports pattern** (lines 1-12):
```c
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
```

**Coverage map comment pattern** (lines 13-23):
```c
/* COVERAGE MAP -- TEMPO-01
 *   bpm set/get roundtrip at 120   : test_set_get_tempo_120
 *   bpm set/get roundtrip at 1     : test_set_get_tempo_min
 *   ...
 */
```

**Setup/teardown stubs and global state allocation** (lines 25-33):
```c
void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[SPU94_WORK_BUF_MAX_BYTES];

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}
```

**Test function pattern** (lines 41-46):
```c
void test_set_get_tempo_120(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_tempo(s, 120));
    TEST_ASSERT_EQUAL_UINT16(120, spu94_get_tempo(s));
}
```

**Main function pattern** (lines 94-106):
```c
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_tempo_zero);
    RUN_TEST(test_set_get_tempo_120);
    /* ... */
    return UNITY_END();
}
```

**Assertion conventions:**
- `TEST_ASSERT_EQUAL_INT(expected, actual)` for result codes
- `TEST_ASSERT_EQUAL_UINT16(expected, actual)` for register values
- `TEST_ASSERT_EQUAL_INT16(expected, actual)` for signed register values
- `TEST_ASSERT_NOT_NULL(ptr)` for state initialization
- `TEST_ASSERT_NULL(ptr)` for expected-NULL returns

---

## Shared Patterns

### NULL Guard
**Source:** `src/spu94/spu94_register_io.c` lines 98-100, `src/spu94/spu94_tempo.c` lines 157-158
**Apply to:** All public functions in `spu94_macro.c` and `spu94_safety.c`
```c
if (state == NULL) return SPU94_INVALID_STATE;
```
For getters that return a value (not `spu94_result_t`), return 0 or a safe default on NULL.

### Re-entrancy Guard
**Source:** `src/spu94/spu94_tempo.c` lines 169-222
**Apply to:** `spu94_macro.c` -- macro engine register writes must not trigger macro re-derivation hooks
```c
state->macro_writing = 1;
/* ... spu94_set_reg_{i16,u16} calls ... */
state->macro_writing = 0;
```
Field `macro_writing` (uint8_t) added to `struct spu94_state`, following `tempo_writing` pattern.

### Cross-TU Hook Declaration
**Source:** `src/spu94/spu94_register_io.c` lines 88-91
**Apply to:** If `spu94_register_io.c` needs to call a macro/safety hook, declare with extern prototype:
```c
/* Defined in spu94_safety.c (Phase 20).
 * Called from spu94_set_reg_* for safety enforcement on manual writes. */
void spu94_safety_on_reg_write(struct spu94_state *state, spu94_reg_t reg, int16_t value);
```
Note: If using the wrapper-function approach (recommended in RESEARCH.md Option A), no cross-TU hook is needed -- callers use `spu94_safe_set_reg_*` directly instead of modifying `spu94_register_io.c`.

### Pending-Aware Value Read
**Source:** `src/spu94/spu94_tempo.c` lines 145-150
**Apply to:** `spu94_safety.c` for m-prefix address bounds checking (TICK_LATCHED registers)
```c
static uint16_t get_latest_u16(const spu94_state *state, spu94_reg_t reg)
{
    if (state->pending_mask & (UINT64_C(1) << reg))
        return (uint16_t)state->pending_values[reg];
    return (uint16_t)state->reg_values[reg];
}
```

### INT16_MIN-Safe Absolute Value
**Source:** `src/spu94/spu94_reverb.c` lines 155-156
**Apply to:** `spu94_safety.c` stability product computation
```c
static inline int32_t abs_i32_safe(int16_t v) {
    return (v < 0) ? -(int32_t)v : (int32_t)v;
}
```

### Test File Scaffold
**Source:** `tests/unit/tempo/test_tempo_basic.c` lines 1-33, 94-106
**Apply to:** All 6 new test files
```c
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[SPU94_WORK_BUF_MAX_BYTES];

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}
/* ... test functions ... */
int main(void) {
    UNITY_BEGIN();
    /* RUN_TEST calls */
    return UNITY_END();
}
```

### Test CMakeLists Pattern
**Source:** `tests/unit/tempo/CMakeLists.txt` lines 1-3
**Apply to:** `tests/unit/macro/CMakeLists.txt` and `tests/unit/safety/CMakeLists.txt`
```cmake
add_executable(test_macro_group test_macro_group.c)
target_link_libraries(test_macro_group PRIVATE unity spu94_static)
add_test(NAME test_macro_group COMMAND test_macro_group)
```

### Result Code Append
**Source:** `include/spu94/spu94.h` lines 53-63
**Apply to:** Appending `SPU94_STABILITY_CLAMPED` and `SPU94_ADDRESS_CLAMPED`
```c
    SPU94_INVALID_ARG        = 6, /* argument out of range (e.g., preset id)   */
    /* Appended 2026-05-03 (Phase 20): safety enforcement result codes. */
    SPU94_STABILITY_CLAMPED  = 7, /* vIIR x vWALL product was clamped to limit */
    SPU94_ADDRESS_CLAMPED    = 8  /* m-prefix address was clamped to buffer sz  */
```

---

## No Analog Found

No files in this phase lack a close analog. The macro engine follows the tempo system pattern precisely (per-group state, re-entrancy guard, register coordination). The safety layer follows the register I/O pattern (typed wrappers, NULL guards, result codes). All test files follow the Unity test scaffold pattern.

---

## Metadata

**Analog search scope:** `src/spu94/`, `include/spu94/`, `tests/unit/`
**Files scanned:** 22 source files, 8 headers, 14 test CMakeLists
**Pattern extraction date:** 2026-05-03
