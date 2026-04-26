# Phase 1: Foundation — Fixed-Point Math + Build Infrastructure - Context

**Gathered:** 2026-04-18
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 1 lays down the concrete bones every later phase depends on: the C project directory layout, the Q15 fixed-point helpers that the reverb algorithm will be built from, the deterministic CMake build, the CI guards that enforce the "no float, no malloc" discipline, and the `DECISIONS.md` log that records gray-area resolutions going forward.

**In scope:**
- Directory layout (C core, public headers, tests, Python binding location reserved for Phase 6)
- Q15 arithmetic helpers (`q15_mul_truncate`, `sat_s16`, and related) with hand-verified behavior on signed operands, `INT16_MIN`, and boundary values
- CMake build producing shared + static library artifacts on Linux with determinism flags locked in
- CI running clang-tidy, cppcheck, warnings-as-errors, and UBSan (with surgical `no_sanitize("integer")` annotations)
- CI grep guard that fails the build if core sources reference `float`/`double`/`malloc`/`calloc`/`realloc`/`free`/unqualified `long`
- `docs/DECISIONS.md` created with entries for Q15 multiply semantics and the vIIR = -0x8000 policy
- `LICENSE` placeholder noting MIT vs Apache-2.0 pick is deferred to end of M1

**Explicitly NOT in scope** (these belong to other phases):
- The reverb algorithm itself (Phase 3)
- SPU register state + buffer wrap math (Phase 2)
- FIR sample-rate converters (Phase 4)
- Python bindings, CLI, or any `spu94_process` end-to-end path (Phases 5–6)
- Any test framework work beyond what Phase 1 needs for Q15 helpers

</domain>

<decisions>
## Implementation Decisions

### Project Layout
- **D-01:** C core source lives at `src/spu94/` — standard `src/projectname/` convention, plays cleanly with CMake `add_subdirectory`.
- **D-02:** Public headers live at `include/spu94/` — library consumers write `#include <spu94/spu94.h>`. The `-Iinclude` separation is kept deliberately; it pays off when the M4 JUCE plugin consumes this library.
- **D-03:** Python bindings (Phase 6) are reserved at `python/spu94/` at the repo root — scikit-build-core picks this up without config, `pip install .` works out of the box. Not built in Phase 1; location locked now so CMake can be authored with this in mind.
- **D-04:** The file-tree-organizer scaffold directories (`agents/`, `prompts/`, `evaluation/`, `observability/`, `services/`, `security/`) remain in place through Phase 1 — a cleanup note is already captured in `.planning/notes/2026-04-18-prune-unused-scaffold.md`. Do **not** prune in Phase 1 work.

### Q15 Helper Shape
- **D-05:** Q15 helpers are **header-only, `static inline`**, shipped as part of the public API at `include/spu94/spu94_q15.h`. No separate `.c` file for hot-path ops.
- **D-06:** Naming is **verbose and self-documenting**: `q15_mul_truncate()`, `sat_s16()`, `q15_add_sat()`. The semantic suffix (truncate vs round, saturate vs wrap) is part of the name — matches REQUIREMENTS.md (CORE-01) wording and keeps intent visible at call sites.
- **D-07:** Rely on `static inline` and compiler optimization — **no `__attribute__((always_inline))`** or portability macro wrappers in Phase 1. Keeps the MCU cross-compile (BUILD-03 in Phase 8) clean and avoids MSVC-portability plumbing before it's earned.
- **D-08:** Q15 API is **public**, exposed in `spu94_q15.h`. Shipped as part of the library surface because these are fundamental ops unlikely to change, and consumers atop SPU-94 benefit from having them.

### Test Harness
- **D-09:** Phase 1 uses **Unity** as the C test framework. Small (~3 `.c` files vendored), zero runtime deps, widely used in embedded C — aligns with the Cortex-M target (BUILD-03, Phase 8).
- **D-10:** Hand-computed Q15 reference values are stored **inline in the test `.c` file** — tables of `{input_a, input_b, expected}` next to the assertions. Self-contained, auditable in code review, no fixture-loader plumbing.
- **D-11:** Test layout: **`tests/unit/q15/`** — `tests/unit/` for C unit tests organized by module. Scales cleanly as Phases 2–5 add `tests/unit/buffer/`, `tests/unit/registers/`, etc.

### DECISIONS.md Format
- **D-12:** **ADR-style per-entry** format — each resolved gray area is a numbered entry with structured sections: **Status**, **Context**, **Decision**, **Consequences**, **Sources**. Rigor pays off for the licensing-posture defense and for future contributors asking "why did you do X?"
- **D-13:** **Single file, grows over time** — `docs/DECISIONS.md`. New entries prepended at the top. Splits into a directory only if it exceeds ~2000 lines (defer that decision).
- **D-14:** Location is **`docs/DECISIONS.md`** (not `.planning/DECISIONS.md`). User-facing documentation root so library consumers and future contributors see it. Ships alongside future `LEVERS-CATALOG.md` and `BIBLIOGRAPHY.md`.

### Phase 1 Must Seed Two DECISIONS.md Entries
Per success criterion 5 in ROADMAP.md, Phase 1 resolves and records:
- **ADR-0001: Q15 multiply semantics** — `>> 15` direction and signed-shift policy (arithmetic vs logical, truncation toward `-∞` vs toward zero). Must be decided and implemented consistently in `q15_mul_truncate`.
- **ADR-0002: vIIR = -0x8000 policy** — the documented SPU anomaly that negates the final reverb result. Decision: reproduce faithfully (it is part of the sound, not a bug). Consequences and test coverage are recorded even though the anomaly test itself lands in Phase 3 (TEST-06).

