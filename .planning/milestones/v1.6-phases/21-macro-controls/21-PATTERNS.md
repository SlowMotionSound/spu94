# Phase 21: Macro Controls - Pattern Map

**Mapped:** 2026-05-04
**Files analyzed:** 9 new/modified files
**Analogs found:** 9 / 9

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/spu94/spu94_macro.h` | header | N/A (type defs) | self (Phase 20 version) | exact |
| `src/spu94/spu94_macro.c` | service | transform | self (Phase 20 version) | exact |
| `src/spu94/spu94_macro_controls.c` | service | CRUD + transform | `src/spu94/spu94_presets.c` (static const tables) + `src/spu94/spu94_macro.c` (engine pattern) | role-match |
| `src/spu94/spu94_state_internal.h` | model | N/A (struct def) | self (Phase 20 version) | exact |
| `tests/unit/macro/test_macro_spread_sweep.c` | test | request-response | `tests/unit/macro/test_macro_group.c` | exact |
| `tests/unit/macro/test_macro_bipolar.c` | test | request-response | `tests/unit/macro/test_macro_gang.c` | exact |
| `tests/unit/macro/test_macro_controls.c` | test | request-response | `tests/unit/macro/test_macro_derive.c` | exact |
| `tests/unit/macro/test_macro_coupling.c` | test | request-response | `tests/unit/macro/test_macro_gang.c` | exact |
| `tests/unit/macro/test_macro_link.c` | test | request-response | `tests/unit/macro/test_macro_group.c` | exact |
| `tests/unit/macro/test_macro_constrain.c` | test | request-response | `tests/unit/macro/test_macro_group.c` | exact |
| `tests/unit/macro/CMakeLists.txt` | config | N/A | self (Phase 20 version) | exact |

## Pattern Assignments

### `include/spu94/spu94_macro.h` (header -- MODIFY)

**Analog:** self (current Phase 20 version)

**Include guard + extern C pattern** (lines 1-27):
```c
#ifndef SPU94_MACRO_H
#define SPU94_MACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <spu94/spu94_registers.h>
#include <spu94/spu94.h>
```

**Group type struct pattern** (lines 37-58): existing `spu94_macro_member_t` and `spu94_macro_group_t`. Phase 21 adds `is_bipolar` field to `spu94_macro_group_t`:
```c
typedef struct {
    spu94_reg_t      reg;
    spu94_reg_type_t type;
    int32_t          floor;
    int32_t          ceiling;
} spu94_macro_member_t;

#define SPU94_MACRO_MAX_MEMBERS 16

/* Phase 21: bump from 8 to 16 for expanded control surface */
#define SPU94_MACRO_MAX_GROUPS 8  /* -> becomes 16 */

typedef struct {
    const char              *name;
    uint8_t                  member_count;
    spu94_macro_member_t     members[SPU94_MACRO_MAX_MEMBERS];
    /* Phase 21 addition: */
    /* uint8_t               is_bipolar; */
} spu94_macro_group_t;
```

**Enum revision pattern** (lines 62-72): existing enum with `__COUNT` sentinel. Phase 21 revises names and expands:
```c
typedef enum {
    SPU94_MACRO_ROOM_SIZE       = 0,
    SPU94_MACRO_ECHO_PHYSICS    = 1,
    SPU94_MACRO_DECAY           = 2,
    SPU94_MACRO_REFLECTIVITY    = 3,
    SPU94_MACRO_WIDTH           = 4,  /* REMOVE per D-27 */
    SPU94_MACRO_EARLY_REFL      = 5,
    SPU94_MACRO_DIFFUSION_AMT   = 6,
    SPU94_MACRO_DIFFUSION_TEX   = 7,
    SPU94_MACRO_GROUP__COUNT    = 8   /* -> becomes 10+ */
} spu94_macro_group_id_t;
```

**Function declaration pattern** (lines 85-132): every public function returns `spu94_result_t` or `float`, takes `spu94_state *state` as first param, uses `spu94_macro_group_id_t` for group selection. New declarations for Spread+Sweep apply and bipolar apply follow this exact signature pattern:
```c
spu94_result_t spu94_macro_apply(spu94_state *state,
                                  spu94_macro_group_id_t group_id,
                                  float position);
