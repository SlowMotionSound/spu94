# Phase 13: Core Preset API - Pattern Map

**Mapped:** 2026-05-01
**Files analyzed:** 6 (2 new, 4 modified)
**Analogs found:** 6 / 6

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/spu94/spu94_preset_io.c` | service | transform (serialize/deserialize) | `src/spu94/spu94_presets.c` | exact |
| `include/spu94/spu94.h` | config (public API header) | N/A | self (append declarations) | exact |
| `src/spu94/CMakeLists.txt` | config (build) | N/A | self (append source file) | exact |
| `tests/unit/preset/test_preset_roundtrip.c` | test | transform (round-trip proof) | `tests/unit/preset/test_preset_load_all.c` | exact |
| `tests/unit/preset/test_preset_parse.c` | test | transform (edge-case parser) | `tests/unit/preset/test_preset_table_integrity.c` | role-match |
| `tests/unit/preset/CMakeLists.txt` | config (build) | N/A | self (append test targets) | exact |

## Pattern Assignments

### `src/spu94/spu94_preset_io.c` (service, transform -- serialize/deserialize)

**Analog:** `src/spu94/spu94_presets.c`

**Includes pattern** (lines 1-3 of analog, adapted for preset I/O needs):
```c
/* Source: src/spu94/spu94_presets.c lines 18-20 */
#include <spu94/spu94.h>
#include "spu94_state_internal.h"
#include <stdint.h>
```
The new file additionally needs `<stdio.h>` (for `snprintf`) and `<string.h>` (for `strcmp`, `strlen`, `strncmp`). These are permitted by grep-guard (verified in RESEARCH.md). The internal header is needed only if accessing struct fields directly; RESEARCH.md recommends using public accessors instead, which means `"spu94_state_internal.h"` may be unnecessary -- `<spu94/spu94.h>` and `<spu94/spu94_registers.h>` suffice for the accessor-based approach.

**Accessor-based preferred includes:**
```c
/* Recommended for spu94_preset_io.c (public accessor approach) */
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdio.h>    /* snprintf */
#include <string.h>   /* strcmp, strlen */
#include <stdint.h>
```

**Null-guard + range-check pattern** (spu94_presets.c lines 508-512):
```c
/* Source: src/spu94/spu94_presets.c lines 508-512 */
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id) {
    if (state == NULL) return SPU94_INVALID_STATE;
    if ((int)id < 0 || (int)id >= (int)SPU94_PRESET__COUNT) {
        return SPU94_INVALID_ARG;
    }
```
Apply this same guard-clause style at the top of both `spu94_preset_save` and `spu94_preset_load`.

**Register iteration + type dispatch pattern** (spu94_presets.c lines 520-527):
```c
/* Source: src/spu94/spu94_presets.c lines 520-527 */
for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
    const int16_t raw = p->regs[r];
    if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
        (void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
    } else {
        (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
    }
}
```
The load function reuses this exact pattern for restoring registers from parsed hex values. The save function uses `spu94_snapshot_registers()` for bulk read, then `spu94_reg_name()` and `spu94_reg_type()` for formatting.

**Register name lookup pattern** (spu94_registers.c lines 111-116):
```c
/* Source: src/spu94/spu94_registers.c lines 111-116 */
const char *spu94_reg_name(spu94_reg_t reg) {
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return (const char *)0;
    }
    return spu94_reg_names[reg];
}
```
The save function calls `spu94_reg_name(r)` to get each key string. The load function matches parsed key strings against `spu94_reg_name(r)` in a loop for register dispatch.

**Return code convention:** `spu94_load_preset` returns `spu94_result_t` (spu94_presets.c line 508, 528). Per RESEARCH.md, `spu94_preset_save` returns `int` (bytes written or negative error) -- a departure from the `spu94_result_t` pattern, justified because callers need the byte count. `spu94_preset_load` returns `spu94_result_t` matching `spu94_load_preset`.

**Cast to void pattern for ignoring result codes** (spu94_presets.c lines 523-526):
```c
/* Source: src/spu94/spu94_presets.c lines 523, 525 */
(void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
/* ... */
(void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
```
The `(void)` cast explicitly acknowledges the ignored return value. The load function should follow this.

---

### `include/spu94/spu94.h` (config -- public API header, MODIFY)

**Analog:** self (append to existing patterns in the file)

**Function declaration pattern** (spu94.h lines 329-332, 483):
```c
/* Source: include/spu94/spu94.h lines 329-332 */
void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples);

/* Source: include/spu94/spu94.h line 483 */
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id);
```
New declarations should follow the same style: multi-line parameter lists for long signatures, inline doc comments above each function, grouped under a section comment banner.

**Section banner pattern** (spu94.h lines 345-347):
```c
/* Source: include/spu94/spu94.h lines 345-347 */
/* ------------------------------------------------------------------------- */
/* Factory preset surface (Phase 5, D-06)                                    */
/* ------------------------------------------------------------------------- */
```
Add a new banner for the user-preset serialization API.

**#define constant pattern** (spu94.h lines 85-86, 400):
```c
/* Source: include/spu94/spu94.h line 85 */
#define SPU94_STATE_SIZE_MAX  16384u

