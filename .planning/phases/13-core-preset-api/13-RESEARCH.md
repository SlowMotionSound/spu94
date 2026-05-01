# Phase 13: Core Preset API - Research

**Researched:** 2026-05-01
**Domain:** C serialization/deserialization of SPU state to human-readable key=value text
**Confidence:** HIGH

## Summary

Phase 13 adds two public C functions (`spu94_preset_save` and `spu94_preset_load`) to libspu94 that serialize the full SPU state (35 registers + 6 mixer faders + latency_comp toggle + 4 DAC controls = 46 fields total) to a versioned INI-style text buffer and restore it with bit-identical fidelity. The format uses `[registers]`, `[mixer]`, `[dac]` sections with hex values for 16-bit fields and `0`/`1` for booleans, preceded by `version=1`, `name=`, and `description=` metadata lines.

The codebase already provides every building block needed: `spu94_snapshot_registers()` dumps all 35 register values, `spu94_reg_name()` provides key strings, individual `spu94_get_*`/`spu94_set_*` accessors cover every mixer fader and DAC toggle, and `spu94_load_preset()` demonstrates the atomic register-restoration pattern. The serialization implementation is a straightforward iteration over known fields with `snprintf`-formatted output; the parser is a simple line-by-line tokenizer splitting on `=` with section-header state tracking.

**Primary recommendation:** Implement as a single new source file `src/spu94/spu94_preset_io.c` with save and load functions that use `snprintf` for formatting and hand-rolled line parsing (split on `=`, match known keys). No external libraries needed. Buffer sizing via a compile-time constant derived from the field inventory.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** INI-style sectioned format with `[registers]`, `[mixer]`, `[dac]` section headers. Version line and metadata at top, outside any section.
- **D-02:** All 16-bit values written as 4-digit hex (`0x0000`-`0xFFFF`). Boolean toggles written as `0`/`1`. Consistent representation everywhere.
- **D-03:** Section-level `#` comment lines at the top of each section for orientation (e.g. `# SPU reverb registers (35 values, hex)`). No per-key inline comments.
- **D-04:** Name + description metadata fields at the top of the file, before any section. Self-documenting for sharing and browsing.
- **D-05:** Preset captures exactly: 35 SPU registers, 6 mixer faders (input_gain, dry_fader, patina_fader, dry_send, patina_send, reverb_fader), latency_comp toggle, and 4 DAC controls (dac_enabled, dac_fir_enabled, dac_noise_enabled, dac_true_oversample).
- **D-06:** ADPCM toggle is NOT saved. ADPCM is always-on infrastructure with its own bus path.
- **D-07:** Latency compensation (on/off) IS saved.
- **D-08:** Missing keys get the engine's default value. Old presets always load into newer software.
- **D-09:** Unknown keys are silently ignored. Newer presets load in older software (losing unrecognized fields). Tolerant of hand-edit typos.