```

---

### `src/spu94/spu94_macro.c` (service -- MODIFY)

**Analog:** self (current Phase 20 version)

**Include pattern** (lines 28-33):
```c
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_macro.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
```

**Helper: read_member_value** (lines 38-44): reads register as int32, dispatches by type. Reuse as-is for Spread+Sweep and bipolar paths:
```c
static int32_t read_member_value(const spu94_state *state,
                                  const spu94_macro_member_t *m) {
    if (m->type == SPU94_REG_TYPE_U16)
        return (int32_t)spu94_get_reg_u16(state, m->reg);
    else
        return (int32_t)spu94_get_reg_i16(state, m->reg);
}
```

**Helper: write_member_value** (lines 49-60): clamps to member floor/ceiling, then uses safe setter. All macro writes go through this -- bipolar and Spread+Sweep will also use this path:
```c
static spu94_result_t write_member_value(spu94_state *state,
                                          const spu94_macro_member_t *m,
                                          int32_t value) {
    if (value < m->floor) value = m->floor;
    if (value > m->ceiling) value = m->ceiling;
    if (m->type == SPU94_REG_TYPE_U16)
        return spu94_safe_set_reg_u16(state, m->reg, (uint16_t)value);
    else
        return spu94_safe_set_reg_i16(state, m->reg, (int16_t)value);
}
```

**Core apply pattern with re-entrancy guard** (lines 181-228): the structure all new apply variants must follow -- validate inputs, clamp position, compute scale, set `macro_writing = 1`, iterate members, write via `write_member_value`, set `macro_writing = 0`, store knob pos, return worst result:
```c
spu94_result_t spu94_macro_apply(spu94_state *state,
                                  spu94_macro_group_id_t group_id,
                                  float position) {
    if (state == NULL) return SPU94_INVALID_STATE;
    if ((int)group_id < 0 || (int)group_id >= SPU94_MACRO_GROUP__COUNT)
        return SPU94_INVALID_ARG;
    const spu94_macro_group_t *group = state->macro_group_defs[group_id];
    if (group == NULL) return SPU94_INVALID_ARG;

    if (position < 0.0f) position = 0.0f;
    if (position > 1.0f) position = 1.0f;

    /* ... compute scale ... */

    state->macro_writing = 1;
    spu94_result_t worst = SPU94_OK;
    for (int i = 0; i < group->member_count; i++) {
        /* ... compute new_value ... */
        spu94_result_t r = write_member_value(state, &group->members[i], new_value);
        if (r != SPU94_OK && worst == SPU94_OK) worst = r;
    }
    state->macro_writing = 0;
    state->macro_knob_pos[group_id] = position;
    return worst;
}
```

**Derive pattern** (lines 135-163): snapshot current register values as base, compute max_scale, compute position = 1.0/max_scale. New `derive_all` function follows this per-group, iterated:
```c
float spu94_macro_derive(spu94_state *state, spu94_macro_group_id_t group_id) {
    /* ... validation ... */
    for (int i = 0; i < group->member_count; i++) {
        state->macro_base_values[group_id][i] =
            read_member_value(state, &group->members[i]);
    }
    float max_scale = compute_max_scale(group, state->macro_base_values[group_id]);
    /* ... compute and return position ... */
}
```

---

### `src/spu94/spu94_macro_controls.c` (service -- NEW)

**Analog:** `src/spu94/spu94_presets.c` (static const table pattern) + `src/spu94/spu94_macro.c` (engine API calls)

**Static const table pattern** from `spu94_presets.c` (lines 50-60): large static const data arrays with designated initializers. Group definitions follow the same pattern:
```c
const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT] = {
    [SPU94_PRESET_OFF] = {
        .name = "Off",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            /* ... */
        }
    },
    /* ... */
};
```

**Include pattern** for a new .c file that uses internal state + public headers:
```c
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_macro.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
```

**Group definition pattern** (from Phase 20 test fixtures, e.g. `test_macro_group.c` lines 30-38): this is the pattern for production group definitions:
```c
static const spu94_macro_group_t test_group_3u16 = {
    .name = "TestGroup3",
    .member_count = 3,
    .members = {
        { SPU94_REG_mLSAME, SPU94_REG_TYPE_U16, 0, 10000 },
        { SPU94_REG_mRSAME, SPU94_REG_TYPE_U16, 0, 10000 },
        { SPU94_REG_mLDIFF, SPU94_REG_TYPE_U16, 0, 10000 },
    }
};
```

---

### `src/spu94/spu94_state_internal.h` (model -- MODIFY)

**Analog:** self (current Phase 20 version)

**Phase 20 macro state block pattern** (lines 218-233): the block to extend. New fields for reference values, spread/sweep positions, link toggles, and constrained flags follow the same annotation style:
```c
    /* -----------------------------------------------------------------
     * Phase 20 (Macro Engine, MACRO-01..05): per-group runtime state
     * for proportional scaling, gang clamping, and re-derivation.
     *
     * 8 macro groups, each up to 16 members. Definitions are const
     * pointers to caller-provided group structs (typically static const
     * tables defined in Phase 21). Base values are snapshots taken at
     * derive time for proportional scaling.
     *
     * State budget: 8 pointers (64B) + 8x16 int32 (512B) + 8 floats
     * (32B) + 1 guard = ~609 bytes. Well within remaining headroom.
     * ----------------------------------------------------------------- */
    uint8_t        macro_writing;
    const spu94_macro_group_t *macro_group_defs[SPU94_MACRO_MAX_GROUPS];
    int32_t        macro_base_values[SPU94_MACRO_MAX_GROUPS][SPU94_MACRO_MAX_MEMBERS];
    float          macro_knob_pos[SPU94_MACRO_MAX_GROUPS];
