# SPU-94 C Core Code Review — Milestone v1.0 Close-Out

**Scope reviewed:** all files in `src/spu94/*.c` (12 sources) and `include/spu94/*.h` (4 headers), plus internal headers `spu94_state_internal.h`, `spu94_reverb_internal.h`, `spu94_fir_internal.h`.

**Depth:** deep (cross-file analysis, trace call chains, verify contracts against implementation).

**Findings summary:** 1 critical, 4 high, 5 medium, 3 low, 2 nit.

---

## CRITICAL

### CR-01 — Reverb input path contradicts its own documentation; raw 44.1 kHz input feeds 22.05 kHz reverb body (silent correctness/quality issue)

**Files / lines:**
- `src/spu94/spu94_process.c:32-43`
- `src/spu94/spu94_io_chain.c:55-95` (comment block ADR-Phase-6-G)
- `src/spu94/spu94_reverb.c:606-615`

**Category:** API contract violation / logic error / silent-failure class.

**Description:**
`spu94_process` writes `state->mix_bus_l = l` with the **raw 44.1 kHz input sample** *before* calling `spu94_fir_chain_step`. Inside `chain_step_impl`, `spu94_fir_decimate` produces `dec_l` / `dec_r` (the 22.05 kHz band-limited sample), and on the retained phase `spu94_tick` fires, eventually running `spu94_reverb_body`, which reads `state->mix_bus_l` as the reverb input. At that moment `mix_bus_l` still holds the **raw input**, not `dec_l`.

Meanwhile, the comment block in `spu94_io_chain.c:56-68` explicitly states:

> "the dry decimator output (dec_l, dec_r) feeds the reverb INPUT via state->mix_bus_l/r (populated by spu94_process before each call ...)"

That is *not what the code does*. `dec_l` / `dec_r` are computed inside `chain_step_impl` and never written to `mix_bus_l/r`. They are used (only on the `reverb_active=0` test-bypass path) directly as the interpolator input.

Net effect on the production path:
1. The reverb body sees `l_in` at every retained-phase 44.1 kHz sample (= input value at the phase-0 tick), which is the **raw sample without anti-alias filtering**.
2. High-frequency input content aliases into the 22.05 kHz reverb network.
3. The 39-tap anti-alias decimator built in Phase 4 is effectively bypassed for the reverb input, used only to latch the sample-rate boundary phase counter.

This is exactly the silent-failure mode the user asked me to look for: no error, output is audible and "reverb-like," but the DSP contract is broken and the difference from the documented behavior is audible on bright content.

**Which side is wrong:** Cannot tell from code alone. Two possibilities:
- (a) Implementation is wrong — fix `spu94_io_chain.c` so the `mix_bus_l/r` write happens with `dec_l/dec_r` *between* `spu94_fir_decimate` and `spu94_tick`, not up in `spu94_process` with raw `l`.
- (b) Implementation is intended, documentation is wrong — a simplification where the reverb samples raw input at half-rate. In that case the comments in `spu94_io_chain.c:56-68` and the comment in `spu94_reverb.c:606-613` (which says "44.1 kHz input sample") need to be reconciled and the DSP design decision documented in an ADR.

**Suggested fix (if (a) is intended):**
```c
/* in chain_step_impl, after decimate, before the tick */
if (dec_valid) {
    if (reverb_active) {
        state->mix_bus_l = dec_l;
        state->mix_bus_r = dec_r;
        state->reverb_out_l = 0;
        state->reverb_out_r = 0;
        spu94_tick(state);
        ...
```
and remove the `mix_bus_l/r` writes from `spu94_process`.

**Why critical:** This is the exact pattern — audio is produced, no diagnostic fires, output sounds plausible, but the signal chain differs from both the documented design and any bit-faithful PS1 reference. M1 should not close with this ambiguity.

---

## HIGH

### HI-01 — No work_buf_size validation on preset load; small work_buf silently degrades audio

**Files / lines:**
- `src/spu94/spu94_presets.c:463-490` (`spu94_load_preset`)
- `include/spu94/spu94.h:134-135` (`spu94_init` contract)

**Category:** Silent failure / unvalidated assumption. **This is the same bug class as the Python default-value bug that triggered the audit.**