### Claude's Discretion
- API signature details (return type, error codes, buffer sizing strategy) -- follow the zero-heap, caller-provides-buffer pattern established throughout the codebase
- Parser implementation approach -- simple line-by-line key=value parser is straightforward
- Register write ordering during load -- follow existing `spu94_load_preset` patterns for atomic state restoration
- Whether to expose factory presets through the new save format (serialize the 10 built-in presets as .spu94 text)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PRE-01 | `spu94_preset_save` writes all register + mixer + DAC state to a caller-provided buffer in key=value text format | Field inventory (46 fields), format specification, `snprintf`-based serialization pattern, buffer sizing constant |
| PRE-02 | `spu94_preset_load` reads a key=value text buffer and restores all register + mixer + DAC state | Line-by-line parser pattern, section state machine, key-matching dispatch, existing setter APIs |
| PRE-03 | Preset format includes a version header so future additions don't break old files | Version line as first non-blank line, D-08/D-09 tolerance rules |
| PRE-04 | Round-trip fidelity -- save then load produces bit-identical SPU state | Hex representation preserves exact bit patterns, round-trip test pattern documented |
| PRE-05 | .spu94 file extension, plain text, human-readable and hand-editable | INI-style format with section comments, 4-digit hex notation, example output documented |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| State serialization (save) | C core library (libspu94) | -- | All DSP state lives in the C core; serialization must access internal state via public accessors |
| State deserialization (load) | C core library (libspu94) | -- | Restoration uses existing setter APIs; must be atomic like `spu94_load_preset` |
| Buffer memory management | Caller (CLI/JUCE) | -- | Zero-heap pattern: caller provides buffer, library fills it |
| File I/O (.spu94 read/write) | Phase 14 (CLI/JUCE) | -- | Phase 13 operates on text buffers only; file I/O is Phase 14's responsibility |
| Format versioning | C core library | -- | Version header is written/parsed by the core; tolerance rules (D-08/D-09) are core behavior |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C99 standard library | -- | `snprintf`, `strcmp`, `strncmp`, `strtol`, `strlen` | Already linked into libspu94 (`memcpy`, `memset`, `strlen` confirmed in `nm -u`); no new dependencies [VERIFIED: `nm -u build/src/spu94/libspu94.so`] |
| Unity test framework | vendored | Round-trip and parsing tests | Already used by all existing unit tests [VERIFIED: tests/unit/preset/CMakeLists.txt] |
| ctest | 3.31.6 | Test runner with label filtering | Existing infrastructure, `preset` label already in use [VERIFIED: `ctest -L preset -N`] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| None | -- | -- | No additional libraries needed |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Hand-rolled line parser | jsmn (vendored) | jsmn is JSON-only; INI-style format doesn't need it. The parser is ~60 lines of straightforward C. |
| `snprintf` formatting | Hand-rolled hex conversion | `snprintf` is cleaner and already linkable; hand-rolled saves one `<stdio.h>` include but adds maintenance burden |
| Single source file | Separate save.c / load.c | One file is simpler; the total code is ~300 lines -- splitting adds CMake churn for no benefit |

## Architecture Patterns

### System Architecture Diagram

```
Caller (CLI / JUCE / test)
    |
    |-- provides: spu94_state*, char buf[], size_t buf_size
    |
    v
spu94_preset_save(state, name, desc, buf, buf_size)
    |
    |-- reads 35 registers via spu94_snapshot_registers()
    |-- reads 6 mixer faders via spu94_get_input_gain() etc.
    |-- reads latency_comp via spu94_get_latency_comp()
    |-- reads 4 DAC toggles via spu94_get_dac_enabled() etc.
    |
    |-- writes formatted text to buf via snprintf:
    |     version=1
    |     name=Hall
    |     description=...
    |     [registers]
    |     # SPU reverb registers (35 values, hex)
    |     vLOUT=0x7FFF
    |     ...
    |     [mixer]
    |     # Mixer faders (Q15 hex) and latency compensation
    |     input_gain=0x7FFF
    |     ...
    |     [dac]
    |     # DAC coloration toggles
    |     dac_enabled=1
    |     ...
    |
    v
Returns: bytes written (success) or negative error code

---

Caller
    |
    |-- provides: spu94_state*, const char buf[], size_t buf_len
    |
    v
spu94_preset_load(state, buf, buf_len)
    |
    |-- line-by-line parse:
    |     1. skip blank lines and # comments
    |     2. track current section via [header] lines
    |     3. split key=value on first '='
    |     4. dispatch by section + key name:
    |         [registers] -> spu94_set_reg_i16/u16 by spu94_reg_name match
    |         [mixer]     -> spu94_set_input_gain etc.
    |         [dac]       -> spu94_set_dac_enabled etc.
    |     5. unknown keys silently ignored (D-09)
    |     6. missing keys leave engine defaults (D-08)
    |
    v
Returns: SPU94_OK or error code
```

### Recommended Project Structure
```
include/spu94/
    spu94.h              # Add spu94_preset_save / spu94_preset_load declarations
                         # Add SPU94_PRESET_BUF_SIZE constant
src/spu94/
    spu94_preset_io.c    # NEW: save + load implementation
    CMakeLists.txt       # Add spu94_preset_io.c to spu94_obj OBJECT library
tests/unit/preset/
    test_preset_roundtrip.c  # NEW: round-trip bit-identical fidelity proof
    test_preset_parse.c      # NEW: parser edge cases (missing keys, unknown keys, comments, blank lines)
    CMakeLists.txt           # Add new test targets
```

