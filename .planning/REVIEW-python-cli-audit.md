# Python / CLI / Scripts Audit

**Audited:** 2026-05-07
**Scope:** Python bindings, C CLI, scripts, tools
**Files reviewed:** 28

---

## Bugs

### BUG-01: `reverb.py` `_reg_type` return type annotation lies

**File:** `python/spu94/reverb.py:137`
**Severity:** BUG

The method signature says `-> int` but it returns a tuple `(reg_type, reg_int)`:

```python
def _reg_type(self, reg) -> int:
    ...
    return _lib.spu94_reg_type(reg_int), reg_int  # returns tuple, not int
```

Callers at lines 162, 170, 179 correctly destructure it as `reg_type, reg_int = self._reg_type(reg)`, so this does not produce wrong behavior at runtime. But any future caller trusting the annotation will get a tuple where they expected an int.

**Fix:** Change annotation to `-> tuple[int, int]` or `-> Tuple[int, int]`.

---

### BUG-02: `cmd_adpcm.c` `encode_channel` truncates `data_size` to `uint32_t`

**File:** `src/cli/cmd_adpcm.c:48`
**Severity:** BUG

```c
uint32_t data_size = (uint32_t)(num_blocks * SPU94_ADPCM_BLOCK_BYTES);
```

`num_blocks` is `uint64_t`. If the input WAV has more than ~7.6 billion frames (`UINT32_MAX / 16 * 28` = ~7.5 billion), the cast silently truncates `data_size`, writing a corrupt VAG header. The file would decode incorrectly.

In practice, a 7.6B-frame WAV at 44.1 kHz is ~48 hours of audio, so this is unlikely to trigger. But the code does not reject oversized input earlier, and `num_frames` comes from `dr_wav` which uses `uint64_t`.

**Fix:** Add a guard before the cast:

```c
if (num_blocks > (uint64_t)UINT32_MAX / SPU94_ADPCM_BLOCK_BYTES) {
    /* data_size would overflow uint32_t */
    return -1;
}
```

---

### BUG-03: `cmd_preset_dump.c` ignores `fwrite` return value

**File:** `src/cli/cmd_preset_dump.c:143-150`
**Severity:** BUG

```c
fwrite(buf, 1, (size_t)written, stdout);
...
fwrite(buf, 1, (size_t)written, fp);
```

Neither call checks the return value. A full disk or broken pipe silently swallows the output and the command returns 0 (success). The CLI's error discipline (D-05) requires every error path to exit non-zero.

**Fix:** Check `fwrite` return value and exit 1 on short write.

---

### BUG-04: `api.py` imports constants it never uses in code paths

**File:** `python/spu94/api.py:38-39`
**Severity:** DEAD_CODE

`SPU94_CLAMPED` and `SPU94_UNKNOWN_REG` are imported from `_binding` but never referenced in any executable code path in `api.py`. They appear only in the import statement. They are mentioned in docstrings but never compared against or returned. This is harmless but violates the "no dead imports" convention.

Similarly, `SPU94_PRESET__COUNT` (line 35) and `SPU94_LATENCY_SAMPLES` (line 33) are imported but never used in executable code -- `SPU94_LATENCY_SAMPLES` is not used at all, and `SPU94_PRESET__COUNT` is not used.

**Fix:** Remove unused imports or move them to `__init__.py` where they are re-exported.

---

## Dead Code

### DEAD-01: `_REJECT_METACHARS` regex is declared but never used

**File:** `scripts/ci/check_coverage.py:53`
**Severity:** DEAD_CODE