**Description:** `spu94_load_preset` and `spu94_init` perform zero validation that the caller's `work_buf_size` is large enough to hold the preset's reverb delay lines. The Hall preset's largest `m*` offset is `0x15BA` halfwords = 5562 halfwords = **11,124 bytes**. A caller who passes, e.g., 8192 bytes of `work_buf` gets:
- `spu94_init` succeeds (no size check).
- `spu94_load_preset` succeeds (no size check).
- `reverb_buf_read`/`reverb_buf_write` silently return 0 / discard on all out-of-range offsets (`src/spu94/spu94_reverb.c:54, 69`).
- Net result: partially working reverb tail, wrong audio, no diagnostic.

The `spu94_preset_t` table carries enough information to compute the minimum work_buf_size at load time (max of all `m*` and `d*` halfword fields × 2 bytes, plus room for `mBASE`). Nothing consumes that information.

**Suggested fix:**
1. Add `size_t spu94_preset_min_work_buf_size(spu94_preset_id_t id)` to the public API. Compute from the preset table at first call (cache in a 10-entry static array).
2. Make `spu94_load_preset` return a new code (e.g., `SPU94_WORK_BUF_TOO_SMALL` — new enum value, append-only per the D-07 contract) when `state->work_buf_size < required`. Do not mutate state on that path.
3. Publish `SPU94_WORK_BUF_MAX_BYTES` = maximum across all 10 presets so callers can statically size.

**Why high:** The Python binding bug the user referenced came from precisely this gap. Hardening at the C layer would have prevented it.

---

### HI-02 — `reverb_buf_read` / `reverb_buf_write` silently clamp when work_buf is smaller than computed byte offset

**Files / lines:**
- `src/spu94/spu94_reverb.c:48-73`

**Category:** Silent failure / unvalidated assumption.

**Description:** Both helpers wrap the halfword offset via `& 0x7FFFEu` (the 512KB-2 SPU RAM window), then check `(size_t)byte_off + 1u >= s->work_buf_size` and silently return 0 or discard on failure. This is consistent with the `spu94_init` contract that permits `work_buf_size < 0x80000`, but it means **every tap read falling outside the caller's buffer produces zero with no signal**. Combined with HI-01, a tiny work_buf produces an alive-but-wrong reverb. The Phase-3 design locked this as "defensive"; it should be revisited because it hides HI-01.

**Suggested fix:** Reclassify the out-of-range case as an error that sets a sticky flag in `state->overflow_magnitude` or a new `state->oob_tap_count` counter so tests and observability can surface the event. Callers can ignore the field; diagnostics can detect it. Purely additive — no API break.

---

### HI-03 — `spu94_init` accepts any non-zero `work_buf_size` including sizes that cannot hold a single halfword

**Files / lines:** `src/spu94/spu94_state.c:63-96`

**Category:** Unvalidated assumption.

**Description:** `spu94_init` validates `state_buf_size >= spu94_state_size()` and alignment, but the only check on `work_buf_size` is `work_buf == NULL && work_buf_size > 0 → NULL`. A caller passing `work_buf_size == 1` is accepted; every reverb tap subsequently returns zero. There is no minimum-useful-size gate.

**Suggested fix:** Either (a) document a `SPU94_WORK_BUF_MIN_BYTES` constant and reject smaller buffers, or (b) couple this to HI-01: accept any size at init, reject at `load_preset` when the specific preset cannot fit. Option (b) is more flexible.

---

### HI-04 — `spu94_load_preset(NULL, id)` returns `SPU94_OK` without any side effect

**Files / lines:** `src/spu94/spu94_presets.c:464-465`, `include/spu94/spu94.h:290-297`.

**Category:** Silent failure (explicitly documented but questionable).

**Description:** The header contract states: "SPU94_OK if state == NULL (lifecycle-null-safe convention)". Returning success for a no-op breaks the "if (r) handle_error(r)" idiom the result codes encourage. A caller who passes a NULL state by mistake gets a clean `SPU94_OK` and assumes the preset loaded.

The header compares `spu94_snapshot_registers(NULL, out)` where a NULL state *zeros the output* (`spu94_registers.c:130-134`) — semantically consistent with "no-op observation." But `load_preset` is a mutation; reporting OK for a no-op mutation is misleading. The destroy/reset null-safe convention is defensible (cleanup idempotence); `load_preset` is not cleanup.

**Suggested fix:** Return `SPU94_UNKNOWN_REG` (or a new `SPU94_INVALID_STATE`) on NULL. Matches the Phase 2 engine-layer set_reg* behavior exactly (those return `SPU94_UNKNOWN_REG` on NULL state).

---

## MEDIUM

### ME-01 — `spu94_init` does not zero the caller's work_buf