### Pattern 1: Caller-Provides-Buffer Serialization
**What:** Save function writes to a caller-provided `char[]` buffer, returns bytes written or a negative error code. No heap allocation.
**When to use:** Every public API in this codebase follows this pattern (see `spu94_init`, `spu94_snapshot_registers`).
**Example:**
```c
// Source: established codebase pattern from spu94_init (include/spu94/spu94.h)
// Save: caller provides buffer
#define SPU94_PRESET_BUF_SIZE 4096u  /* generous upper bound */

int spu94_preset_save(const spu94_state *state,
                      const char *name,        /* may be NULL */
                      const char *description,  /* may be NULL */
                      char *buf, size_t buf_size);
// Returns: bytes written (>= 0) on success, negative on error

// Load: caller provides text buffer
spu94_result_t spu94_preset_load(spu94_state *state,
                                 const char *buf, size_t buf_len);
// Returns: SPU94_OK on success, error code on failure
```

### Pattern 2: Section-Aware Line Parser
**What:** State machine that tracks current INI section and dispatches key=value pairs to the appropriate setter.
**When to use:** For the load path. Simpler than a full INI library; the format is fixed and small.
**Example:**
```c
// Source: derived from codebase conventions [VERIFIED: codebase analysis]
typedef enum {
    SECTION_NONE = 0,      /* before any [section] header */
    SECTION_REGISTERS,
    SECTION_MIXER,
    SECTION_DAC
} preset_section_t;

// Parse loop pseudocode:
// for each line in buf:
//   skip blank lines and lines starting with '#'
//   if line matches "[registers]" -> section = SECTION_REGISTERS
//   if line matches "[mixer]"     -> section = SECTION_MIXER
//   if line matches "[dac]"       -> section = SECTION_DAC
//   else: split on '=' -> key, value
//     switch (section):
//       SECTION_NONE: parse version/name/description metadata
//       SECTION_REGISTERS: match key against spu94_reg_name(0..34)
//       SECTION_MIXER: match key against "input_gain", "dry_fader", etc.
//       SECTION_DAC: match key against "dac_enabled", etc.
```

### Pattern 3: Register Restoration via Existing Setters
**What:** Load path restores registers through the typed engine-layer setters, exactly as `spu94_load_preset()` does for factory presets.
**When to use:** Ensures write-timing policy (IMMEDIATE vs TICK_LATCHED) is honored automatically.
**Example:**
```c
// Source: spu94_presets.c spu94_load_preset() [VERIFIED: src/spu94/spu94_presets.c:520-528]
// Existing factory preset loader iterates registers and dispatches by type:
for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
    const int16_t raw = p->regs[r];
    if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
        (void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
    } else {
        (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
    }
}
// The preset_load function should do the same for parsed register values.
```

### Anti-Patterns to Avoid
- **Heap allocation in the serializer:** The library has a strict no-malloc invariant enforced by `test_no_heap.sh` and `grep-guard.sh`. All memory must be caller-provided.
- **Using `float`/`double` anywhere in core sources:** Grep-guard forbids these in `src/spu94/` and `include/spu94/`. All values are integer (int16 hex or boolean 0/1).
- **Using unqualified `long`:** Grep-guard forbids this in core sources. Use `int32_t`/`int64_t` or cast through `strtol` carefully (the return type is `long`, but the value fits in `int32_t`).
- **Direct struct field access from public API:** The state struct is opaque. Save must use `spu94_snapshot_registers()` and `spu94_get_*()` accessors. Load must use `spu94_set_reg_*()` and `spu94_set_*()` setters. The implementation file CAN include `spu94_state_internal.h` if needed, but using the public API is cleaner and maintains encapsulation.
- **Partial state on parse error:** D-08 says missing keys get defaults, D-09 says unknown keys are ignored. The load function should NOT fail on malformed individual lines -- it should skip them and keep going. Only truly catastrophic issues (NULL state, NULL buf) should return errors.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Hex formatting | Custom hex-digit loop | `snprintf(buf, n, "0x%04X", val)` | `snprintf` is already linkable (no new deps); correct for all edge cases |
| Hex parsing | Custom hex parser | `strtol(str, &end, 16)` with validation | Handles `0x` prefix, overflow, invalid chars correctly |
| Register name lookup | Duplicate string table | `spu94_reg_name(reg)` (already exists) | Single source of truth for the 35 register names [VERIFIED: src/spu94/spu94_registers.c] |
| Register type dispatch | Hardcoded type lists | `spu94_reg_type(reg)` (already exists) | Determines I16 vs U16 signedness per register [VERIFIED: include/spu94/spu94_registers.h] |