```

**Field annotation convention**: every block has a Phase/Plan attribution comment, state budget summary, and zero-init guarantee note. New fields must follow this convention.

**Sizing guard** (lines 257-259):
```c
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");
```

---

### `tests/unit/macro/test_macro_spread_sweep.c` (test -- NEW)

**Analog:** `tests/unit/macro/test_macro_group.c`

**Test file boilerplate** (lines 1-27): includes, setUp/tearDown stubs, state buffer, fresh_state helper, set_and_tick helper:
```c
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_macro.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[SPU94_WORK_BUF_MAX_BYTES];

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}

static void set_and_tick(spu94_state *s, spu94_reg_t reg, uint16_t val) {
    spu94_set_reg_u16(s, reg, val);
    spu94_tick(s);
}
```

**Test function pattern** (lines 49-69): function named `test_<behavior>`, fresh state, set registers, register group, derive, apply, read back, assert:
```c
void test_register_and_derive(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);

    set_and_tick(s, SPU94_REG_mLSAME, 1000);
    set_and_tick(s, SPU94_REG_mRSAME, 2000);
    set_and_tick(s, SPU94_REG_mLDIFF, 3000);

    spu94_macro_register_group(s, SPU94_MACRO_ROOM_SIZE, &test_group_3u16);
    float pos = spu94_macro_derive(s, SPU94_MACRO_ROOM_SIZE);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, pos);
}
```

**Assertion patterns used**: `TEST_ASSERT_FLOAT_WITHIN`, `TEST_ASSERT_INT_WITHIN`, `TEST_ASSERT_EQUAL_UINT16`, `TEST_ASSERT_EQUAL_INT`, `TEST_ASSERT_NOT_NULL`, `TEST_ASSERT_TRUE`.

**main() pattern** (lines 230-240):
```c
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_register_and_derive);
    RUN_TEST(test_apply_proportional);
    /* ... */
    return UNITY_END();
}
```

---

### `tests/unit/macro/test_macro_bipolar.c` (test -- NEW)

**Analog:** `tests/unit/macro/test_macro_gang.c`

Same boilerplate as above. Additionally uses **i16 register setup pattern** for signed registers (lines 139-180 of test_macro_gang.c):
```c
/* i16 group definition with negative floor */
static const spu94_macro_group_t i16_group = {
    .name = "I16Gang",
    .member_count = 2,
    .members = {
        { SPU94_REG_vCOMB1, SPU94_REG_TYPE_I16, -1000, 32767 },
        { SPU94_REG_vCOMB2, SPU94_REG_TYPE_I16, -1000, 32767 },
    }
};