### Claude's Discretion
Within the locked decisions above, the planner/executor has discretion on:
- Exact helper function signatures beyond the named ones (e.g., whether a `q15_abs` helper exists now or arrives in Phase 3)
- CMake target names and variable naming conventions
- Internal layout under `src/spu94/` (e.g., whether Q15 is its own subdirectory or a single `spu94_q15.h` / `spu94_q15_tests.c` pair at the `src/spu94/` root)
- Exact clang-tidy / cppcheck config field choices, as long as the success criteria (green CI, grep guard firing on forbidden tokens) hold
- Unity version pinning and vendor-vs-submodule choice (pin must be determinism-safe)
- `.gitignore` and dev-env niceties (editor configs, pre-commit scaffolding)

### Folded Todos
None — no pending todos matched Phase 1 scope.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Spec (internal)
- `.planning/PROJECT.md` — Core Value, principles (no float, no malloc, bit-faithfulness), licensing posture, current milestone goals
- `.planning/REQUIREMENTS.md` — v1 requirements; Phase 1 owns CORE-01, BUILD-01, BUILD-02, BUILD-04, BUILD-05, BUILD-07, DOCS-01, DOCS-05
- `.planning/ROADMAP.md` — Phase 1 success criteria verbatim; must all be TRUE to consider phase complete
- `.planning/research/SUMMARY.md` — Research synthesis (already integrated into PROJECT.md; re-read only if planner needs raw source citations)

### Origin and Historical Context
- `docs/origin/ps1-reverb-eurorack.md` — Original pre-GSD project brief. Banner notes it is superseded by PROJECT.md; read only for traceability on *why* SPU-94 exists, not as current spec.

### External References (paraphrased only — do NOT transcribe)
- **nocash PSX SPU documentation** — the primary authority for Q15 semantics, vIIR anomaly, and SPU register behavior. Licensing is ambiguous; SPU-94 paraphrases facts into `BIBLIOGRAPHY.md` (created in Phase 7). Phase 1 planner may consult nocash to resolve ADR-0001 and ADR-0002 but must write decisions in SPU-94's own words with source citations.
- **Sony PlayStation SDK docs** (where findable) — secondary witness on Q15 expectations.

**Not to be read as primary source:**
- Mednafen, lv2-psx-reverb, DuckStation, MiSTer source code — licensing-posture guard. Outputs may be used as witness comparisons (Phase 7, TEST-03), but source code is not a primary development input in Phase 1.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **None** — greenfield C codebase. No prior reverb, Q15, or CMake code in the repo.

### Scaffold State (from `file-tree-organizer 1.0.0`)
- `tests/`, `docs/`, `config/`, `utils/` — useful shells, can be populated or left as `.gitkeep` stubs
- `agents/`, `prompts/`, `evaluation/`, `observability/`, `services/`, `security/` — AI-agent-template artifacts that do not match SPU-94's shape; **do not touch in Phase 1** (cleanup is noted for post-M1)
- `PLAN.md`, `README.md`, `.env.example`, `main.sh` at repo root — auto-generated, likely to be rewritten or deleted; do not rely on their content

### Established Patterns
- **None yet** — Phase 1 is the first phase to write code; it establishes the patterns (CMake style, test framework integration, header layout) that later phases inherit.

### Integration Points
- Phase 2 (Buffer + Register Infrastructure) will consume `spu94_q15.h` for arithmetic and expect the CMake target to be linkable.
- Phase 6 (Python Binding + CLI) will expect `python/spu94/` to be a valid wheel-building location; CMake should be authored with `scikit-build-core` compatibility in mind (no `scikit-build-core` config *written* in Phase 1, but don't structure CMake in a way that blocks it later).
- Phase 7 (Verification) will append to `docs/DECISIONS.md` heavily; the format chosen in Phase 1 is the one it inherits.
- Phase 8 (MCU Cross-Compile) will re-use the CMake build with `arm-none-eabi-gcc`; keep the Linux-specific bits (shared library, dynamic linking) in a toolchain-conditioned block.

</code_context>

<specifics>
## Specific Ideas

- **Grep guard implementation hint:** A portable guard uses `grep -nE '\b(float|double|malloc|calloc|realloc|free)\b|\blong\b(?!\s+long)' src/ include/ --include='*.c' --include='*.h'` and fails if anything matches. `long long` is allowed; bare `long` is not (width is platform-dependent). The planner can refine.
- **UBSan surgical annotations:** Functions implementing documented SPU saturation or intentional wraparound should be annotated with `__attribute__((no_sanitize("integer")))` — UBSan otherwise flags them as bugs. The list of annotated functions should live in `docs/DECISIONS.md` as ADR-0003 (or later) so intent is traceable.
- **Determinism flags** (`-ffp-contract=off`, `-fno-fast-math`) are strictly speaking only relevant to float code, which we forbid. They're included anyway as defense-in-depth against accidental FP introduction and to make the "no float" stance explicit in the compile command.

</specifics>

<deferred>
## Deferred Ideas

### Not Raised in Phase 1 Discussion (flagged for future phases)
- **CI platform specifics** (GitHub Actions vs alternative) — not discussed; planner picks the default (likely GitHub Actions) unless the user says otherwise during planning.
- **Pre-commit hooks** (running grep guard locally, not just in CI) — useful but not required by any REQ; defer to post-M1 DX polish.
- **CMake structure nuances** (nested `CMakeLists.txt` vs top-level only) — planner decides; success criterion is "shared + static artifacts build with locked flags," not a specific CMake topology.
- **Git workflow** (branch strategy, commit signing) — out of scope for the project's technical work; not raised.

### Reviewed Todos (not folded)
None — no pending todos existed when Phase 1 discussion ran.

### Scope Creep Rejections
None raised during discussion.

</deferred>

---

*Phase: 01-foundation-fixed-point-math-build-infrastructure*
*Context gathered: 2026-04-18*