**Key insight:** The existing register infrastructure (`spu94_reg_name`, `spu94_reg_type`, `spu94_snapshot_registers`, `spu94_set_reg_i16/u16`) already provides 90% of the machinery. The preset serializer is thin glue code formatting and parsing around these existing building blocks.

## Common Pitfalls

### Pitfall 1: Signedness Mismatch in Hex Formatting
**What goes wrong:** Formatting a signed int16 value (like `vIIR = 0xBCE0` which is `-17184` as int16) through `%04X` without casting to `uint16_t` first. If `int16_t` is sign-extended to `int`, `snprintf` with `%04X` and a negative value produces `0xFFFFBCE0` (8 digits), not `0xBCE0` (4 digits).
**Why it happens:** C integer promotion rules sign-extend `int16_t` to `int` in variadic function arguments.
**How to avoid:** Always cast to `(unsigned)(uint16_t)value` before passing to `%04X`. The snapshot returns `int16_t` values; reinterpret the bit pattern as `uint16_t` for formatting.
**Warning signs:** Hex output longer than 4 digits for any register value.

### Pitfall 2: `strtol` Returns `long`, Grep-Guard Forbids `long`
**What goes wrong:** Using `strtol` in core source code requires declaring a `long` variable for the return type, but grep-guard forbids unqualified `long` in `src/spu94/`.
**Why it happens:** `strtol` is the standard C hex parser, but its return type is `long`.
**How to avoid:** Two options: (a) use `strtol` with an immediate cast to `int32_t` on the same line, structured so grep-guard's `grep -v 'long long'` filter doesn't hide it; or (b) write a minimal hand-rolled 4-digit hex parser that returns `uint16_t` directly (trivial for exactly-4-digit hex with `0x` prefix). Option (b) is cleaner given the format constraint that all values are `0xNNNN`.
**Warning signs:** grep-guard CI failure mentioning `long`.

### Pitfall 3: Missing Null Terminator in Buffer Sizing
**What goes wrong:** `snprintf` returns the number of characters that WOULD have been written (excluding null terminator). If `buf_size` is exactly the needed text length, the null terminator is silently truncated.
**Why it happens:** Off-by-one in buffer size calculation.
**How to avoid:** Define `SPU94_PRESET_BUF_SIZE` with generous headroom (4096 bytes when the max realistic output is ~1700 bytes). The save function should track remaining space and fail cleanly if the buffer is too small.
**Warning signs:** Truncated last line in output, or missing null terminator causing strlen to overrun.

### Pitfall 4: Not Restoring Register Write-Timing Policy on Load
**What goes wrong:** Directly writing to `state->reg_values[]` instead of using `spu94_set_reg_i16/u16`. This bypasses the IMMEDIATE/TICK_LATCHED split policy, leaving d-prefix and m-prefix registers in an inconsistent state.
**Why it happens:** Tempting shortcut to access the internal struct directly.
**How to avoid:** Always use the public setter APIs, exactly as `spu94_load_preset()` does. The write-timing policy is automatically honored.
**Warning signs:** Loaded presets sound different from the same preset loaded via the factory preset loader.

### Pitfall 5: Mixer Fader Default Values After Load
**What goes wrong:** A preset file saved with version=1 is loaded on a future version that adds a new mixer control. If the loader resets ALL mixer state to zero before parsing, the new control's engine default is lost.
**Why it happens:** Overaggressive pre-load reset.
**How to avoid:** Do NOT reset state before loading. The D-08 contract says missing keys retain engine defaults. The load function should only SET values it finds in the file, leaving everything else at whatever the engine currently has. If the caller wants a clean slate, they call `spu94_reset()` first (which applies engine defaults including `latency_comp=1` and `dac_true_oversample=1`).
**Warning signs:** Loaded presets have unexpected silence or missing DAC mode.