```python
_REJECT_METACHARS = r"[;&|><$`\n\s]"  # noqa: E501,W605
```

The comment says it is "not used directly by the check" and exists for documentation purposes. The actual validation uses the `_VALID_TEST_NAME` positive allowlist regex. This is a dead variable occupying namespace. The `noqa` comments suppress two linter warnings on a line that does nothing.

**Fix:** Delete the line or move the pattern into the docstring/comment of `_reject_if_metachars`.

---

### DEAD-02: `DAC_INPUTS` is identical to `INPUTS` in `regenerate_goldens.py`

**File:** `scripts/regenerate_goldens.py:85`
**Severity:** REDUNDANT

```python
INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]
DAC_INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]
```

These are byte-for-byte identical. `DAC_INPUTS` exists as a separate constant but has the same value as `INPUTS`. The comment says "Phase 9 D-02: DAC inputs -- same 5 as the reverb corpus" which confirms the redundancy is known.

**Fix:** Replace references to `DAC_INPUTS` with `INPUTS`, or alias `DAC_INPUTS = INPUTS`.

---

## Redundancy

### REDUNDANT-01: `locate_cli()` duplicated in 4 files

**Files:**
- `scripts/regenerate_goldens.py:136`
- `scripts/ci/witness_diff.py:545` (named `locate_spu94_cli`)
- `tools/dac_compare.py:35`
- `tools/dac_measure.py:56`

**Severity:** REDUNDANT

Four nearly identical copies of the same 3-step CLI binary resolution logic:
1. `$SPU94_BIN` env var
2. `build/src/cli/spu94` dev tree path
3. Bare `"spu94"` on PATH

All four have the same body. The witness_diff version differs only in name (`locate_spu94_cli` vs `locate_cli`).

**Fix:** Extract to a shared utility module (e.g. `scripts/_common.py`) and import from there. The scripts are standalone executables, not library code, so a relative import or `sys.path` insert is appropriate.

---

### REDUNDANT-02: `generate_input()` duplicated between `regenerate_goldens.py` and `witness_diff.py`

**Files:**
- `scripts/regenerate_goldens.py:102`
- `scripts/ci/witness_diff.py:458`

**Severity:** REDUNDANT

Two independent implementations of the same standard input generator. The witness_diff version adds trailing pad zeros and lacks the `chirp` input case, but the core generation logic (same seed, same amplitude, same signal shapes) is copy-pasted. Both share the same constants (`AMP=16000`, `NOISE_SEED=0x1094_DADA`, etc.).

**Fix:** Extract shared input generation into a common module. The witness_diff version can call it with a `pad_samples` parameter.

---

### REDUNDANT-03: `SPU94_ERROR` macro duplicated across 4 CLI source files

**Files:**
- `src/cli/main.c:19`
- `src/cli/cmd_reverb.c:34`
- `src/cli/cmd_adpcm.c:20`
- `src/cli/cmd_preset_dump.c:20`

**Severity:** REDUNDANT

The same 4-line macro is copy-pasted into each translation unit. The comment in `cmd_preset_dump.c` acknowledges this: "Each CLI TU carries its own copy -- no shared macro header."

**Fix:** Move to a shared `cli_common.h` header.

---

### REDUNDANT-04: `render_golden` / `render_adpcm_golden` / `render_dac_golden` are near-identical

**File:** `scripts/regenerate_goldens.py:153-264`
**Severity:** REDUNDANT

Four render functions share 90% identical structure (generate input, write WAV, build env dict, call subprocess, check return code). They differ only in the CLI flags passed:
- `render_golden`: `--preset <name>`
- `render_adpcm_golden`: `reverb --adpcm --preset <name>`
- `render_dac_golden`: `reverb --dac --preset <name>`
- `render_dac_isolated`: `reverb --dac --preset off`

**Fix:** Extract a single `_render(preset, input_name, out_path, tmp_in_path, spu94_bin, extra_flags=None)` function and call it from thin wrappers.

---

### REDUNDANT-05: `_reg_type` in `reverb.py` duplicates `_coerce_reg` in `api.py`

**Files:**
- `python/spu94/reverb.py:137-155`
- `python/spu94/api.py:350-366`

**Severity:** REDUNDANT

Both functions perform the same Register/bool/int/str coercion logic with identical isinstance chains and identical bool-guard logic. `_reg_type` adds one extra step (calling `_lib.spu94_reg_type`), but the coercion itself is duplicated.

**Fix:** Have `_reg_type` call `api._coerce_reg(reg)` internally instead of reimplementing the same chain.

---

## Simplification

### SIMPLIFY-01: Fader validation in `cmd_reverb.c` is 6 identical copy-paste blocks

**File:** `src/cli/cmd_reverb.c:226-285`
**Severity:** SIMPLIFY

Six consecutive option cases (1005-1010) each repeat:
```c
char *endptr;
double val = strtod(optarg, &endptr);
if (endptr == optarg || *endptr != '\0' || val < 0.0 || val > 1.0) {
    SPU94_ERROR("invalid value for --<name>: '%s' (accepts 0.0 to 1.0)", optarg);
    return 2;
}
fader_<name> = val;
```

This is 60 lines of boilerplate that could be a single helper function.

**Fix:** Extract a `static int parse_fader(const char *optarg, const char *name, double *out)` helper and call it from each case.

---

### SIMPLIFY-02: `dac_isolated` check/gen loops inline what `_check_loop`/`_generate_loop` do

**File:** `scripts/regenerate_goldens.py:438-483`
**Severity:** SIMPLIFY

The DAC isolated check and generation loops (lines 438-483) manually duplicate the same SHA-256 sidecar read/verify/write logic that `_check_loop` and `_generate_loop` already provide. The only reason they cannot use those helpers is that `render_dac_isolated` takes 4 args (no preset) while the other render functions take 5 (with preset).

**Fix:** Make `render_dac_isolated` accept a dummy `preset` arg (ignored), or restructure `_check_loop` / `_generate_loop` to accept a preset list of `[None]` for single-preset modes.

---

## Stale Ideas

### STALE-01: No stale macro/crossfade/morph/dual-engine references found

**Severity:** (none)

Grep for `macro`, `crossfade`, `dual.engine`, `morph`, `interpolat` across all audited files returned zero hits related to abandoned approaches. The `interpolat` hits are all from `dac_filter_design.py` and refer to the current DAC interpolation filter design (active code). The codebase appears clean of abandoned-approach debris in the audited files.

---

## Quality Notes

### NOTE-01: `_binding.py` docstring claims numpy is not imported, but it is

**File:** `python/spu94/_binding.py:23-24`

The module docstring says: "Plan 1 must not import numpy -- keeping the package dependency-free for consumers who only need reflection / presets / register IO." However, lines 40-41 unconditionally import numpy:

```python
import numpy as np
from numpy.ctypeslib import ndpointer
```

The docstring is outdated -- it describes the Plan 1 intent, but Plan 2 landed the numpy dependency. The docstring also says "Plan 2 overrides those four argtype entries with numpy.ctypeslib.ndpointer once the numpy dependency is admitted" (line 20-22), but the ndpointer argtypes are already set in this file (lines 148-166).

**Severity:** STALE (docstring misleads about current module behavior)

**Fix:** Update the docstring to reflect current state: numpy IS imported and ndpointer argtypes ARE set in this file.

---

### NOTE-02: `reverb.py` `load_preset` docstring says "Returns `SPU94_UNKNOWN_REG`" but the actual error code for bad preset IDs is `SPU94_INVALID_ARG`

**File:** `python/spu94/reverb.py:134`

```python
def load_preset(self, preset) -> int:
    """Load one of the 10 factory presets. Accepts Preset enum,
    case-insensitive string name, or int id. Returns
    ``SPU94_OK`` / ``SPU94_UNKNOWN_REG``."""