/* Source: include/spu94/spu94.h line 400 */
#define SPU94_WORK_BUF_MAX_BYTES 0x80000u
```
`SPU94_PRESET_BUF_SIZE 4096u` should follow this exact style -- `#define` with `u` suffix, comment above explaining the value.

**Placement:** New declarations should go AFTER the factory preset surface section (after line 483) and BEFORE the `#ifdef __cplusplus` closing brace (line 486). This preserves the logical grouping: factory presets, then user-preset serialization.

---

### `src/spu94/CMakeLists.txt` (config -- build, MODIFY)

**Analog:** self

**Source file addition pattern** (CMakeLists.txt lines 5-25):
```cmake
# Source: src/spu94/CMakeLists.txt lines 5-25
add_library(spu94_obj OBJECT
    spu94_state.c
    spu94_registers.c
    # ... (one file per line, no trailing comma)
    spu94_presets.c
    # ... more files
)
```
Add `spu94_preset_io.c` to the `add_library(spu94_obj OBJECT ...)` list. Place it after `spu94_presets.c` (line 18) for logical grouping.

---

### `tests/unit/preset/test_preset_roundtrip.c` (test, transform -- round-trip proof)

**Analog:** `tests/unit/preset/test_preset_load_all.c`

**Includes pattern** (test_preset_load_all.c lines 30-35):
```c
/* Source: tests/unit/preset/test_preset_load_all.c lines 30-35 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
```
The round-trip test will additionally need `<string.h>` for `memcmp` or `strcmp` on the serialized buffer.

**State allocation pattern** (test_preset_load_all.c lines 37-39):
```c
/* Source: tests/unit/preset/test_preset_load_all.c lines 37-39 */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[64 * 1024];
static spu94_state *state = NULL;
```
The round-trip test needs TWO states (one for save, one for load-and-compare) or can reuse one with reset between operations.

**setUp/tearDown pattern** (test_preset_load_all.c lines 41-46):
```c
/* Source: tests/unit/preset/test_preset_load_all.c lines 41-46 */
void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}
void tearDown(void) { state = NULL; }
```

**Diagnostic message pattern** (test_preset_load_all.c lines 93-97):
```c
/* Source: tests/unit/preset/test_preset_load_all.c lines 93-97 */
char msg[96];
snprintf(msg, sizeof msg,
         "preset=%d reg=%d (I16) active mismatch", id, r);
TEST_ASSERT_EQUAL_INT16_MESSAGE(p->regs[r],
    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
```
Round-trip test should use `_MESSAGE` variants with identifying context (which register, which field type) for cell-level failure diagnosis.

**main() pattern** (test_preset_load_all.c lines 180-189):
```c
/* Source: tests/unit/preset/test_preset_load_all.c lines 180-189 */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_load_null_state_invalid);
    RUN_TEST(test_load_out_of_range_id);
    /* ... */
    return UNITY_END();
}
```

---

### `tests/unit/preset/test_preset_parse.c` (test, transform -- parser edge cases)

**Analog:** `tests/unit/preset/test_preset_table_integrity.c`

**Simpler test includes pattern** (test_preset_table_integrity.c lines 1-3):
```c
/* Source: tests/unit/preset/test_preset_table_integrity.c lines 1-3 */
#include "unity.h"
#include <spu94/spu94.h>
#include <string.h>
```
Parser edge-case tests need `<spu94/spu94_registers.h>`, `<stdalign.h>`, `<stdint.h>`, and `<stdio.h>` as well (same as the round-trip test, since they exercise the full load path).

**setUp/tearDown for stateless tests** (test_preset_table_integrity.c lines 37-38):
```c
/* Source: tests/unit/preset/test_preset_table_integrity.c lines 37-38 */
void setUp(void)    {}
void tearDown(void) {}
```
However, the parser tests DO need state (to load into), so they should use the stateful setUp pattern from test_preset_load_all.c instead.

**main() pattern** identical to above.

---

### `tests/unit/preset/CMakeLists.txt` (config -- build, MODIFY)

**Analog:** self

**Test target pattern** (CMakeLists.txt lines 4-11):
```cmake
# Source: tests/unit/preset/CMakeLists.txt lines 4-11
add_executable(test_preset_table_integrity test_preset_table_integrity.c)
target_link_libraries(test_preset_table_integrity
    PRIVATE
        unity
        spu94_static
)
add_test(NAME preset_table_integrity COMMAND test_preset_table_integrity)
set_tests_properties(preset_table_integrity PROPERTIES LABELS "preset")
```