**Files / lines:** `src/spu94/spu94_state.c:63-96`.

**Category:** Unvalidated assumption / contract ambiguity.

**Description:** `spu94_reset` zeros work_buf (line 112-114); `spu94_init` does not. The header comment on `spu94_init` does not promise to zero it, but `spu94_reset` documents that it does. First-tick behavior is therefore undefined if the caller passes uninitialized memory: `reverb_buf_read` will return stale bytes. Static/BSS-allocated buffers are fine; heap-allocated buffers (Python binding, plugin host) are not.

**Suggested fix:** Either zero in `spu94_init` (one line: `spu94_zero_bytes(work_buf, work_buf_size)`), or add a prominent header note: "Caller must zero work_buf before `spu94_init` or call `spu94_reset` once to guarantee clean initial state." The former is cheaper than re-debugging stale-memory reverb in the next caller.

---

### ME-02 — `spu94_mbase_on_write` does not validate against `work_buf_size`

**Files / lines:** `src/spu94/spu94_buffer.c:82-88`.

**Category:** Silent failure.

**Description:** Snap-on-write sets `state->buffer_address = (uint32_t)new_mbase` verbatim. If the caller writes mBASE = 0xFFFF with a 4KB work_buf, every subsequent tap access outside [0x0, 4096) silently returns zero (HI-02). This is ADR-0006 bit-faithful behavior by design, but given the silent-failure audit frame, worth calling out that the reverb goes silent with no diagnostic until the advance formula wraps back into the caller's buffer bounds.

**Suggested fix:** Document in the header (`spu94.h` around the `spu94_load_preset` / mBASE commentary) that mBASE must be chosen such that the intended work-buffer region overlaps `[mBASE, work_buf_size)`, and consider a getter/validator `spu94_mbase_fits(state, mBASE, preset_id)`.

---

### ME-03 — `reverb_out_l/r = (int16_t)LeftOutput` implicit-narrowing cast is correct-by-reasoning, not by construction

**Files / lines:** `src/spu94/spu94_reverb.c:654-655`.

**Category:** Defensive-programming gap.

**Description:** `LeftOutput` is `int32_t`; it is guaranteed by `spu94_reverb_output_scale` to hold an already-int16-clamped value widened to int32 (`q15_mul_truncate_with_err` returns int16). An implicit narrowing cast to `int16_t` is therefore fine — today. A future edit that changes the output-scale signature (e.g., removes the pre-cast saturation to observe int32 range) will silently wrap to a garbage int16. The cost of adding `sat_s16(LeftOutput)` here is one function call inlined to nothing.

**Suggested fix:** `state->reverb_out_l = sat_s16(LeftOutput);` and same for R. Documents the invariant at the assignment site instead of four stages upstream.

---

### ME-04 — `spu94_reverb_output_scale` dereferences `state` without NULL check, unlike all sibling stages

**Files / lines:** `src/spu94/spu94_reverb.c:166-178`.

**Category:** Defensive consistency / null deref.

**Description:** Every other stage (`input_scale`, `same_iir`, `diff_iir`, `comb`, `apf1`, `apf2`, `reverb_body`) opens with `if (state == (spu94_state *)0) return;`. `output_scale` skips the guard and writes `state->err_output_scale +=`. Reachable only from `spu94_reverb_body`, which does check for NULL before calling, so dead defensively — but the *pattern* is inconsistent, and a future direct caller from a test TU would crash.

**Suggested fix:** Add the matching null guard for symmetry and future-proofing.

---

### ME-05 — `spu94_reverb_hard_clip` has no NULL checks on `Lin_out` / `Rin_out`

**Files / lines:** `src/spu94/spu94_reverb.c:134-159`.

**Category:** Defensive consistency.

**Description:** `comb` and `apf1`/`apf2` guard against NULL out-params; `hard_clip` does not. Currently only called from `reverb_body` with stack locals. A stray direct test caller passing NULL would SIGSEGV. Low blast radius.

**Suggested fix:** Add `if (Lin_out == NULL || Rin_out == NULL) return;` at the top. Keeps the defensive pattern uniform.

---

## LOW

### LO-01 — `spu94_reg_type` returns a valid enum value on out-of-range input

**Files / lines:** `src/spu94/spu94_register_io.c:65-74`.

**Category:** API contract ambiguity.