/* Signed register set -- no tick needed (IMMEDIATE policy) */
spu94_set_reg_i16(s, SPU94_REG_vCOMB1, 5000);
spu94_set_reg_i16(s, SPU94_REG_vCOMB2, 10000);
```

Key note: i16 registers (v-prefix) are IMMEDIATE policy -- no `spu94_tick()` needed after set. u16 registers (m-prefix, d-prefix) are TICK_LATCHED -- need `spu94_tick()` to flush.

---

### `tests/unit/macro/test_macro_coupling.c` (test -- NEW)

**Analog:** `tests/unit/macro/test_macro_gang.c`

Uses the same boilerplate. Additionally will need to read back safety-clamped values -- pattern from `src/spu94/spu94_safety.c` for stability coupling (lines 86-109):
```c
/* Safety layer clamps vWALL when abs(vIIR)*abs(vWALL) > STABILITY_LIMIT.
 * After Decay sets vIIR via safe setter, read back actual vWALL to verify
 * clamping, then check Reflectivity knob re-derived correctly. */
spu94_result_t r = spu94_safe_set_reg_i16(state, SPU94_REG_vIIR, value);
/* Returns SPU94_STABILITY_CLAMPED if product exceeded limit */
```

---

### `tests/unit/macro/test_macro_link.c` and `test_macro_constrain.c` (test -- NEW)

**Analog:** `tests/unit/macro/test_macro_group.c`

Same boilerplate. These test link toggle and constrained mode state flags. Pattern for setting wall distances then verifying echo speed propagation, or setting tap positions then verifying wall boundary clamping.

---

### `tests/unit/macro/CMakeLists.txt` (config -- MODIFY)

**Analog:** self (current Phase 20 version)

**Per-test-target pattern** (lines 1-3): one `add_executable` + `target_link_libraries` + `add_test` triplet per test file:
```cmake
add_executable(test_macro_group test_macro_group.c)
target_link_libraries(test_macro_group PRIVATE unity spu94_static)
add_test(NAME test_macro_group COMMAND test_macro_group)
```

New test files get the same 3-line block. Link target is always `unity spu94_static`.

---

## Shared Patterns

### Re-entrancy Guard
**Source:** `src/spu94/spu94_macro.c` lines 205, 225
**Apply to:** All new apply variants (Spread+Sweep, bipolar, buffer)
```c
state->macro_writing = 1;
/* ... batch writes ... */
state->macro_writing = 0;
```

### Safe Setter Routing
**Source:** `src/spu94/spu94_macro.c` lines 49-60 (`write_member_value`)
**Apply to:** All macro write paths -- never bypass safety enforcement
```c
static spu94_result_t write_member_value(spu94_state *state,
                                          const spu94_macro_member_t *m,
                                          int32_t value) {
    if (value < m->floor) value = m->floor;
    if (value > m->ceiling) value = m->ceiling;
    if (m->type == SPU94_REG_TYPE_U16)
        return spu94_safe_set_reg_u16(state, m->reg, (uint16_t)value);
    else
        return spu94_safe_set_reg_i16(state, m->reg, (int16_t)value);
}
```

### Worst-Result Tracking
**Source:** `src/spu94/spu94_macro.c` lines 207-223
**Apply to:** All apply functions that write multiple registers
```c
spu94_result_t worst = SPU94_OK;
for (int i = 0; i < group->member_count; i++) {
    spu94_result_t r = write_member_value(state, &group->members[i], new_value);
    if (r != SPU94_OK && worst == SPU94_OK) worst = r;
}
return worst;
```

### Input Validation
**Source:** `src/spu94/spu94_macro.c` lines 183-189
**Apply to:** All new public API functions
```c
if (state == NULL) return SPU94_INVALID_STATE;
if ((int)group_id < 0 || (int)group_id >= SPU94_MACRO_GROUP__COUNT)
    return SPU94_INVALID_ARG;
