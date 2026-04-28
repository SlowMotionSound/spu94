---
phase: 05-interpolation-filter-design
reviewed: 2026-04-28T18:00:00Z
depth: standard
files_reviewed: 2
files_reviewed_list:
  - tools/dac_filter_design.py
  - docs/DECISIONS.md
findings:
  critical: 0
  warning: 3
  info: 2
  total: 5
status: issues_found
---

# Phase 5: Code Review Report

**Reviewed:** 2026-04-28T18:00:00Z
**Depth:** standard
**Files Reviewed:** 2
**Status:** issues_found

## Summary

Reviewed the AK4309 8x interpolation filter design tool (`tools/dac_filter_design.py`) and the new ADR-0054 entry in `docs/DECISIONS.md`. The filter design is technically sound -- Parks-McClellan half-band cascade meets all AK4309B datasheet specs with margin, the half-band zero enforcement is correct, Q15 quantization is properly verified, and the composite upsampling math checks out. All verification assertions pass.

Three warnings found: the C export emits unsigned hex literals for negative `int16_t` values (implementation-defined behavior), the interpolation gain convention (gain=1 vs gain=2 per stage) is undocumented for downstream C consumers, and the `--plot` command writes to a relative path that depends on CWD. Two informational items on minor inconsistencies.

No critical/blocking issues. The design script fulfills its stated purpose as a Phase 5 deliverable.

## Warnings

### WR-01: C export uses unsigned hex literals for signed int16_t initializers

**File:** `tools/dac_filter_design.py:310`
**Issue:** Negative Q15 coefficients are exported as unsigned hex values (e.g., `-92` becomes `0xFFA4`) assigned to `int16_t` arrays. Per C99 6.7.8, initializing a signed integer with a value outside its representable range is implementation-defined behavior. While every real-world compiler handles this correctly via two's complement truncation, static analyzers and pedantic `-Wpedantic` builds may warn. Since these coefficients will be consumed by the Phase 6 C implementation, this could generate noise in CI.
**Fix:** For negative values, emit signed decimal or cast explicitly:
```python
# Option A: signed decimal for negatives, hex for non-negatives
if v < 0:
    row_item = f"{int(v)}"
else:
    row_item = f"0x{int(v):04X}"

# Option B: explicit cast (preserves hex readability)
row_item = f"(int16_t)0x{int(v) & 0xFFFF:04X}"
```

### WR-02: Interpolation gain convention not documented for C consumers

**File:** `tools/dac_filter_design.py:296-313`
**Issue:** The exported filter coefficients have DC gain of approximately 1.0 per stage (center tap = 0.5, coefficients sum to ~1.0). For a 2x interpolation filter applied after zero-stuffing, the signal amplitude drops by half at each stage, requiring a compensating gain of 2 per stage (or 8 total for the 3-stage cascade). Whether the C implementation needs this gain depends on the interpolation architecture: zero-stuff + convolve needs gain=2; polyphase decomposition typically handles gain internally. The exported header comment says "half-band FIR, Q15" but does not specify which convention the coefficients assume. Phase 6 will consume these coefficients, and an incorrect gain assumption would produce output at 1/8 amplitude (-18 dB).
**Fix:** Add a comment to the C export header documenting the gain convention:
```python
print("/* NOTE: Coefficients assume polyphase implementation (DC gain = 1.0 per stage).")
print(" * For zero-stuff + convolve, multiply output by 2 after each stage. */")
```

### WR-03: Plot output path is relative to CWD

**File:** `tools/dac_filter_design.py:290-291`
**Issue:** `cmd_plot()` writes to `plots/dac_interpolation_response.png` relative to the current working directory. If the script is invoked from a directory other than the project root (e.g., `python3 ../tools/dac_filter_design.py --plot`), the `plots/` directory is created in the wrong location. The `--verify` and `--export-c` commands write to stdout and are unaffected.
**Fix:** Resolve relative to the script's own directory:
```python
script_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.dirname(script_dir)
plots_dir = os.path.join(project_root, 'plots')
os.makedirs(plots_dir, exist_ok=True)
fig.savefig(os.path.join(plots_dir, 'dac_interpolation_response.png'), dpi=150, bbox_inches='tight')
```

## Info

### IN-01: measure_stage does not normalize attenuation to DC gain

**File:** `tools/dac_filter_design.py:76-81`
**Issue:** `measure_stage()` computes stopband attenuation from raw `H_db` without normalizing to DC gain, while `verify_cascade()` (line 113) normalizes via `H_db_norm = H_db - dc_gain`. The discrepancy is ~0.01 dB for these filters (negligible), but the inconsistency between the two measurement functions could confuse future maintainers.
**Fix:** Normalize in `measure_stage` for consistency:
```python
dc_gain = H_db[0]
H_db_norm = H_db - dc_gain
```

### IN-02: Numpy arrays returned in verify_cascade results dict

**File:** `tools/dac_filter_design.py:128-139`
**Issue:** `verify_cascade()` returns `w` and `H_db_norm` (large numpy arrays) in the results dict alongside scalar measurements. This is used by `cmd_plot()` but is unnecessary baggage for `cmd_verify()`. Minor coupling -- the function serves double duty as both a measurement function and a data source for plotting.
**Fix:** No action needed for Phase 5. If the function grows more consumers, consider splitting measurement from data retrieval.

---

_Reviewed: 2026-04-28T18:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