```

`SPU94_UNKNOWN_REG` is the error code for unknown register names, not preset IDs. The actual error for out-of-range preset is `SPU94_INVALID_ARG` (per `api.py:302`).

**Severity:** BUG (incorrect documentation that will mislead callers checking return values)

**Fix:** Change docstring to `Returns SPU94_OK / SPU94_INVALID_ARG`.

---

### NOTE-03: `check_coverage.py` `in_table` variable is declared but never read

**File:** `scripts/ci/check_coverage.py:96`

```python
in_table = False
```

This variable is assigned on line 96 but never referenced anywhere in the function. Section tracking uses `in_recognized` and `in_known_gaps` instead.

**Severity:** DEAD_CODE

**Fix:** Remove the `in_table = False` declaration.

---

## Summary

| Category | Count |
|----------|-------|
| BUG | 4 (BUG-01 through BUG-04) |
| DEAD_CODE | 3 (DEAD-01, DEAD-02, NOTE-03) |
| REDUNDANT | 5 (REDUNDANT-01 through REDUNDANT-05) |
| SIMPLIFY | 2 (SIMPLIFY-01, SIMPLIFY-02) |
| STALE | 1 (NOTE-01) |
| **Total** | **15** |

The codebase is well-structured and free of abandoned-approach debris. The main issues are code duplication across scripts (locate_cli, generate_input, render functions, SPU94_ERROR macro) and a handful of annotation/docstring bugs. The only runtime-impactful bugs are the unchecked `fwrite` in preset-dump and the theoretical `data_size` truncation in ADPCM encode.

---

_Reviewer: Claude (adversarial audit)_
_Depth: standard + cross-file redundancy analysis_