const spu94_macro_group_t *group = state->macro_group_defs[group_id];
if (group == NULL) return SPU94_INVALID_ARG;
```

### Result Enum Extension
**Source:** `include/spu94/spu94.h` lines 53-66
**Apply to:** If new result codes are needed (append-only, per contract)
```c
typedef enum {
    SPU94_OK                 = 0,
    /* ... existing codes ... */
    SPU94_ADDRESS_CLAMPED    = 8
    /* New codes append here with next sequential value */
} spu94_result_t;
```

### Test File Structure
**Source:** All 4 existing `tests/unit/macro/test_macro_*.c` files
**Apply to:** All 6 new test files
```c
/* File header with Phase/Plan attribution */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_macro.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[SPU94_WORK_BUF_MAX_BYTES];

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}
/* U16 helper with tick: */
static void set_and_tick(spu94_state *s, spu94_reg_t reg, uint16_t val) {
    spu94_set_reg_u16(s, reg, val);
    spu94_tick(s);
}
/* ... test functions ... */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_name);
    return UNITY_END();
}
```

### Register Write Timing
**Source:** `include/spu94/spu94_registers.h` lines 107-123 (write policy docs)
**Apply to:** All test files must respect this when reading back values
- **IMMEDIATE** (v-prefix gains + mBASE): no `spu94_tick()` needed after write; read back immediately
- **TICK_LATCHED** (d-prefix delays + m-prefix addresses): must call `spu94_tick()` before reading back active value

### Stability Enforcement
**Source:** `src/spu94/spu94_safety.c` lines 86-136
**Apply to:** Decay/Reflectivity coupling tests; any test verifying vIIR/vWALL interaction
```c
/* After setting vIIR to a large value, vWALL is clamped by safety layer:
 * abs(vIIR) * abs(vWALL) <= SPU94_STABILITY_LIMIT (0x40000000) */
spu94_safe_set_reg_i16(state, SPU94_REG_vIIR, some_value);
/* Then read back vWALL -- it may have been clamped */
int16_t actual_vwall = spu94_get_reg_i16(state, SPU94_REG_vWALL);
```

### Address Bounds Enforcement
**Source:** `src/spu94/spu94_safety.c` lines 146-166
**Apply to:** Buffer control tests; any test verifying m-prefix clamping after mBASE changes
```c
/* m-prefix registers (NOT mBASE) are clamped to (work_buf_size / 2) - 1 */
/* mBASE is explicitly excluded from m_prefix_addr_mask (line 45-61) */
```

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All Phase 21 files have strong analogs in the Phase 20 codebase |

Every file in Phase 21 extends or follows patterns already established in the Phase 20 macro engine, safety layer, and test suite. No greenfield patterns are needed.

## Metadata

**Analog search scope:** `include/spu94/`, `src/spu94/`, `tests/unit/macro/`
**Files scanned:** 12 (5 source files, 3 headers, 4 test files)
**Pattern extraction date:** 2026-05-04
