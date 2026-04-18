# Phase 1: Foundation — Fixed-Point Math + Build Infrastructure - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-18
**Phase:** 01-foundation-fixed-point-math-build-infrastructure
**Areas discussed:** Project layout, Q15 helper shape, Test harness choice, DECISIONS.md format

---

## Project Layout

### Q1: Where should the C core source live?

| Option | Description | Selected |
|--------|-------------|----------|
| src/spu94/ (Recommended) | Standard src/projectname/ — clean namespace, matches typical C library convention, plays well with CMake add_subdirectory. | ✓ |
| core/ | Explicit 'core' naming. Matches the SPU-94 narrative but less conventional for C libs. | |
| lib/ | Traditional Unix layout. Mildly ambiguous (lib/ often means 'built artifacts' elsewhere). | |
| You decide | Claude picks. | |

**User's choice:** src/spu94/ (Recommended)

### Q2: Public header layout?

| Option | Description | Selected |
|--------|-------------|----------|
| include/spu94/spu94.h (Recommended) | Classic split: -Iinclude clean, #include <spu94/spu94.h>. Best for library consumption (M4 JUCE plugin). | ✓ |
| Public headers in src/spu94/ | One directory — public vs private distinguished by naming. Less clutter, messier -I flags downstream. | |
| include/ flat (no subdir) | #include <spu94.h>. Simpler, namespace collisions likely if more headers are added. | |

**User's choice:** include/spu94/spu94.h (Recommended)

### Q3: What to do with the unused scaffold dirs?

| Option | Description | Selected |
|--------|-------------|----------|
| Delete them now in Phase 1 | Clean slate before any real code. Removes 'what is agents/?' confusion. | |
| Keep, already noted for later | Earlier call — prune after M1 settles. Less churn in Phase 1. | ✓ |
| Delete some now, keep maybes | Delete obvious AI-agent-template ones, keep ambiguous ones. | |

**User's choice:** Keep, already noted for later

### Q4: Where will future Python bindings live?

| Option | Description | Selected |
|--------|-------------|----------|
| python/spu94/ at repo root (Recommended) | Standard Python wheel layout — scikit-build-core picks this up cleanly. | ✓ |
| bindings/python/ | Language-agnostic 'bindings' parent. Good if other language bindings later. | |
| src/spu94/python/ | Everything under src/. Tight C/Python coupling. | |
| Defer this decision to Phase 6 | Just pick C layout in Phase 1 and revisit. | |

**User's choice:** python/spu94/ at repo root (Recommended)

---

## Q15 Helper Shape

### Q1: Header-only static inline vs separate .c file?

| Option | Description | Selected |
|--------|-------------|----------|
| Header-only static inline (Recommended) | Classic hot-path DSP pattern. Zero call overhead. Full rebuild on helper changes. | ✓ |
| Separate .c file, normal linkage | Simpler debugging, call overhead per op unless LTO saves you. | |
| Mixed — hot ops inline, cold ops in .c | q15_mul/sat inline, formatting/debug in .c. More files, more cognitive load. | |

**User's choice:** Header-only static inline (Recommended)

### Q2: Naming convention for Q15 helpers?

| Option | Description | Selected |
|--------|-------------|----------|
| Verbose, self-documenting (Recommended) | q15_mul_truncate(), sat_s16(), q15_add_sat(). Explicit about semantics. | ✓ |
| Terse | q15_mul(), q15_sat(), q15_add(). Shorter, ambiguous semantics. | |
| Sony/SPU-style names | Mirror hardware/nocash terminology. Consistent with literature, less conventional for C. | |

**User's choice:** Verbose, self-documenting (Recommended)

### Q3: Force-inline policy on hot-path helpers?

| Option | Description | Selected |
|--------|-------------|----------|
| `static inline` only, trust the compiler (Recommended) | Portable C99. -O2 inlines aggressively. Clean MCU port. | ✓ |
| Force inline via __attribute__((always_inline)) | Guarantees inlining at -O0. GCC-specific; needs MSVC portability macro later. | |
| Force inline via a macro wrapper | Wrap attribute in SPU94_FORCE_INLINE. Most portable, slightly more plumbing. | |

**User's choice:** `static inline` only, trust the compiler (Recommended)

### Q4: Q15 helpers: public API or internal only?