### Pitfall 6: `strtol` Overflow for Invalid Hand-Edited Values
**What goes wrong:** A user hand-edits `vIIR=0x1FFFF` (5 hex digits, exceeds uint16 range). `strtol` returns `131071` without error, and casting to `int16_t` silently wraps.
**Why it happens:** No range validation after parsing.
**How to avoid:** After `strtol`, check `value >= 0 && value <= 0xFFFF` for hex fields. Out-of-range values should be clamped or the line should be silently ignored (per D-09's tolerance philosophy).
**Warning signs:** Hand-edited presets producing unexpected register values.

## Code Examples

### Example 1: Expected .spu94 File Output
```
version=1
name=Hall
description=PS1 factory Hall reverb with full DAC chain

[registers]
# SPU reverb registers (35 values, hex)
vLOUT=0x7FFF
vROUT=0x7FFF
mBASE=0x0000
dAPF1=0x01A5
dAPF2=0x0139
vIIR=0x6000
vCOMB1=0x5000
vCOMB2=0x4C00
vCOMB3=0xB800
vCOMB4=0xBC00
vWALL=0xC000
vAPF1=0x6000
vAPF2=0x5C00
mLSAME=0x15BA
mRSAME=0x11BB
mLCOMB1=0x14C2
mRCOMB1=0x10BD
mLCOMB2=0x11BC
mRCOMB2=0x0DC1
dLSAME=0x11C0
dRSAME=0x0DC3
mLDIFF=0x0DC0
mRDIFF=0x09C1
mLCOMB3=0x0BC4
mRCOMB3=0x07C1
mLCOMB4=0x0A00
mRCOMB4=0x06CD
dLDIFF=0x09C2
dRDIFF=0x05C1
mLAPF1=0x05C0
mRAPF1=0x041A
mLAPF2=0x0274
mRAPF2=0x013A
vLIN=0x8000
vRIN=0x8000

[mixer]
# Mixer faders (Q15 hex) and latency compensation
input_gain=0x7FFF
dry_fader=0x6000
patina_fader=0x0000
dry_send=0x4000
patina_send=0x0000
reverb_fader=0x5000
latency_comp=1

[dac]
# DAC coloration toggles
dac_enabled=1
dac_fir_enabled=1
dac_noise_enabled=1
dac_true_oversample=1
```

### Example 2: Save Function Skeleton
```c
// Source: derived from codebase patterns [VERIFIED: codebase analysis]
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdio.h>   /* snprintf */
#include <string.h>  /* strcmp, strlen */

int spu94_preset_save(const spu94_state *state,
                      const char *name,
                      const char *description,
                      char *buf, size_t buf_size)
{
    if (!state || !buf || buf_size == 0) return -1;

    int pos = 0;
    int n;

    /* Version header */
    n = snprintf(buf + pos, buf_size - pos, "version=1\n");
    if (n < 0 || (size_t)(pos + n) >= buf_size) return -1;
    pos += n;

    /* Metadata */
    n = snprintf(buf + pos, buf_size - pos, "name=%s\n",
                 name ? name : "");
    if (n < 0 || (size_t)(pos + n) >= buf_size) return -1;
    pos += n;

    n = snprintf(buf + pos, buf_size - pos, "description=%s\n",
                 description ? description : "");
    if (n < 0 || (size_t)(pos + n) >= buf_size) return -1;
    pos += n;

    /* [registers] section */
    n = snprintf(buf + pos, buf_size - pos,
                 "\n[registers]\n"
                 "# SPU reverb registers (35 values, hex)\n");
    /* ... snprintf each register as key=0xNNNN ... */

    /* [mixer] section */
    /* ... snprintf each mixer fader as key=0xNNNN, latency_comp as 0/1 ... */

    /* [dac] section */
    /* ... snprintf each DAC toggle as key=0/1 ... */

    return pos;  /* bytes written, excluding null terminator */
}
```

### Example 3: Load Function Key-Matching Pattern
```c
// Source: derived from spu94_load_preset pattern [VERIFIED: src/spu94/spu94_presets.c]
// For the [registers] section, match key name against the register name table:
for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
    if (strcmp(key, spu94_reg_name((spu94_reg_t)r)) == 0) {
        uint16_t val = parse_hex_u16(value_str);
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            spu94_set_reg_i16(state, (spu94_reg_t)r, (int16_t)val);
        } else {
            spu94_set_reg_u16(state, (spu94_reg_t)r, val);
        }
        break;
    }
}
// For [mixer] and [dac] sections, use direct strcmp chains:
// if (strcmp(key, "input_gain") == 0) spu94_set_input_gain(state, (int16_t)parse_hex_u16(value_str));
// etc.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Factory presets only (hardcoded C tables) | User-saveable presets (text files) | Phase 13 (this phase) | Users can share, hand-edit, and archive custom configurations |

**Deprecated/outdated:**
- Nothing deprecated. The factory preset mechanism (`spu94_load_preset`) remains unchanged; Phase 13 adds a complementary user-preset system alongside it.

## Field Inventory (Complete Serialization Target)

This is the authoritative field list derived from CONTEXT.md D-05 and verified against the internal state struct and public accessor API.

[VERIFIED: `src/spu94/spu94_state_internal.h` struct layout + `include/spu94/spu94.h` accessor declarations]

### [registers] Section -- 35 fields (all 4-digit hex)
| Key | Accessor (save) | Accessor (load) | Type |
|-----|-----------------|------------------|------|
| `vLOUT` through `vRIN` (35 total) | `spu94_snapshot_registers()` bulk dump | `spu94_set_reg_i16/u16()` per `spu94_reg_type()` | I16 or U16, all formatted as `0xNNNN` |

Register names are provided by `spu94_reg_name()` in enum order: vLOUT, vROUT, mBASE, dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN.

### [mixer] Section -- 7 fields
| Key | Type | Accessor (save) | Accessor (load) |
|-----|------|-----------------|------------------|
| `input_gain` | Q15 hex (`0xNNNN`) | `spu94_get_input_gain()` | `spu94_set_input_gain()` |
| `dry_fader` | Q15 hex | `spu94_get_dry_fader()` | `spu94_set_dry_fader()` |
| `patina_fader` | Q15 hex | `spu94_get_patina_fader()` | `spu94_set_patina_fader()` |
| `dry_send` | Q15 hex | `spu94_get_dry_send()` | `spu94_set_dry_send()` |
| `patina_send` | Q15 hex | `spu94_get_patina_send()` | `spu94_set_patina_send()` |
| `reverb_fader` | Q15 hex | `spu94_get_reverb_fader()` | `spu94_set_reverb_fader()` |
| `latency_comp` | Boolean (`0`/`1`) | `spu94_get_latency_comp()` | `spu94_set_latency_comp()` |

### [dac] Section -- 4 fields
| Key | Type | Accessor (save) | Accessor (load) |
|-----|------|-----------------|------------------|
| `dac_enabled` | Boolean (`0`/`1`) | `spu94_get_dac_enabled()` | `spu94_set_dac_enabled()` |
| `dac_fir_enabled` | Boolean | `spu94_get_dac_fir_enabled()` | `spu94_set_dac_fir_enabled()` |
| `dac_noise_enabled` | Boolean | `spu94_get_dac_noise_enabled()` | `spu94_set_dac_noise_enabled()` |
| `dac_true_oversample` | Boolean | `spu94_get_dac_true_oversample()` | `spu94_set_dac_true_oversample()` |

### NOT Serialized (D-06)
| Field | Reason |
|-------|--------|
| `adpcm_enabled` | Always-on infrastructure; patina_fader/patina_send control audibility |
| `adpcm_buf_pos`, `adpcm_in/out_buf_*`, `adpcm_state_*` | Transient DSP state, not user configuration |
| `buffer_address` | Derived from mBASE + tick advancement; not a user-settable parameter |
| `pending_mask`, `pending_values[]` | Transient write-policy state; resolves on next tick |
| `err_*`, `overflow_*`, `oob_tap_count` | Diagnostic counters, not configuration |
| FIR state (`fir_delay_*`, `fir_idx_*`, `fir_*_phase`) | Transient DSP state |
| DAC module state (`dac_fir_l/r`, `dac_noise_l/r`) | Transient DSP state |
| `work_buf`, `work_buf_size` | Caller-owned memory; not part of preset |

**Total serialized fields: 46** (35 registers + 7 mixer + 4 DAC)

## Buffer Sizing Analysis

[VERIFIED: calculated from field inventory]

Worst-case line lengths:
- `version=1\n` = 10 bytes
- `name=` + 64 chars max + `\n` = 70 bytes
- `description=` + 256 chars max + `\n` = 269 bytes
- Blank separator lines: 3 bytes
- `[registers]\n` + comment line: ~55 bytes
- 35 register lines at max `mRCOMB1=0xFFFF\n` = 35 * 18 = 630 bytes
- `[mixer]\n` + comment line: ~55 bytes
- 7 mixer lines at max `patina_fader=0x7FFF\n` = 7 * 22 = 154 bytes
- `[dac]\n` + comment line: ~40 bytes
- 4 DAC lines at max `dac_true_oversample=1\n` = 4 * 22 = 88 bytes
- Null terminator: 1 byte

**Total worst case: ~1375 bytes** (with max-length name and description)

**Recommendation:** `#define SPU94_PRESET_BUF_SIZE 4096u` -- a round power-of-two that comfortably exceeds the worst case and leaves room for future field additions without a version bump. Matches the generous sizing philosophy used for `SPU94_STATE_SIZE_MAX` (16384 bytes for a ~2KB struct).

## Build Integration

[VERIFIED: `src/spu94/CMakeLists.txt`]

The new source file must be added to the `spu94_obj` OBJECT library target:

```cmake
# In src/spu94/CMakeLists.txt, add to the add_library(spu94_obj OBJECT ...) list:
    spu94_preset_io.c
```

This ensures the serialization code is compiled once and linked into both `spu94_shared` and `spu94_static` targets, consistent with every other source file in the library.

## Grep-Guard Constraints

[VERIFIED: `scripts/ci/grep-guard.sh`]

The new `spu94_preset_io.c` lives under `src/spu94/` and is subject to Tier 1 (core) grep-guard rules:

| Forbidden | Alternative |
|-----------|-------------|
| `float`, `double` | Not needed; all values are integer |
| `malloc`, `calloc`, `realloc`, `free` | Caller-provides-buffer pattern |
| Unqualified `long` | Use `int32_t` for `strtol` result, or hand-roll hex parser |

`snprintf`, `strcmp`, `strncmp`, `strlen`, `memcpy` are all permitted (not in the forbidden list). `<stdio.h>` and `<string.h>` are permitted includes.

**The `strtol` / `long` issue** deserves special attention. `strtol` returns `long`, but storing its result requires a `long` variable. Options:

1. **Hand-rolled hex parser** (recommended): A `parse_hex_u16()` function that processes exactly 4 hex digits after an optional `0x` prefix, returning `uint16_t`. ~15 lines of code, no `long` anywhere, trivially correct for the fixed format.

2. **`strtol` with immediate cast**: `int32_t val = (int32_t)strtol(str, &end, 16);` -- the `long` appears only as a transient return type, not as a declared variable. Whether grep-guard catches this depends on whether `strtol` is in the source text (grep-guard scans source, not headers). Since `strtol` itself doesn't match `\blong\b`, this may pass -- but the `long` implicit in the function signature doesn't appear in the source line.

**Recommendation:** Use approach 1 (hand-rolled hex parser). It's trivial for a known-width format, avoids the `long` question entirely, and matches the codebase's preference for explicit integer-width control.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Name field capped at 64 chars, description at 256 chars | Buffer Sizing Analysis | Buffer could overflow if uncapped; mitigated by SPU94_PRESET_BUF_SIZE having 2.5x headroom |
| A2 | `snprintf` does not introduce heap-allocator symbols into the link closure on Linux/GCC | Grep-Guard Constraints | Would fail `test_no_heap.sh`; can be verified by linking and running `nm -u` |
| A3 | Save function return type is `int` (bytes written or negative error) rather than `spu94_result_t` | Pattern 1 | If return type should be `spu94_result_t`, the bytes-written semantic needs a separate out-parameter |

## Open Questions

1. **Name/description length limits**
   - What we know: The format includes `name=` and `description=` metadata fields (D-04). No explicit length limit was discussed.
   - What's unclear: Should the API enforce a maximum length? Truncate silently? Return an error?
   - Recommendation: Cap `name` at 64 chars and `description` at 256 chars in the save function. Truncate silently (or return error). The buffer sizing math assumes these limits. Document in the header comment.

2. **Save return type convention**
   - What we know: Most library functions return `spu94_result_t`. But the save function naturally wants to return bytes-written (an int).
   - What's unclear: Should save return `int` (bytes written, negative on error) or `spu94_result_t` (with a separate `size_t *bytes_written` out parameter)?
   - Recommendation: Return `int` (bytes written). This is the standard C `snprintf`-family convention and is more ergonomic for callers who need to know the output size. The existing `spu94_result_t` enum doesn't have codes for "buffer too small for output" (distinct from `SPU94_WORK_BUF_TOO_SMALL` which is about the reverb work buffer).

3. **Factory preset serialization**
   - What we know: CONTEXT.md lists this under Claude's Discretion.
   - What's unclear: Whether to add a helper that exports the 10 factory presets as .spu94 text.
   - Recommendation: Defer to Phase 14 (CLI). The `preset-dump` subcommand can call `spu94_load_preset(state, id)` then `spu94_preset_save(state, ...)` to export any factory preset. No special Phase 13 API needed.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (vendored) |
| Config file | `tests/unit/preset/CMakeLists.txt` |
| Quick run command | `ctest --test-dir build -L preset -j4` |
| Full suite command | `ctest --test-dir build -j$(nproc)` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PRE-01 | Save writes all 46 fields to buffer | unit | `ctest --test-dir build -R preset_save_all_fields -j1` | Wave 0 |
| PRE-02 | Load restores all 46 fields from buffer | unit | `ctest --test-dir build -R preset_load_all_fields -j1` | Wave 0 |
| PRE-03 | Version header present in output | unit | `ctest --test-dir build -R preset_version_header -j1` | Wave 0 |
| PRE-04 | Round-trip bit-identical (save->load->compare) | unit | `ctest --test-dir build -R preset_roundtrip -j1` | Wave 0 |
| PRE-05 | Output is plain text, human-readable | unit | `ctest --test-dir build -R preset_format_check -j1` | Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -L preset -j4`
- **Per wave merge:** `ctest --test-dir build -j$(nproc)`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/preset/test_preset_roundtrip.c` -- covers PRE-04 (round-trip fidelity), also exercises PRE-01 through PRE-03
- [ ] `tests/unit/preset/test_preset_parse.c` -- covers PRE-02 edge cases (missing keys/D-08, unknown keys/D-09, comments, blank lines, malformed values)
- [ ] `tests/unit/preset/CMakeLists.txt` -- add new test targets (existing file, needs 2 new `add_executable` + `add_test` blocks)

## Sources

### Primary (HIGH confidence)
- `include/spu94/spu94.h` -- Public API, all accessor declarations, result codes, state sizing
- `include/spu94/spu94_registers.h` -- Register enum (35 entries), `spu94_reg_name()`, `spu94_reg_type()`, `spu94_snapshot_registers()`
- `src/spu94/spu94_state_internal.h` -- Complete struct layout defining all serializable fields
- `src/spu94/spu94_presets.c` -- Factory preset loader pattern, register iteration, type dispatch
- `src/spu94/spu94_registers.c` -- Register name table (35 bare names), signedness classification
- `scripts/ci/grep-guard.sh` -- Tier 1 core forbidden tokens: float, double, malloc, calloc, realloc, free, unqualified long
- `tests/rt_safety/test_no_heap.sh` -- No-heap symbol verification for libspu94.so

### Secondary (MEDIUM confidence)
- `nm -u build/src/spu94/libspu94.so` -- Confirmed undefined symbols: memcpy, memset, strlen (no stdio symbols currently)
- Buffer sizing calculation -- derived from field inventory and max line lengths

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no external libraries, all building blocks verified in codebase
- Architecture: HIGH -- pattern follows established `spu94_load_preset` + caller-provides-buffer conventions exactly
- Pitfalls: HIGH -- grep-guard and no-heap constraints verified against actual CI scripts; signedness issue is a known C gotcha

**Research date:** 2026-05-01
**Valid until:** 2026-06-01 (stable -- pure C library, no external dependency drift)
