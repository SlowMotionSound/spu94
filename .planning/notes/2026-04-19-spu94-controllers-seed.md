---
date: "2026-04-19"
promoted: false
type: future-project-seed
---

# SPU-94 Controllers — exploration layer for gray-area levers

Future late-stage milestone within this project (same repo). SPU-94 core reproduces the PS1 reverb bit-faithfully. **SPU-94 Controllers** is where the locked-in gray-area choices become performance controls — the control-layout / schema / musical-feel exploration layer, refined iteratively until it "feels right."

## Rationale (from 2026-04-19 Phase 2 discussion)

Every gray-area decision in SPU-94's core (register write timing, Q15 rounding direction, buffer wrap math, mBASE side effects, saturation policy, error-carry behavior, etc.) is internally structured as a **pinnable seam** — SPU-94 pins each seam to the hardware-faithful answer; Controllers unpins them and exposes them as knobs. Same code path, different policy tables.

This architectural commitment is what lets both projects live without conflict:
- **SPU-94** remains bit-faithful. Witness diffs against hardware captures and GPL-avoided emulator outputs remain meaningful.
- **Controllers** consumes the same SPU-94 API and mutates the policy tables the core exposes. It does not re-implement anything; it re-configures.

## Natural contents

- **Error Accumulator** (see `error-accumulator.md` at repo root) — quantization-error carry as a controlled parameter. Uses the `q15_mul_truncate_with_err` tap + `spu94_tick()` entry point already locked into Phase 2.
- **Register write timing** — the per-register immediate/tick-latched table, exposed as editable.
- **mBASE side-effect policy** — whatever SPU-94 locks in, Controllers can swap.
- **Filter topology experiments** — "what if APF1 and APF2 swap?" "what if comb tap order changes?"
- **Q15 rounding direction swap** — hear what the PS1's exact choice contributes vs alternatives.
- **Saturation policy swap** — hard-clip vs wrap vs soft-knee.
- **Inertial rotor hardware interface** — velocity-as-input control surface concept from `error-accumulator.md`.

## Critical invariant

SPU-94 core **must remain bit-faithful**. Controllers is a **consumer**, not a modifier, of the core API's seams. If Controllers ever requires a change to SPU-94 core behavior, either (a) the change must be a no-op on the bit-faithful path, or (b) Controllers is asking for something out of scope and must build its own adaptor layer.

## Timing

- **Not M1.** M1 is SPU-94 core + Python bindings + CLI only.
- **Scheduled slot:** late-stage milestone in this project (post-M4 or M5, after the plugin layer exists as scaffolding). Same repo, same planning structure.
- **Mode of work:** iterative refinement — try control schemas, play with them, keep what feels right, discard what doesn't. Not a specification-first phase. Success criterion is subjective-musical, not diff-against-witness.
- **Action item for ROADMAP.md:** add a Controllers milestone at M5 or similar with the exploration brief attached.

## Related seeds

- `error-accumulator.md` (repo root) — the algorithm + hardware concept brief that triggered this framing.
- `2026-04-18-prune-unused-scaffold.md` — unrelated cleanup note.