| Option | Description | Selected |
|--------|-------------|----------|
| Public in spu94_q15.h (Recommended) | Ship helpers as part of public API. Consumers benefit. Low risk. | ✓ |
| Internal header, not installed | src/spu94/internal/q15.h, not exported. Minimal public surface. | |
| You decide | Claude picks. | |

**User's choice:** Public in spu94_q15.h (Recommended)

---

## Test Harness Choice

### Q1: Test framework for Phase 1 Q15 helpers?

| Option | Description | Selected |
|--------|-------------|----------|
| Unity (Recommended) | Small (~3 .c files), zero runtime deps, widely used in embedded C, aligns with Cortex-M target. | ✓ |
| cmocka | More features (mocks, parameterized). External dep, heavier for pure-math helpers. | |
| Plain ctest + C asserts | No framework. Each test is a small main() returning nonzero on failure. Simplest. | |
| Defer framework pick to Phase 2 | Phase 1 uses throwaway asserts; Phase 2 picks real framework. Risk of rewrite. | |

**User's choice:** Unity (Recommended)

### Q2: How should hand-computed Q15 reference values be stored?

| Option | Description | Selected |
|--------|-------------|----------|
| Inline in the test .c file (Recommended) | Tables of {input_a, input_b, expected} next to assertions. Self-contained, auditable. | ✓ |
| Separate JSON/CSV fixture file | Forces fixture loader in C. Reusable across C/Python tests later. Premature for Phase 1. | |
| Python-generated, committed header | Python script emits .h with static const tables. Reproducible, adds build-time dep. | |

**User's choice:** Inline in the test .c file (Recommended)

### Q3: Where do Phase 1 tests live in the repo?

| Option | Description | Selected |
|--------|-------------|----------|
| tests/unit/q15/ (Recommended) | Phase-agnostic structure, organized by module. Scales cleanly to Phases 2–5. | ✓ |
| tests/ flat | All tests in tests/ root. Simpler, messy by Phase 5. | |
| src/spu94/q15/tests/ | Tests co-located with source (Rust-ish). Some C projects use this; more CMake plumbing. | |

**User's choice:** tests/unit/q15/ (Recommended)

---

## DECISIONS.md Format

### Q1: Structural format?

| Option | Description | Selected |
|--------|-------------|----------|
| ADR-style per-entry (Recommended) | Numbered entries with Status/Context/Decision/Consequences/Sources. Defensible for licensing. | ✓ |
| Freeform dated log | Narrative prose under dated headings. Fast to write, harder to scan. | |
| Hybrid — ADR fields, informal tone | Keep ADR headers with narrative prose. Balance rigor vs speed. | |

**User's choice:** ADR-style per-entry (Recommended)

### Q2: One DECISIONS.md file or a directory of ADRs?

| Option | Description | Selected |
|--------|-------------|----------|
| Single file, grows over time (Recommended) | docs/DECISIONS.md with entries prepended at top. Matches DOCS-01 wording. | ✓ |
| Directory of per-ADR files | docs/decisions/ADR-0001-*.md. Cleaner git history per ADR, more fragmented. | |
| Single file now, split if it gets unwieldy | Pragmatic — split only if it exceeds ~2000 lines. | |

**User's choice:** Single file, grows over time (Recommended)

### Q3: Location for DECISIONS.md?

| Option | Description | Selected |
|--------|-------------|----------|
| docs/DECISIONS.md (Recommended) | User-facing docs root. Discoverable with LEVERS-CATALOG.md, BIBLIOGRAPHY.md. Ships with library. | ✓ |
| .planning/DECISIONS.md | Lives with GSD artifacts. Project-internal, less discoverable. | |
| Repo root DECISIONS.md | Very visible, clutters root. | |

**User's choice:** docs/DECISIONS.md (Recommended)

---

## Claude's Discretion

No areas were explicitly handed to Claude's discretion. However, within the decisions above, the planner retains discretion on:
- Exact CMake target names and variable conventions
- Internal subdirectory structure under `src/spu94/`
- Clang-tidy / cppcheck config field choices (success criteria govern, not specific fields)
- Unity version pinning and vendor-vs-submodule choice

## Deferred Ideas

- CI platform specifics (GitHub Actions vs alternative) — planner picks default during planning
- Pre-commit hooks for the grep guard — post-M1 DX polish
- Nested vs top-level CMakeLists.txt topology — planner decides
- Git workflow (branch strategy, commit signing) — out of scope

No scope-creep features raised during discussion.