**Description:** Out-of-range `reg` returns `SPU94_REG_TYPE_I16` — indistinguishable from a legitimate classification. The header comment at `spu94_registers.h:100-103` correctly flags this as "arbitrary," but a caller iterating `for (int r = 0; r < 35; r++)` and passing the result to `spu94_set_reg_i16` has no way to detect an enum drift. Low concern because `spu94_load_preset` is the only internal iteration consumer and it respects `SPU94_REG__COUNT`.

**Suggested fix:** Consider adding a `SPU94_REG_TYPE_INVALID = -1` sentinel or an out-param result code. Not urgent.

---

### LO-02 — `spu94_apply_pending_writes` iterates only bits `[0, SPU94_REG__COUNT)` but the mask is uint64

**Files / lines:** `src/spu94/spu94_pending.c:36-42`.

**Category:** Resource cleanup edge case.

**Description:** Any bit set in `pending_mask` at position >= 35 is cleared at line 41 (`state->pending_mask = 0u`) but never applied. Today this is unreachable: the only writers (`spu94_set_reg_u16`) validate `reg` first. Future refactors could set high bits through a new path and silently lose them.

**Suggested fix:** Mask the iteration: `mask &= ((UINT64_C(1) << SPU94_REG__COUNT) - 1u);` before the loop, and then set `state->pending_mask &= ~mask;` (clears only the bits that were actually processed). Detects drift instead of silently absorbing it.

---

### LO-03 — Output-scale `int32` return surface is vestigial

**Files / lines:** `src/spu94/spu94_reverb.c:166-178`, header `spu94_reverb_internal.h:86-90`.

**Category:** Dead code / misleading signature.

**Description:** `spu94_reverb_output_scale` returns `int32_t *LeftOutput_out, *RightOutput_out`, but the values are guaranteed int16-range at line 176-177 (post-`q15_mul_truncate_with_err` + sat_s16). The only caller (`spu94_reverb_body`) widens to int32 then narrows to int16 at line 654-655. The int32 signature suggests observable overflow; there is none to observe post-saturation. Misleading for future readers.

**Suggested fix:** Change the out-params to `int16_t *`. One-line change; body compiles with identical semantics (assigning int16 to int16 instead of widening).

---

## NIT

### NT-01 — Phase counter in decimator assigns `fir_decimate_phase = 0` / `1` in non-obvious order
`src/spu94/spu94_fir.c:189-196`: on the discarded phase we set it to 0 (meaning "next call retains"). The naming is fine but a one-line comment "after this call, fir_decimate_phase == 0 means 'retained next'" next to the assignment would save the next reader five minutes.

### NT-02 — Several `(spu94_state *)0` casts could be plain `NULL`
`spu94_buffer.c:68, 83, 91`, `spu94_register_io.c:93, 113, 147, ...`, `spu94_reverb.c:51, 64, 112, ...`. Explicit casts are legal under freestanding C99 (`NULL` requires `<stddef.h>` which is freestanding-OK), but inconsistent with `spu94_process.c` / `spu94_presets.c` which use `NULL`. Pick one and apply uniformly.

---

## Files scanned
`include/spu94/spu94.h`, `include/spu94/spu94_q15.h`, `include/spu94/spu94_registers.h`, `include/spu94/spu94_register_facade.h`, `src/spu94/spu94_state.c`, `src/spu94/spu94_state_internal.h`, `src/spu94/spu94_buffer.c`, `src/spu94/spu94_register_io.c`, `src/spu94/spu94_registers.c`, `src/spu94/spu94_pending.c`, `src/spu94/spu94_write_policy.c`, `src/spu94/spu94_tick.c`, `src/spu94/spu94_reverb.c`, `src/spu94/spu94_reverb_internal.h`, `src/spu94/spu94_fir.c`, `src/spu94/spu94_fir_internal.h`, `src/spu94/spu94_fir_coef.c`, `src/spu94/spu94_io_chain.c`, `src/spu94/spu94_process.c`, `src/spu94/spu94_presets.c`.

---

## Recommendation for M1 close-out

**Block M1 close on:** CR-01 (reverb input path).  Either confirm implementation intent and reconcile the contradictory comments, or fix the routing so `dec_l/dec_r` feeds `mix_bus_l/r`. This affects every rendered sample produced by the library.

**Strongly recommend before M1 close:** HI-01 (preset vs. work_buf_size validation). It is the literal silent-failure class the user called out.  A minimal version (compute required size from preset table, return new result code on mismatch) is under ~40 lines.

**Deferrable to M2 or later:** HI-02, HI-03, HI-04, all MEDIUM and below. Worth a ticket each; none block correctness for a validated caller.
