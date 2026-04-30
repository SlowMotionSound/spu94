# Phase 10: Discussion Log

**Date:** 2026-04-30

## Areas Discussed

### 1. Implementation Approach

**Question:** Naive 8x (faithful to hardware, simpler code, 8x CPU) or polyphase (same output, complex code, 1x CPU)?

**Options presented:**
1. Naive 8x [RECOMMENDED] — Does what the hardware does. Simpler. Fast enough on desktop.
2. Polyphase from the start — Efficient, follows existing spu94_fir.c pattern. Phase-tracking complexity adds risk.
3. Naive first, then optimize — Build naive as production AND verification reference.

**Selected:** Naive 8x [RECOMMENDED]

**Notes:** Researchers disagreed (Stack said naive, Architecture + Pitfalls said polyphase). User chose simplicity and hardware fidelity. Polyphase deferred as future optimization if MCU budget requires it.

### 2. v1.2 Path Preservation

**Question:** How should v1.2's single-rate path survive the Phase 10 rewrite?

**Options presented:**
1. Rename + add new — Rename to _approx, add _8x
2. Keep original, add 8x alongside — Leave spu94_dac_fir_step untouched, add new

**Selected:** N/A — user noted both options are essentially the same. Locked as Claude's discretion.

**Notes:** Both paths stay available, process.c calls the new 8x version. Implementation detail.

### 3. Prototype Strategy

**Question:** Prototype the 8x cascade in Python/scipy first, or go straight to C?

**Options presented:**
1. Scipy first [RECOMMENDED] — Extend dac_filter_design.py, verify before porting
2. Straight to C — Simple enough, verify against existing tests

**Selected:** Scipy first [RECOMMENDED]

## Deferred Ideas

None.

## Claude's Discretion Items

- Accumulator overflow proof re-derivation
- Delay line dimensioning for 8x state
- Decimation sample selection (impulse test during prototype)
- State struct organization (extend existing vs new struct)