**Test target with internal header access** (CMakeLists.txt lines 14-27):
```cmake
# Source: tests/unit/preset/CMakeLists.txt lines 14-27
add_executable(test_preset_load_all test_preset_load_all.c)
target_link_libraries(test_preset_load_all
    PRIVATE
        unity
        spu94_static
        spu94_warnings
)
target_include_directories(test_preset_load_all PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_preset_load_all COMMAND test_preset_load_all)
set_tests_properties(test_preset_load_all PROPERTIES LABELS "preset")
```
The new test targets need `spu94_warnings` and the `target_include_directories` for `src/spu94` only if they include `spu94_state_internal.h`. If they use only the public API (which is the recommended approach for preset I/O tests), the simpler pattern (lines 4-11) suffices -- just `unity` and `spu94_static`.

The `spu94_warnings` link is needed regardless for consistent compiler warning flags. Use the full pattern (lines 14-27) for both new test targets.

---

## Shared Patterns

### Null-Safety Guard Clause
**Source:** `src/spu94/spu94_presets.c` lines 508-510, `src/spu94/spu94_state.c` lines 67-68, 110-112
**Apply to:** `spu94_preset_save`, `spu94_preset_load`
```c
/* Source: src/spu94/spu94_presets.c lines 508-509 */
if (state == NULL) return SPU94_INVALID_STATE;
```
Every public function that takes `spu94_state *` begins with a NULL check. Mutation functions return an error code; read-only functions return a zero/default.

### Register Type Dispatch
**Source:** `src/spu94/spu94_presets.c` lines 520-527, `include/spu94/spu94_registers.h` lines 95-103
**Apply to:** Both save (formatting) and load (restoration) paths in `spu94_preset_io.c`
```c
/* Source: src/spu94/spu94_presets.c lines 522-526 */
if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
    (void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
} else {
    (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
}
```
The I16/U16 split determines setter choice on load and casting on save (all values are formatted as unsigned hex regardless, but must be CAST to `(unsigned)(uint16_t)` before `%04X` formatting to avoid sign-extension -- see RESEARCH.md Pitfall 1).

### Zero-Heap Invariant
**Source:** `scripts/ci/grep-guard.sh`, `tests/rt_safety/test_no_heap.sh`
**Apply to:** `src/spu94/spu94_preset_io.c`
No `malloc`, `calloc`, `realloc`, `free` anywhere in the file. All memory is caller-provided buffers. Verified at CI by `grep-guard.sh` (scans source text) and `test_no_heap.sh` (scans linker symbols of `libspu94.so`).

### No Float/Double in Core
**Source:** `scripts/ci/grep-guard.sh`
**Apply to:** `src/spu94/spu94_preset_io.c`
Tokens `float` and `double` are forbidden in `src/spu94/` and `include/spu94/`. All values are integer (int16 hex or boolean 0/1).

### No Unqualified `long` in Core
**Source:** `scripts/ci/grep-guard.sh`
**Apply to:** `src/spu94/spu94_preset_io.c`
The token `long` (not preceded by another type keyword) is forbidden. This affects `strtol` usage -- RESEARCH.md recommends a hand-rolled `parse_hex_u16()` function that returns `uint16_t` directly, avoiding `long` entirely.

### Unity Test Structure
**Source:** `tests/unit/preset/test_preset_load_all.c` lines 30-46, 180-189
**Apply to:** Both new test files
```c
/* Source: tests/unit/preset/test_preset_load_all.c structure */
#include "unity.h"
/* ... other includes ... */

/* static state allocation */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[64 * 1024];
static spu94_state *state = NULL;

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}
void tearDown(void) { state = NULL; }

/* test functions... */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_name_here);
    /* ... */
    return UNITY_END();
}
```

### CMake Test Target Pattern
**Source:** `tests/unit/preset/CMakeLists.txt` lines 14-27
**Apply to:** Both new test target additions
```cmake
# Source: tests/unit/preset/CMakeLists.txt lines 14-27 (template)
add_executable(TARGET_NAME source_file.c)
target_link_libraries(TARGET_NAME
    PRIVATE
        unity
        spu94_static
        spu94_warnings
)
target_include_directories(TARGET_NAME PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME TARGET_NAME COMMAND TARGET_NAME)
set_tests_properties(TARGET_NAME PROPERTIES LABELS "preset")
```
The `LABELS "preset"` tag enables `ctest -L preset` filtering.

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All files have strong analogs in the existing codebase |

Every file in Phase 13 has an exact or role-match analog. The serialization logic (`spu94_preset_io.c`) is a natural complement to the existing factory preset loader (`spu94_presets.c`) and reuses the same register infrastructure. The tests follow the established Unity + ctest pattern already used by the preset test suite.

---

## Metadata

**Analog search scope:** `src/spu94/`, `include/spu94/`, `tests/unit/preset/`
**Files scanned:** 12 (6 analog candidates read, all matched)
**Pattern extraction date:** 2026-05-01
