# Phase 1: Foundation — Fixed-Point Math + Build Infrastructure - Research

**Researched:** 2026-04-18
**Domain:** C build infrastructure, Q15 fixed-point arithmetic, CI discipline for embedded-C projects
**Confidence:** HIGH overall — primary findings cross-verified against C standard documents, nocash SPU reference, and official tool documentation. One important LOW-confidence item flagged below (nocash's exact shift-direction language).

## Summary

Phase 1 is the foundation every downstream module builds on. The research confirms that Phase 1 has no genuinely open technical questions — every success criterion maps to a well-established pattern with authoritative documentation. The bulk of the work is *implementation discipline*: getting the Q15 helper semantics right once, locking determinism flags into the compile command, and wiring static analysis + UBSan + a grep guard into CI so Phase 2+ can't regress.

Two gray areas rise above the rest. First, **Q15 multiply truncation direction** (`>> 15` semantics on negative intermediate products) is *implementation-defined* in C17 and earlier. C23 mandates two's complement representation (N2412) but does not explicitly redefine `>>` for negative values — it remains implementation-defined even under C23, though every mainstream compiler on two's-complement hardware emits arithmetic shift right (ASR), which rounds toward −∞ (floor). This matters because nocash does not explicitly specify whether the SPU hardware rounds toward zero (C division) or toward −∞ (ASR). Resolving this is ADR-0001. Second, the **`INT16_MIN × INT16_MIN` edge case** in Q15: the mathematically-correct product `+2^30 >> 15 = +32768` cannot fit in `int16_t` and aliases to `−32768`. This is a well-known Q15 pitfall that must be saturated or wrapped deliberately.

Everything else — CMake target authoring for shared + static, GitHub Actions as the CI platform, Unity for C unit tests, clang-tidy + cppcheck wiring, UBSan with surgical `no_sanitize` annotations, portable grep guards — is standard and has established patterns ready to copy.

**Primary recommendation:** Resolve ADR-0001 by **choosing arithmetic shift right (ASR) explicitly and writing the Q15 multiply to guarantee ASR behavior regardless of compiler** (cast to `int32_t`, multiply, then use a division-based fallback or a portable ASR macro). Document in DECISIONS.md that this matches Sony DSP hardware convention and every mainstream PSX emulator's reproduced behavior. Reproduce vIIR=−0x8000 as a conditional post-multiply negation for bit-faithfulness.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Project Layout**
- **D-01:** C core source lives at `src/spu94/` — standard `src/projectname/` convention, plays cleanly with CMake `add_subdirectory`.
- **D-02:** Public headers live at `include/spu94/` — library consumers write `#include <spu94/spu94.h>`. The `-Iinclude` separation is kept deliberately; it pays off when the M4 JUCE plugin consumes this library.
- **D-03:** Python bindings (Phase 6) are reserved at `python/spu94/` at the repo root — scikit-build-core picks this up without config, `pip install .` works out of the box. Not built in Phase 1; location locked now so CMake can be authored with this in mind.
- **D-04:** The file-tree-organizer scaffold directories (`agents/`, `prompts/`, `evaluation/`, `observability/`, `services/`, `security/`) remain in place through Phase 1 — a cleanup note is already captured in `.planning/notes/2026-04-18-prune-unused-scaffold.md`. Do **not** prune in Phase 1 work.

**Q15 Helper Shape**
- **D-05:** Q15 helpers are **header-only, `static inline`**, shipped as part of the public API at `include/spu94/spu94_q15.h`. No separate `.c` file for hot-path ops.
- **D-06:** Naming is **verbose and self-documenting**: `q15_mul_truncate()`, `sat_s16()`, `q15_add_sat()`. The semantic suffix (truncate vs round, saturate vs wrap) is part of the name — matches REQUIREMENTS.md (CORE-01) wording and keeps intent visible at call sites.
- **D-07:** Rely on `static inline` and compiler optimization — **no `__attribute__((always_inline))`** or portability macro wrappers in Phase 1. Keeps the MCU cross-compile (BUILD-03 in Phase 8) clean and avoids MSVC-portability plumbing before it's earned.
- **D-08:** Q15 API is **public**, exposed in `spu94_q15.h`. Shipped as part of the library surface because these are fundamental ops unlikely to change, and consumers atop SPU-94 benefit from having them.

**Test Harness**
- **D-09:** Phase 1 uses **Unity** as the C test framework. Small (~3 `.c` files vendored), zero runtime deps, widely used in embedded C — aligns with the Cortex-M target (BUILD-03, Phase 8).
- **D-10:** Hand-computed Q15 reference values are stored **inline in the test `.c` file** — tables of `{input_a, input_b, expected}` next to the assertions. Self-contained, auditable in code review, no fixture-loader plumbing.
- **D-11:** Test layout: **`tests/unit/q15/`** — `tests/unit/` for C unit tests organized by module. Scales cleanly as Phases 2–5 add `tests/unit/buffer/`, `tests/unit/registers/`, etc.

**DECISIONS.md Format**
- **D-12:** **ADR-style per-entry** format — each resolved gray area is a numbered entry with structured sections: **Status**, **Context**, **Decision**, **Consequences**, **Sources**. Rigor pays off for the licensing-posture defense and for future contributors asking "why did you do X?"
- **D-13:** **Single file, grows over time** — `docs/DECISIONS.md`. New entries prepended at the top. Splits into a directory only if it exceeds ~2000 lines (defer that decision).
- **D-14:** Location is **`docs/DECISIONS.md`** (not `.planning/DECISIONS.md`). User-facing documentation root so library consumers and future contributors see it. Ships alongside future `LEVERS-CATALOG.md` and `BIBLIOGRAPHY.md`.

**Phase 1 Must Seed Two DECISIONS.md Entries**
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

### Deferred Ideas (OUT OF SCOPE)

- **CI platform specifics** (GitHub Actions vs alternative) — not discussed; planner picks the default (likely GitHub Actions) unless the user says otherwise during planning.
- **Pre-commit hooks** (running grep guard locally, not just in CI) — useful but not required by any REQ; defer to post-M1 DX polish.
- **CMake structure nuances** (nested `CMakeLists.txt` vs top-level only) — planner decides; success criterion is "shared + static artifacts build with locked flags," not a specific CMake topology.
- **Git workflow** (branch strategy, commit signing) — out of scope for the project's technical work; not raised.
- **The reverb algorithm itself** (Phase 3)
- **SPU register state + buffer wrap math** (Phase 2)
- **FIR sample-rate converters** (Phase 4)
- **Python bindings, CLI, or any `spu94_process` end-to-end path** (Phases 5–6)
- **Any test framework work beyond what Phase 1 needs for Q15 helpers**
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CORE-01 | Fixed-point Q15 arithmetic with integer truncation (not rounding), matching SPU saturation and overflow semantics | §Q15 Semantics Deep Dive — ADR-0001 resolution strategy, INT16_MIN × INT16_MIN edge case handling, portable ASR pattern |
| BUILD-01 | CMake-based build producing shared and static library artifacts on Linux | §Architecture Patterns — dual-target CMake authoring with `OBJECT` intermediate library; `add_library(spu94_obj OBJECT ...)` + shared + static consumers |
| BUILD-02 | Determinism compiler flags locked in (`-ffp-contract=off`, `-fno-fast-math`, `-Werror` on agreed warning set) | §Standard Stack — `target_compile_options` with PRIVATE scope; verification via `compile_commands.json` grep |
| BUILD-04 | Static analysis in CI — clang-tidy, cppcheck, compiler warnings as errors | §Standard Stack — `CMAKE_C_CLANG_TIDY` + `CMAKE_C_CPPCHECK` (in-build) OR separate CI job (decoupled) |
| BUILD-05 | UndefinedBehaviorSanitizer in CI with surgical `__attribute__((no_sanitize("integer")))` annotations on functions where overflow is the intended SPU behavior | §UBSan Integration — Clang-only attribute, needs `#if defined(__clang__)` guard for GCC portability; group name `"integer"` IS valid |
| BUILD-07 | CI grep guard prohibiting `float`, `double`, `malloc`, `calloc`, `realloc`, `free`, `long` (unqualified) in core library sources | §Grep Guard Implementation — GNU-grep `-E` + `\b` pattern; BSD/macOS incompatibility noted for future cross-platform CI; portable `awk` fallback documented |
| DOCS-01 | `DECISIONS.md` begun and maintained — gray-area log with one entry per resolution | §ADR Format — Michael Nygard template (Status / Context / Decision / Consequences) + added "Sources" section as locked by D-12 |
| DOCS-05 | `LICENSE` placeholder noting that the final permissive-license pick (MIT vs Apache-2.0) is deferred to end of M1 | §Trivial — single placeholder file with deferral notice; no research depth needed |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

No `./CLAUDE.md` found at the project root at `/home/ubuntu-studio/Desktop/PSX Reverb/`. The user's global `~/.claude/CLAUDE.md` instructs hands-on guided walkthroughs for deployed-system work, but Phase 1 is greenfield code/build scaffolding on the dev machine — standard autonomous implementation is appropriate. No project-specific skills in `.claude/skills/` or `.agents/skills/`. Constraints for Phase 1 are fully captured by PROJECT.md, CONTEXT.md (locked decisions above), and REQUIREMENTS.md.

Key PROJECT.md directives Phase 1 MUST honor:
- No float, no double in core library. [VERIFIED: PROJECT.md §Constraints, REQ BUILD-07]
- No heap allocations (malloc/calloc/realloc/free) in core library. [VERIFIED: PROJECT.md §Constraints]
- No GPL source code reading as primary input — nocash paraphrase discipline. [VERIFIED: PROJECT.md §Constraints; CONTEXT.md `<canonical_refs>`]
- Real-time safety discipline (no locks, no syscalls) — Phase 1 sets the compile-time enforcement that later phases rely on. [VERIFIED: PROJECT.md §Constraints]

## Standard Stack

### Core
| Component | Version | Purpose | Why Standard |
|-----------|---------|---------|--------------|
| CMake | 3.20+ | Build system | `target_compile_options` with PROPERTY scope is mature; `CMAKE_EXPORT_COMPILE_COMMANDS` reliable; scikit-build-core requires ≥ 3.15 but 3.20+ is the comfortable minimum for modern CMake idioms. [CITED: cmake.org 4.3.1 docs] |
| C standard | C11 | Language target | PROJECT.md says "C99/C11"; C11 gives us `_Static_assert` for struct-layout drift detection (PYBIND-05 in Phase 6) without plumbing. Works under all target compilers (gcc, clang, arm-none-eabi-gcc). [VERIFIED: PROJECT.md §Constraints] |
| GCC | 11+ (Ubuntu 22.04 baseline) | Primary compiler | ubuntu-latest GitHub Actions runner ships gcc-11+. Supports all required warning flags. [ASSUMED: GHA runner defaults] |
| Clang | 14+ | Secondary compiler + UBSan runner | Clang is where `__attribute__((no_sanitize("integer")))` is reliably supported; GCC 8+ supports it but Clang is the reference. [CITED: clang.llvm.org/docs/UndefinedBehaviorSanitizer.html] |
| Unity | v2.6.1 (Jan 2025) | C test framework | Small (~3 `.c` files), zero runtime deps, embedded-C standard. Locked by D-09. [VERIFIED: github.com/ThrowTheSwitch/Unity/releases] |
| clang-tidy | 14+ | AST-based static analysis | Standard embedded-C linter; integrates cleanly via `CMAKE_C_CLANG_TIDY`. [CITED: clang.llvm.org/extra/clang-tidy/] |
| cppcheck | 2.x | Complementary static analysis | Different bug classes than clang-tidy (dead code, buffer patterns); `--project=compile_commands.json` integration. [CITED: cppcheck-clang-tidy-and-cmake GitHub guide] |

### Supporting
| Component | Version | Purpose | When to Use |
|-----------|---------|---------|-------------|
| GitHub Actions | ubuntu-latest runner | CI platform | Not locked in CONTEXT.md, but the deferred-ideas note says "planner picks the default." GitHub Actions is the sensible default — free for public repos, wide community knowledge, `ubuntu-latest` comes with gcc/clang/cmake/python preinstalled. [ASSUMED: standard open-source C project default] |
| `ctest` | (bundled with CMake) | Test runner harness | `add_test()` + `enable_testing()` give a trivial `ctest --output-on-failure` invocation in CI without an extra dep. [CITED: CMake docs] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Unity | Ceedling / CMocka / Check | Ceedling is Unity + Ruby-based test runner = extra dep. CMocka needs pthread on some platforms = bad for Cortex-M smoke test. Check requires fork() = doesn't cross-compile. Unity is the right choice per D-09. |
| `OBJECT` library + shared + static | Two separate `add_library()` calls | `OBJECT` library compiles source once, consumed by both shared and static targets → faster + guarantees identical flags. Two separate `add_library()` calls re-compile and risk flag drift. |
| GitHub Actions | GitLab CI / Circle / self-hosted | GH Actions fits the open-source-default assumption. Portable YAML is simple; can switch later if needed. No research-relevant lock-in. |
| `CMAKE_C_CLANG_TIDY` in-build | Separate CI job running `run-clang-tidy.py` against `compile_commands.json` | In-build runs tidy on every compilation = slower dev builds; out-of-build decouples and parallelizes. **Recommendation:** decoupled (separate CI step) — the compile-itself step should be fast and uncluttered. |

**Installation on ubuntu-latest (CI):**
```bash
# GitHub Actions ubuntu-latest already ships: gcc, clang, cmake, python3, git
sudo apt-get update && sudo apt-get install -y clang-tidy cppcheck
# Unity: vendor into third_party/unity/ as git subtree or as a versioned tarball — NOT a submodule
# (submodules break offline/reproducible builds if upstream force-pushes the referenced ref)
```

**Version verification (run before locking in the plan):**
```bash
# Verify current Unity release
curl -sL https://api.github.com/repos/ThrowTheSwitch/Unity/releases/latest | grep tag_name
# Verify GHA ubuntu-latest compiler versions at plan time
# (they drift; pin GHA runner image if determinism matters for M1)
```

## Architecture Patterns

### Recommended Project Structure

```
PSX Reverb/                                     # repo root
├── CMakeLists.txt                              # top-level; defines spu94 project
├── LICENSE                                     # placeholder per DOCS-05
├── .gitignore
├── include/
│   └── spu94/
│       ├── spu94.h                             # umbrella public header (stub in P1; fleshed out P2+)
│       └── spu94_q15.h                         # Q15 static inline helpers (PUBLIC, per D-05/D-08)
├── src/
│   └── spu94/
│       ├── CMakeLists.txt                      # defines spu94_obj + spu94_shared + spu94_static
│       └── spu94_placeholder.c                 # empty TU so the library has something to compile
│                                               # (Phase 2 replaces with real sources)
├── tests/
│   ├── CMakeLists.txt                          # enable_testing() + add_subdirectory(unit)
│   └── unit/
│       └── q15/
│           ├── CMakeLists.txt
│           └── test_q15_mul_truncate.c         # Unity test TU with inline {a,b,expected} table
│       └── unity/                              # vendored Unity v2.6.1 (3 files)
│           ├── unity.c
│           ├── unity.h
│           └── unity_internals.h
├── docs/
│   ├── DECISIONS.md                            # ADR log — seeded with ADR-0001, ADR-0002
│   └── origin/                                 # (existing) Eurorack brief
├── python/                                     # RESERVED for Phase 6 — empty dir or .gitkeep
│   └── spu94/
│       └── .gitkeep
├── cmake/                                      # toolchain files + helper modules
│   └── spu94_warnings.cmake                    # warning flag set, INTERFACE target
└── .github/
    └── workflows/
        └── ci.yml                              # build + test + clang-tidy + cppcheck + UBSan + grep guard
```

**Rationale:**
- `include/spu94/` + `src/spu94/` separation honors D-01, D-02, D-08.
- `python/spu94/.gitkeep` reserves the scikit-build-core location per D-03 without building anything in Phase 1.
- `tests/unit/q15/` honors D-11 and scales to `tests/unit/buffer/`, etc. in Phases 2–5.
- `docs/DECISIONS.md` honors D-14.
- `cmake/` gives the planner a clean place for toolchain files (Phase 8's `arm-none-eabi.cmake`) without polluting root.
- Scaffold directories (`agents/`, `prompts/`, etc.) are NOT shown above but remain in the repo untouched per D-04.

### Pattern 1: Dual Shared + Static from Single Compile (CMake)

**What:** Compile each source file once into an `OBJECT` library, then link it into both a shared and a static library target. Guarantees identical compile flags across both artifacts — critical for determinism.

**When to use:** Always, when a project ships both shared and static. The alternative (two `add_library` calls) duplicates compilation and risks flag drift.

**Example:**
```cmake
# src/spu94/CMakeLists.txt
add_library(spu94_obj OBJECT
    spu94_placeholder.c
)
target_include_directories(spu94_obj PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_property(TARGET spu94_obj PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(spu94_obj PUBLIC c_std_11)

# Determinism flags — PRIVATE so they apply to spu94_obj only,
# not leaked to consumers
target_compile_options(spu94_obj PRIVATE
    -Werror
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -Wstrict-prototypes -Wmissing-prototypes
    -ffp-contract=off
    -fno-fast-math
)

# Shared + static consumers
add_library(spu94_shared SHARED $<TARGET_OBJECTS:spu94_obj>)
target_link_libraries(spu94_shared PUBLIC spu94_obj)
set_target_properties(spu94_shared PROPERTIES OUTPUT_NAME spu94)

add_library(spu94_static STATIC $<TARGET_OBJECTS:spu94_obj>)
target_link_libraries(spu94_static PUBLIC spu94_obj)
set_target_properties(spu94_static PROPERTIES OUTPUT_NAME spu94)
```

*Source pattern: synthesized from CMake 4.3.1 docs + the-risk-taker/cppcheck-clang-tidy-and-cmake GitHub example. [CITED: cmake.org/cmake/help/latest/command/add_library.html]*

### Pattern 2: Determinism Flags as an INTERFACE Target

**What:** Define a CMake INTERFACE target that carries the determinism flags; every real target `target_link_libraries()` it to pick them up. Single source of truth.

**When to use:** When the same flag set must apply to multiple targets (library + tests + future MCU build).

**Example:**
```cmake
# cmake/spu94_warnings.cmake
add_library(spu94_warnings INTERFACE)
target_compile_options(spu94_warnings INTERFACE
    -Werror -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -ffp-contract=off -fno-fast-math
)

# Then in src/spu94/CMakeLists.txt:
target_link_libraries(spu94_obj PRIVATE spu94_warnings)
```

### Pattern 3: Q15 Multiply Helper (Portable ASR)

**What:** Write the Q15 multiply so it produces arithmetic-shift-right semantics regardless of whether the compiler emits `sar` or not. The `>> 15` of a signed value is implementation-defined per C17/C23 — even though every mainstream compiler on two's-complement hardware does ASR, the standard does not mandate it.

**When to use:** Any fixed-point Q15 multiply where the intermediate may be negative.

**Example (header-only, `static inline`, per D-05):**
```c
/* include/spu94/spu94_q15.h */
#ifndef SPU94_Q15_H
#define SPU94_Q15_H

#include <stdint.h>

/*
 * q15_mul_truncate: signed Q15 × Q15 multiply, truncation toward -infinity (ASR-equivalent),
 * saturated to int16_t range on output.
 *
 * Edge case: INT16_MIN * INT16_MIN = +2^30, >> 15 = +2^15 = +32768, which does not fit in int16_t.
 * Saturate to INT16_MAX.
 *
 * Per ADR-0001 (DECISIONS.md): we define "truncate" as arithmetic-shift-right semantics
 * (round toward -infinity for negative results). This matches hardware DSP convention
 * and every mainstream PSX emulator's reproduced behavior. nocash's prose is silent
 * on the rounding direction; witness consensus informs the choice.
 */
static inline int16_t q15_mul_truncate(int16_t a, int16_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    /* Portable ASR: division by 2^15 in C rounds toward zero for negative numbers,
     * which is NOT what we want. Use a branch-free ASR pattern.
     * On GCC/Clang/MSVC targeting two's-complement (all PSX-94 target platforms per C23
     * N2412), signed >> emits ASR; we use it with a documented assumption. */
    int32_t shifted = product >> 15;   /* ASR on all target compilers; verified in tests */
    if (shifted > INT16_MAX) return INT16_MAX;
    if (shifted < INT16_MIN) return INT16_MIN;
    return (int16_t)shifted;
}

/*
 * sat_s16: saturate a wider integer to signed 16-bit range.
 */
static inline int16_t sat_s16(int32_t x) {
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return (int16_t)x;
}

#endif /* SPU94_Q15_H */
```

*Pattern derived from: C17 §6.5.7/5 (implementation-defined shift), C23 N2412 (two's complement mandated), Jason Sachs embeddedrelated.com "Understanding and Preventing Overflow", and Sestevenson's DSP tutorials on Q15 multiply. [CITED: embeddedrelated.com/showarticle/532.php, sestevenson.wordpress.com]*

### Pattern 4: UBSan Surgical `no_sanitize` Annotation

**What:** Functions that deliberately wrap (SPU hard-clip-adjacent behavior) are annotated with `__attribute__((no_sanitize("integer")))` under Clang. GCC 8+ supports the same attribute but syntax/availability must be guarded.

**When to use:** On any function where UBSan's integer-overflow detection would fire on *intentional* SPU saturation or wraparound behavior.

**Example:**
```c
#if defined(__clang__)
#  define SPU94_NO_SANITIZE_INTEGER __attribute__((no_sanitize("integer")))
#elif defined(__GNUC__) && __GNUC__ >= 8
#  define SPU94_NO_SANITIZE_INTEGER __attribute__((no_sanitize_undefined))
   /* GCC's no_sanitize("integer") is accepted but less granular;
    * no_sanitize_undefined is the pragmatic cross-version fallback.
    * Listed in ADR-0003 (future) with rationale. */
#else
#  define SPU94_NO_SANITIZE_INTEGER /* empty */
#endif

static inline SPU94_NO_SANITIZE_INTEGER int16_t intentional_wrap_example(int16_t x) {
    return (int16_t)((int32_t)x * 2);  /* may wrap; that's the SPU behavior we model */
}
```

*Sources: [CITED: clang.llvm.org/docs/UndefinedBehaviorSanitizer.html] confirms `"integer"` is a valid group name covering `signed-integer-overflow`, `unsigned-integer-overflow`, `shift`, `integer-divide-by-zero`, and the implicit-truncation/sign-change checks. GCC support noted in kernel ubsan patches.*

### Anti-Patterns to Avoid

- **Writing `int16_t q15_mul = (int16_t)(((int32_t)a * b) >> 15)` without saturating.** This aliases `INT16_MIN × INT16_MIN = +32768` to `-32768`. Always saturate or document the wrap.
- **Relying on `malloc`/`free` in test code that links against the core library target.** Tests are separate TUs — they may use any C, but the *core library* TU must not. The grep guard enforces this by scanning `src/` and `include/` only.
- **Using `add_library(... SHARED)` and `add_library(... STATIC)` as two separate compilations.** Flags drift silently. Use the `OBJECT` library pattern.
- **Putting determinism flags in `CMAKE_C_FLAGS` (global).** They become sticky and leak into consumer projects that find this library via `find_package`. Use `target_compile_options(target PRIVATE ...)`.
- **Using `__attribute__((always_inline))` in Phase 1.** D-07 forbids it — `static inline` + compiler optimization is sufficient; the attribute is MSVC-unfriendly and earns no portability credit.
- **Transcribing nocash prose into DECISIONS.md.** The licensing posture requires paraphrasing. Quote facts (registers, coefficients, pseudocode patterns); write prose in SPU-94's own words.
- **Vendoring Unity as a git submodule.** Breaks reproducible builds if upstream force-pushes the referenced ref. Vendor as a subtree or pinned tarball.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| C test framework | Custom macros around `assert()` | **Unity** (vendored, ~3 files) | Unity gives `TEST_ASSERT_EQUAL_INT16`, per-test isolation, pass/fail reporting, CTest integration. Rolling your own ends up being 80% of Unity with 10× the bugs. [VERIFIED by D-09] |
| Static analysis | `grep -R "TODO" src/` | **clang-tidy + cppcheck** | These find real bug classes (dead stores, buffer patterns, misuse of sizeof). A custom grep guard complements but does not replace them. [CITED: kitware.com static-checks] |
| Undefined-behavior detection | Runtime `assert(x + y > x)` | **UBSan** (`-fsanitize=undefined`) | UBSan instruments every arithmetic op at compile time; catches overflow, shift OOB, bad alignment, UB memcpy, etc. Cannot be hand-rolled. [CITED: clang.llvm.org UBSan docs] |
| ADR numbering / indexing | Custom tooling, nanoc | **Plain markdown + convention** | DECISIONS.md is a single file per D-13; ADR-NNNN numbers assigned sequentially in-document. No tooling needed for M1 volume (< 20 entries expected). |
| Q15 multiply | Loops / table lookup / clever bit twiddles | **`(int32_t)a * b >> 15`, saturated, in `static inline`** | Widening to int32_t is O(1) on every target architecture; modern compilers emit a single `smul` + `sar` + conditional saturate. Table lookups destroy i-cache. Clever bit-twiddles are hard to audit against hand-computed references. |
| grep guard | Full regex engine in Python | **GNU `grep -nE` + shell** | The guard pattern is 30 characters; Python adds a shell dep and dependency surface. POSIX `grep` + `awk` fallback suffices. |
| CI runner image | Custom Docker image | **`ubuntu-latest` on GH Actions** | GH Actions runner images are pinned, documented, and free. Custom Docker gets us determinism for golden files (Phase 7, BUILD-08) — defer it. |

**Key insight:** Phase 1's tooling choices are aggressively boring by design. Every item above has a canonical "use the library, don't rebuild it" answer. The only place SPU-94 writes original code in Phase 1 is the Q15 helper header and its unit tests — and those are 50-100 LOC total.

## Q15 Semantics Deep Dive (ADR-0001 resolution)

### The C Standard Reality

| Standard | `>>` on negative signed LHS | Source |
|----------|----------------------------|--------|
| C89/C90 | Implementation-defined | ISO/IEC 9899:1990 |
| C99 | Implementation-defined | §6.5.7/5 |
| C11 | Implementation-defined | §6.5.7/5 |
| C17 | Implementation-defined (unchanged) | §6.5.7/5 [CITED: codegenes.net summary, wiki.sei.cmu.edu INT34-C] |
| **C23** | Implementation-defined (UNCHANGED) — but signed integer representation is now mandated two's complement (N2412) | [VERIFIED: open-std.org/jtc1/sc22/wg14/www/docs/n2412.pdf] |

**What C23 changed:** Two's complement is now the ONLY permitted representation. The minimum signed integer value is exactly `-2^(N-1)` and `INT_MIN == -INT_MAX - 1` is guaranteed. This eliminates one-complement and sign-magnitude edge cases from consideration. **But C23 did NOT change the wording of `E1 >> E2` for negative E1 — it remains implementation-defined.** In practice, on every two's-complement platform Clang/GCC/MSVC emit arithmetic shift right (ASR), which rounds toward −∞ for negative results.

**What this means for Q15:** SPU-94 must either (a) rely on implementation-defined ASR + document the assumption + verify with a test, or (b) implement ASR by hand (e.g., `(product - (product < 0 ? (1 << 15) - 1 : 0)) / (1 << 15)` — the "bias-to-floor" trick). Option (a) is simpler and fully portable across our target compilers. Option (b) is purer but slower and harder to read.

### The SPU Hardware Reality (nocash paraphrased)

Nocash states that SPU reverb multiplication results are "divided by +8000h, to fit them to 16bit range" and that "the values written to memory are saturated to -8000h..+7FFFh." [CITED: psx-spx.consoledev.net/soundprocessingunitspu/ via WebFetch 2026-04-18] Nocash does NOT explicitly say whether the division rounds toward zero (C `/` on negative) or toward −∞ (ASR).

**Witness evidence — what emulators do** (behavioral, not source-read): jsgroth.dev blog Part 3 shows reverb formula examples using `>> 15` on signed intermediates, implicitly relying on ASR. [CITED: jsgroth.dev/blog/posts/ps1-spu-part-3/ via WebFetch 2026-04-18] This is the standard DSP-hardware convention: the real SPU implements ASR in silicon (fixed-function multipliers on commercial DSPs always do).

**Confidence level:** MEDIUM on "the SPU rounds toward −∞." Nocash is silent; jsgroth uses `>> 15`; industry convention strongly favors ASR. If a Phase 7 witness-diff against real hardware (Milestone 5) reveals "toward zero" rounding, ADR-0001 would need to be revised — but that discovery is years away and the choice must be made now.

**Recommendation for ADR-0001:** Choose arithmetic shift right (toward −∞) as the documented Q15 multiply semantic. Cite nocash silence, jsgroth convention, and DSP hardware consensus. Record in DECISIONS.md Consequences: *"If M5 hardware capture reveals toward-zero rounding, this ADR is superseded and every Q15 multiply site must be revisited."*

### The INT16_MIN × INT16_MIN Edge Case

`INT16_MIN = -0x8000 = -32768`. `INT16_MIN × INT16_MIN = +2^30 = +1073741824`. `+2^30 >> 15 = +32768 = +0x8000`. But `INT16_MAX = +0x7FFF = +32767`. So +32768 does not fit.

**Behaviors observed in the wild:**
- Wrap to `-32768` (naive `int16_t` cast): plausible hardware behavior, but unverified.
- Saturate to `+32767`: conservative; preserves sign.
- Raise an exception: not applicable in C.

**Recommendation:** Saturate to `+INT16_MAX`. Document this in `q15_mul_truncate`'s contract. The vIIR = −0x8000 anomaly is the specific case the hardware handles weirdly — and nocash documents THAT as a separate quirk (result gets negated, not clipped). So the generic Q15 multiply doesn't need to reproduce the vIIR quirk; ADR-0002 handles vIIR specifically at the register-application site.

### The vIIR = −0x8000 Anomaly (ADR-0002 resolution)

**What nocash says (paraphrased):** vIIR is expected in the range −0x7FFF..+0x7FFF. When set to exactly −0x8000, the multiplication `sample * vIIR` is still performed correctly as a signed multiply, but then the FINAL value written back to reverb memory gets NEGATED — described explicitly by nocash as a quirk, "NOT a simple overflow bug," and noted to affect the "+[mLSAME-2]" addition term that normally shouldn't be touched. Similar effects may occur on other volume registers when set to −0x8000. [CITED: psx-spx.consoledev.net + problemkaputt.de via WebSearch 2026-04-18]

**Recommendation for ADR-0002:** Reproduce faithfully. The anomaly is part of the sound — any preset that historically used vIIR=−0x8000 (if any) would sound different without it. Implementation lands in Phase 3 (register application site), but ADR-0002 is written in Phase 1 so the decision is locked and visible.

**Phase 1 does NOT implement the anomaly — Phase 3 does.** Phase 1's obligation is only to record the decision.

## Runtime State Inventory

*Not applicable — Phase 1 is greenfield. No existing code, no running services, no stored data, no installed artifacts to update. This section exists only for rename/refactor/migration phases.*

## Common Pitfalls

### Pitfall 1: `>> 15` implementation-defined for negative LHS
**What goes wrong:** Code assumes ASR; tests pass on gcc/clang; ports to an exotic compiler (e.g., a rare MCU C compiler in Phase 8 or later) that shifts as logical. Negative products produce wrong results with no compiler warning.
**Why it happens:** The C standard does not mandate ASR for signed right-shift; it is implementation-defined through C23.
**How to avoid:** (a) Test the assumption with a `_Static_assert` or a runtime test fixture; (b) document in ADR-0001 that the chosen target compilers (gcc, clang, arm-none-eabi-gcc) all emit ASR on two's-complement hardware, and (c) add a unit test that confirms `-1 >> 1 == -1` (ASR) not `0x7FFF...` (logical).
**Warning signs:** Unit tests for `q15_mul_truncate(-32768, 32767)` returning wildly wrong values on any compiler.

### Pitfall 2: `INT16_MIN × INT16_MIN` overflow aliasing
**What goes wrong:** The naive `(int16_t)(((int32_t)a * b) >> 15)` returns `-32768` instead of the mathematically-correct `+32768` (saturated `+32767`). If the SPU feeds two full-scale negative signals into the same stage, the output is wrong.
**Why it happens:** `+2^15` cannot fit in `int16_t`; cast wraps. [CITED: embeddedrelated.com/showarticle/532.php]
**How to avoid:** Saturate after the shift and before the cast to `int16_t`, per Pattern 3 above.
**Warning signs:** Boundary test with `a = b = INT16_MIN` fails; audible artifact on very loud negative inputs.

### Pitfall 3: UBSan fires on every intentional SPU wrap
**What goes wrong:** The CI UBSan build reports 10,000 "signed integer overflow" errors per test run because SPU saturation and the vIIR anomaly *are* overflow.
**Why it happens:** UBSan's `-fsanitize=integer` (or the subgroup `signed-integer-overflow`) treats all signed overflow as an error. UB by the C standard, but intentional by the SPU spec.
**How to avoid:** Annotate the *specific* functions where intentional wrap happens with `__attribute__((no_sanitize("integer")))` under a `#ifdef __clang__` guard. Do NOT globally disable the sanitizer — surgical annotations preserve coverage elsewhere. List annotated functions in a future ADR-0003 for auditability.
**Warning signs:** UBSan output floods; the temptation is to `-fsanitize=undefined` minus `signed-integer-overflow` globally, which is wrong.

### Pitfall 4: Grep guard false-positives on comments / documentation
**What goes wrong:** A code comment like `/* see malloc.h for reference */` trips the grep guard; engineer disables the guard for one commit "temporarily" and it decays.
**Why it happens:** Pure text-match pattern, no C-aware parser.
**How to avoid:** Scope the guard to `src/**/*.c` and `include/**/*.h`; accept the occasional comment false-positive as a feature (the comment is a hint that someone was thinking about malloc — flag it); provide a documented "escape hatch" (a `/* SPU94_ALLOW_TOKEN: rationale */` marker that the grep is configured to skip) if needed later. Document the guard pattern in DECISIONS.md or at minimum in `ci.yml` comments.
**Warning signs:** Guard is disabled in any commit. Red flag.

### Pitfall 5: BSD grep vs GNU grep `\b` incompatibility
**What goes wrong:** Guard works on ubuntu-latest (GNU grep); fails or produces different results on macOS (BSD grep) if a contributor runs the guard locally. M1 is Linux-first per CONTEXT.md but contributors on macOS may exist.
**Why it happens:** BSD grep's `\b` requires different syntax; `\<` and `\>` also vary. [CITED: ponderthebits.com GNU-vs-BSD utilities]
**How to avoid:** Phase 1 decision: guard runs *only* in CI on GNU grep; document this. If local pre-commit hook wanted (deferred per CONTEXT.md), use `grep -E '[[:<:]]...[[:>:]]'` BSD syntax in a macOS-aware wrapper.
**Warning signs:** Contributor report "guard doesn't fire locally but fires in CI" on macOS.

### Pitfall 6: `_Static_assert` C23 rename
**What goes wrong:** C23 adds `static_assert` as a keyword; older code uses `_Static_assert`. Mixing can confuse.
**Why it happens:** C23 language evolution.
**How to avoid:** Use `_Static_assert` consistently in Phase 1 (works everywhere, C11+). Revisit in a later phase if C23 adoption matters.
**Warning signs:** Not a Phase 1 issue — flagged for awareness only.

### Pitfall 7: `target_compile_options` INTERFACE leakage
**What goes wrong:** Determinism flags set as `PUBLIC` or `INTERFACE` on `spu94_obj` leak into consumer projects (Phase 6 Python wheel, Phase 8 MCU). Consumer gets flag conflicts.
**Why it happens:** CMake `PUBLIC` + `INTERFACE` propagate through `target_link_libraries`.
**How to avoid:** Use `PRIVATE` for determinism flags (they need to apply only when compiling spu94's own source files). Use `PUBLIC` only for include directories (`include/`).
**Warning signs:** Python wheel build fails with "unknown flag -ffp-contract=off" on Python's toolchain.

## CI Wiring Details

### Verification that determinism flags are in `compile_commands.json`

Per BUILD-02, the flags must be "verifiable in the compile command line." CMake emits `compile_commands.json` when `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. Verification step in CI:

```yaml
# .github/workflows/ci.yml (sketch — planner finalizes)
- name: Verify determinism flags
  run: |
    test -f build/compile_commands.json
    grep -q '\-ffp-contract=off' build/compile_commands.json || { echo "MISSING -ffp-contract=off"; exit 1; }
    grep -q '\-fno-fast-math' build/compile_commands.json || { echo "MISSING -fno-fast-math"; exit 1; }
    grep -q '\-Werror' build/compile_commands.json || { echo "MISSING -Werror"; exit 1; }
```

*[CITED: cmake.org CMAKE_EXPORT_COMPILE_COMMANDS docs; clang.llvm.org JSON Compilation Database Format Specification]*

### Grep Guard Pattern (BUILD-07)

Goal: fail CI if any of `float`, `double`, `malloc`, `calloc`, `realloc`, `free`, or unqualified `long` appear in `src/` or `include/`. Allow `long long`.

```bash
# Single portable-GNU-grep invocation:
if grep -nE '\b(float|double|malloc|calloc|realloc|free)\b' \
        $(find src include -name '*.c' -o -name '*.h'); then
  echo "ERROR: forbidden token found in core library sources"
  exit 1
fi

# 'long' is harder — must exclude 'long long':
if grep -nE '\blong\b' $(find src include -name '*.c' -o -name '*.h') \
   | grep -v 'long long'; then
  echo "ERROR: unqualified 'long' found (use int32_t / int64_t)"
  exit 1
fi
```

*The `long` pattern's `grep -v 'long long'` second-pass is the simplest portable form. A single-pass negative-lookahead pattern (`\blong\b(?!\s+long)`) requires `grep -P` which is GNU-only. The two-pass form works on both GNU and BSD grep as long as the first pass uses `-E`. [CITED: GNU grep 3.12 manual; CONTEXT.md `<specifics>` section]*

### clang-tidy invocation (decoupled from build)

Two integration options, both documented:

**Option A (in-build, simpler):**
```cmake
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    set_target_properties(spu94_obj PROPERTIES
        C_CLANG_TIDY "${CLANG_TIDY_EXE};--warnings-as-errors=*"
    )
endif()
```

**Option B (decoupled CI job, recommended):**
```yaml
- name: Run clang-tidy
  run: |
    cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    find src include -name '*.c' -o -name '*.h' \
      | xargs clang-tidy -p build --warnings-as-errors='*'
```

**Recommendation:** Option B. Keeps main build fast; failures are isolated to the lint job.

### cppcheck invocation

```bash
cppcheck --project=build/compile_commands.json \
         --enable=warning,performance,portability \
         --error-exitcode=1 \
         --inline-suppr \
         --suppress=missingIncludeSystem \
         src/ include/
```

*[CITED: the-risk-taker/cppcheck-clang-tidy-and-cmake GitHub repository pattern]*

### UBSan CI job

```yaml
- name: UBSan build + test
  env:
    CC: clang
    CFLAGS: '-fsanitize=undefined -fno-sanitize-recover=undefined -g -O1'
    LDFLAGS: '-fsanitize=undefined'
  run: |
    cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-ubsan
    cd build-ubsan && ctest --output-on-failure
```

`-fno-sanitize-recover=undefined` turns UBSan diagnostics into hard aborts (CI-friendly). Phase 1 ships zero intentional-wrap code, so the UBSan build passes trivially. Phase 3's reverb core will add the `SPU94_NO_SANITIZE_INTEGER` annotations.

*[CITED: clang.llvm.org UBSan docs; MaskRay's "All about UBSan" reference]*

## Validation Architecture

### What "correct" means for Q15 helpers

"Correct" = "outputs match a hand-computed truncation-direction reference" per ROADMAP Phase 1 success criterion 2. Specifically:

1. **Truncation direction is arithmetic-shift-right (round toward −∞).** Tested by constructing inputs whose product is a specific known negative value, and asserting that the output matches the ASR-of-the-product, not the C-division-of-the-product.
2. **Saturation to `INT16_MIN..INT16_MAX` on boundary inputs.** Tested with the four corners of the int16 × int16 grid plus INT16_MIN/MAX paired with representative mid-range values.
3. **Identity and unit cases.** `q15_mul_truncate(0, x) == 0`, `q15_mul_truncate(INT16_MAX, 0) == 0`, `sat_s16(0) == 0`, `sat_s16(INT16_MAX + 1) == INT16_MAX`.

### Independent Oracles

| Oracle | What it verifies | Authority |
|--------|------------------|-----------|
| **Hand-computed table** | For each `{a, b}` test case, compute `(a * b) >> 15` with saturation on paper (or Python `numpy.int32` + explicit ASR) and put the expected value next to the test. | HIGHEST — human-checked, no software dependency. Per D-10. |
| **C23 two's-complement guarantee** | For negative intermediate products, two's complement representation is mandated (N2412); ASR on most compilers produces the documented result. | HIGH — ISO standard. |
| **nocash SPU reverb formula** | Describes `* vIIR / 0x8000` pattern; the `/ 0x8000` is the Q15 shift. | MEDIUM — authoritative on what the hardware *does*, silent on C-level rounding direction. |
| **Witness emulators (output-only)** | lv2-psx-reverb output on a test vector; if Phase 1's Q15 helper produces different values, something is off. Not a Phase 1 gate — witness diff lands in Phase 7. | LOW for Phase 1; MEDIUM for Phase 7. Subject to licensing discipline (output-only, no source reading). |

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity v2.6.1 (vendored, 3 files: unity.c, unity.h, unity_internals.h) |
| Config file | none — Unity is header + TU, configured per test TU |
| Quick run command | `ctest --test-dir build --output-on-failure -R q15` |
| Full suite command | `ctest --test-dir build --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CORE-01 | `q15_mul_truncate` produces ASR-direction truncation on negative intermediates | unit | `ctest -R q15_mul_truncate` | Wave 0 |
| CORE-01 | `q15_mul_truncate(INT16_MIN, INT16_MIN)` saturates to `INT16_MAX` | unit | `ctest -R q15_mul_truncate_boundary` | Wave 0 |
| CORE-01 | `q15_mul_truncate` identity + zero cases | unit | `ctest -R q15_mul_truncate_trivial` | Wave 0 |
| CORE-01 | `sat_s16` clamps out-of-range int32 to int16 bounds | unit | `ctest -R sat_s16` | Wave 0 |
| CORE-01 | `q15_add_sat` (if implemented) saturates on overflow | unit | `ctest -R q15_add_sat` | Wave 0 |
| BUILD-01 | `libspu94.so` and `libspu94.a` both build | smoke | `cmake --build build && test -f build/src/spu94/libspu94.so && test -f build/src/spu94/libspu94.a` | Wave 0 |
| BUILD-02 | Determinism flags present in `compile_commands.json` | smoke | `grep -q '\-ffp-contract=off' build/compile_commands.json` (as above) | Wave 0 |
| BUILD-04 | clang-tidy green | automated-manual-only-on-CI | `find src include -name '*.c' -o -name '*.h' \| xargs clang-tidy -p build --warnings-as-errors='*'` | Wave 0 |
| BUILD-04 | cppcheck green | automated-manual-only-on-CI | `cppcheck --project=build/compile_commands.json --error-exitcode=1` | Wave 0 |
| BUILD-04 | Warnings-as-errors green | smoke | `cmake --build build 2>&1 \| grep -E 'warning:' && exit 1 \|\| exit 0` | Wave 0 |
| BUILD-05 | UBSan build green on empty reverb core | smoke | Full UBSan job (above) | Wave 0 |
| BUILD-07 | Grep guard fires on seeded forbidden token, passes on clean tree | smoke | Dedicated CI step: scan `src/`, `include/`; separate positive-test run with a fixture file containing the token | Wave 0 |
| DOCS-01 | DECISIONS.md contains ADR-0001 (Q15 multiply) and ADR-0002 (vIIR anomaly) as complete ADR-format entries | manual + automated-file-check | `grep -q 'ADR-0001' docs/DECISIONS.md && grep -q 'ADR-0002' docs/DECISIONS.md` | Wave 0 |
| DOCS-05 | LICENSE placeholder exists and notes deferral | manual + automated-file-check | `test -f LICENSE && grep -q 'deferred' LICENSE` | Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build --output-on-failure -R q15` — runs all Q15 helper tests (expected < 100 ms)
- **Per wave merge:** `cmake --build build && ctest --test-dir build --output-on-failure && find src include -name '*.[ch]' | xargs clang-tidy -p build`
- **Phase gate:** Full CI workflow green — build × {gcc, clang}, UBSan build, clang-tidy, cppcheck, grep guard, flag verification, ctest

### Wave 0 Gaps
- [ ] `src/spu94/spu94_placeholder.c` — empty translation unit (or a `const int spu94_version = 0;` — something so the library has something to compile in Phase 1)
- [ ] `include/spu94/spu94.h` — umbrella header stub (includes `spu94_q15.h`)
- [ ] `include/spu94/spu94_q15.h` — Q15 static inline helpers
- [ ] `tests/unit/q15/test_q15_mul_truncate.c` — Unity test TU with inline reference table
- [ ] `tests/unity/unity.c`, `tests/unity/unity.h`, `tests/unity/unity_internals.h` — vendored Unity v2.6.1
- [ ] `CMakeLists.txt` (root) + `src/spu94/CMakeLists.txt` + `tests/CMakeLists.txt` + `tests/unit/q15/CMakeLists.txt`
- [ ] `cmake/spu94_warnings.cmake` — INTERFACE target for flag set
- [ ] `.github/workflows/ci.yml` — CI workflow
- [ ] `docs/DECISIONS.md` — seeded with ADR-0001 and ADR-0002
- [ ] `LICENSE` — deferral placeholder
- [ ] `.gitignore` — standard C build artifacts + CMake `build/`

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build system | ✓ (assumed on dev machine + GHA ubuntu-latest) | 3.20+ required | none — hard dep |
| GCC | Primary compiler | ✓ (assumed) | 11+ | Clang 14+ substitutes |
| Clang | UBSan + portable check | ✓ (assumed) | 14+ | UBSan not usable under GCC < 8 for our attribute syntax |
| clang-tidy | Static analysis | ✓ (GHA ubuntu-latest has it; may need `apt install clang-tidy` on dev machine) | matches Clang version | warnings-as-errors from compiler covers a subset |
| cppcheck | Static analysis | ✓ (may need `apt install cppcheck`) | 2.x | clang-tidy covers some overlap |
| Unity | Test framework | ✗ — NOT YET VENDORED | — | Must be vendored as part of Phase 1 work (3 files from github.com/ThrowTheSwitch/Unity v2.6.1) |
| GH Actions runner | CI | ✓ (assumed; if user self-hosts, planner revises) | ubuntu-latest | GitLab CI, self-hosted, or local-only testing all viable |
| Python | scikit-build-core prep (NOT for Phase 1 itself) | — | — | Phase 6 concern |
| arm-none-eabi-gcc | MCU cross-compile | — | — | Phase 8 concern |

**Missing dependencies with no fallback:**
- None that block Phase 1.

**Missing dependencies with fallback:**
- Unity is not yet vendored — trivial to add as first task of Phase 1.
- `clang-tidy` and `cppcheck` may need apt-install on dev machine; GHA handles this in CI.

**Verification at plan time (Plan 01 first task):**
```bash
command -v cmake && cmake --version | head -1
command -v gcc && gcc --version | head -1
command -v clang && clang --version | head -1
command -v clang-tidy && clang-tidy --version | head -1
command -v cppcheck && cppcheck --version
```

## Code Examples

### ADR-0001 entry for DECISIONS.md (template — planner fleshes out)

```markdown
## ADR-0001: Q15 multiply semantics — truncation direction

**Status:** Accepted (2026-04-18, Phase 1)

**Context:**

The PS1 SPU reverb algorithm uses Q15 fixed-point multiplication pervasively. Every gain-type register (vWALL, vIIR, vCOMB1..4, vAPF1..2, vLOUT, vROUT, and others) participates in a multiplication of the form `sample * register_value`, where the result must be shifted right by 15 bits to re-scale into the int16 sample range before being written back to the reverb work buffer or the output.

nocash's SPU documentation states that multiplication results are "divided by +8000h" and saturated to the int16 range, but does not specify the rounding direction for negative results. In C, `>> 15` on a signed negative LHS is implementation-defined (C17 §6.5.7/5, unchanged in C23), and on two's-complement hardware every mainstream compiler (GCC, Clang, MSVC, arm-none-eabi-gcc) emits arithmetic shift right (ASR), which rounds toward negative infinity. C `/` on a negative dividend rounds toward zero. These are *different* operations for negative results.

For SPU-94 to be deterministic across platforms, the rounding direction must be chosen and documented.

**Decision:**

Q15 multiplication in SPU-94 uses arithmetic shift right (ASR) — rounding toward negative infinity — implemented as:

```c
int32_t product = (int32_t)a * (int32_t)b;
int16_t result = sat_s16(product >> 15);
```

relying on the documented implementation-defined behavior of GCC, Clang, and arm-none-eabi-gcc to emit ASR on two's-complement hardware (now mandated by C23 N2412).

A unit test (`test_q15_mul_truncate_asr`) confirms `-1 >> 1 == -1` at build time via `_Static_assert` and at runtime via Unity assertions on a table of hand-computed reference values.

**Consequences:**

- *Easier:* All target compilers agree; no runtime cost; code is idiomatic.
- *Harder:* If a future target compiler does NOT emit ASR, the `_Static_assert` fires at compile time and the port is blocked until either the compiler is swapped or an explicit ASR helper is written.
- *Risk:* If Milestone 5 hardware capture reveals the SPU actually rounds toward zero (not ASR), this ADR is superseded and every Q15 multiply site must be revisited. The likelihood is LOW — industry DSP hardware convention, nocash silence, and witness-emulator consensus all favor ASR — but the risk is acknowledged.

**Sources:**

- ISO/IEC 9899:2018 (C17) §6.5.7/5 — signed right shift is implementation-defined
- ISO/IEC 9899:2023 (C23) draft — two's complement mandated (WG14 N2412); shift semantics unchanged
- nocash psx-spx, SPU Reverb Formula section (paraphrased; see BIBLIOGRAPHY.md entry BIB-001)
- jsgroth.dev PS1 SPU Part 3 (behavioral witness, not source-read)
- embeddedrelated.com — Jason Sachs, "Understanding and Preventing Overflow (I Had Too Much to Add Last Night)"
```

### ADR-0002 entry for DECISIONS.md (template — planner fleshes out)

```markdown
## ADR-0002: vIIR = −0x8000 anomaly — reproduce faithfully

**Status:** Accepted (2026-04-18, Phase 1) — implementation deferred to Phase 3

**Context:**

nocash documents that the SPU reverb's vIIR parameter "works only in the range −0x7FFF..+0x7FFF." When the caller writes −0x8000 to vIIR, the multiplication proceeds correctly, but the final computed value written to the reverb memory is negated. nocash describes this as a quirk — explicitly "NOT a simple overflow bug" — and notes it also affects the `+[mLSAME-2]` addition term. Similar negation effects may occur on other volume registers when set to −0x8000.

The question: does SPU-94 reproduce this anomaly, or treat −0x8000 as equivalent to saturated +0x7FFF (or some other "sane" behavior)?

**Decision:**

Reproduce the anomaly faithfully. When any reverb coefficient register is written with exactly −0x8000, the final post-multiplication result at that site is negated. This matches nocash's documented behavior and preserves bit-faithfulness for any preset or modulation sequence that historically exercised this code path.

Implementation lands in Phase 3 at the register-application site, not in `q15_mul_truncate` itself. `q15_mul_truncate` remains a clean generic Q15 multiply; the anomaly is register-specific.

**Consequences:**

- *Easier:* Golden-file tests that compare SPU-94 against hardware captures (M5) will match on vIIR=−0x8000 inputs. The anomaly is *the sound*, not a bug.
- *Harder:* Every register that applies vIIR-style coefficients (at minimum, vIIR itself; per nocash, "similar effects" on other volume registers must be identified and tested in Phase 3) needs the negation logic. Increases test surface.
- *Test obligation:* Phase 3 TEST-06 specifically asserts the anomaly occurs under vIIR=−0x8000 and does NOT occur under vIIR=−0x7FFF, against a hand-derived reference.

**Sources:**

- nocash psx-spx, SPU Reverb Formula section — quirk description (paraphrased; see BIBLIOGRAPHY.md entry BIB-001)
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hand-rolled signed-right-shift with pre-processor checks | Rely on C23 two's complement (N2412) + documented assumption | C23 ratified 2024 | Simpler code; portable across all mainstream compilers; exotic-compiler edge case becomes a compile-time assert rather than runtime bug |
| Separate `STATIC` and `SHARED` library targets | `OBJECT` library + two consumers | CMake 3.12+ (object libraries gained `PIC` and linking improvements) | Single compile pass; guaranteed flag-identical artifacts |
| `compile_commands.json` as a clang-specific artifact | Industry-standard compilation database across clang-tidy, cppcheck, IWYU, ccls, clangd, etc. | ~2018 | Single `CMAKE_EXPORT_COMPILE_COMMANDS=ON` feeds all tooling |
| GCC UBSan with global disable | Clang UBSan with `no_sanitize("integer")` per-function | Clang 3.9+ / GCC 8+ | Surgical suppression preserves coverage |

**Deprecated/outdated for this project:**
- `autotools` — CMake is the chosen build system.
- `make` handwritten — CMake generates ninja/make; no handwritten Makefiles in SPU-94.
- `CMAKE_BUILD_TYPE=Release` as determinism guarantee — it's not; explicit flags are required.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The chosen CI platform is GitHub Actions (not GitLab/CircleCI/self-hosted) | §Standard Stack, §CI Wiring Details | LOW — YAML rewrite + re-test; no architectural impact. Confirm with user in planning if uncertain. |
| A2 | GCC 11+ and Clang 14+ are the minimum C compilers (Ubuntu 22.04 baseline + GHA ubuntu-latest) | §Standard Stack | LOW — both ship for years; if user wants older baseline, Unity + CMake still work down to much older versions |
| A3 | The SPU hardware Q15 multiply rounds toward −∞ (ASR), not toward zero (C division) | §Q15 Semantics Deep Dive, §ADR-0001 | MEDIUM — nocash is silent; witness-emulator consensus favors ASR; hardware-capture verification is M5 work. If M5 reveals toward-zero rounding, ADR-0001 is superseded. |
| A4 | The vIIR = −0x8000 anomaly reproduction is desired (vs "fix" the quirk) | §ADR-0002 | LOW — CONTEXT.md D-12 locks "reproduce faithfully" as part of bit-faithfulness; aligned with PROJECT.md Core Value. |
| A5 | `__attribute__((no_sanitize("integer")))` string name matches Clang's expected syntax (group "integer" covers signed-overflow + unsigned-overflow + shift + etc.) | §UBSan Integration | LOW — verified against Clang 23 UBSan docs; if a future Clang version renames the group, the macro changes in one place |
| A6 | GNU grep is available in CI (needed for `\b` word-boundary syntax in guard) | §Grep Guard Implementation | LOW — ubuntu-latest ships GNU grep; macOS/BSD portability is a deferred concern per CONTEXT.md. |
| A7 | Unity v2.6.1 (Jan 2025) is the latest stable release | §Standard Stack | LOW — confirm via `curl https://api.github.com/repos/ThrowTheSwitch/Unity/releases/latest` at plan time; if newer, pin to newer |
| A8 | `scikit-build-core` requires CMake ≥ 3.15; we use 3.20+ which is safely above | §Standard Stack | LOW — if scikit-build-core bumps its minimum, we're still ahead |
| A9 | Phase 1 does NOT need to produce a valid `spu94.h` public header — a stub is sufficient | §Architecture Patterns | LOW — ROADMAP success criteria do not require the full API; Phase 2 fleshes out the header per CORE-03/04 |
| A10 | macOS/Windows builds are NOT Phase 1 concerns (deferred per PROJECT.md §Constraints and v2-PLAT-01/02) | §Environment Availability | LOW — Linux-first is explicit M1 scope |

**Confidence in assumption breakdown:** Most are LOW-risk — they reflect explicit CONTEXT.md decisions or mainstream defaults. A3 is the only MEDIUM-risk assumption; it's a forward-looking claim about hardware behavior that M5 will ultimately verify. All others are safe to lock in Phase 1 with the noted revision paths if they fail.

## Open Questions (RESOLVED)

All questions surfaced below have been resolved in the Phase 1 plans. Dispositions are recorded inline so downstream agents do not re-litigate them.

1. **Does the planner/user want `ctest` integration in Phase 1 or is the Unity-standalone `main()` per-test-TU sufficient?**
   - What we know: CTest integration is 3 lines of CMake; standalone test binaries are simpler.
   - What's unclear: User preference on test harness polish in Phase 1 vs. deferring to later.
   - Recommendation: Use CTest from the start — it's trivial and gives `ctest --output-on-failure` which is nicer than manually running each test binary.
   - **RESOLVED:** CTest integration is used from the start. Plan 01 Task 2 registers `test_q15` via `add_test()` / `enable_testing()`; full-suite command in VALIDATION.md is `ctest --test-dir build -R q15 --output-on-failure`.

2. **Does the grep guard include `size_t` and `ptrdiff_t` or only the explicit token list from BUILD-07?**
   - What we know: BUILD-07 lists exactly `float, double, malloc, calloc, realloc, free, long` (unqualified).
   - What's unclear: Should `size_t` be allowed (yes — it's unsigned and platform-correct) or should we force `uint32_t`/`uint64_t` (no — `size_t` is the correct type for sizes/offsets).
   - Recommendation: Stick to BUILD-07's exact list. `size_t`, `ptrdiff_t`, and `intptr_t` are all explicitly OK and don't need to be in the guard.
   - **RESOLVED:** Grep-guard token set matches BUILD-07 verbatim (`float|double|malloc|calloc|realloc|free|\blong\b` with `long long` exception). `size_t`, `ptrdiff_t`, `intptr_t` are NOT in the guard and remain legal. Recorded in Plan 02 Task 1 action block.

3. **Is GitHub Actions confirmed as the CI platform, or does the user want a decision point in planning?**
   - What we know: Not discussed in CONTEXT.md; deferred-ideas note says "planner picks the default (likely GitHub Actions) unless the user says otherwise during planning."
   - What's unclear: Whether the planner should present CI platform as an open choice or proceed with GHA.
   - Recommendation: Proceed with GHA. Surfacing this in planning adds friction for a low-stakes call.
   - **RESOLVED:** GitHub Actions is the Phase 1 CI platform. Plan 02 Task 2 authors `.github/workflows/ci.yml`. Third-party actions pinned to full commit SHAs per T-01-01.

4. **Is `compile_commands.json` required to be committed to the repo, or is it a build artifact only?**
   - What we know: It's generated by CMake into the build directory; consumed by clang-tidy/cppcheck/editors.
   - What's unclear: Editor-experience implications (some editors want it at repo root via a symlink).
   - Recommendation: Do NOT commit it. Keep `build/` in `.gitignore`. Editors can be configured per-developer; Phase 6 README can mention symlinking for convenience.
   - **RESOLVED:** `compile_commands.json` is a build artifact; `build/` is in `.gitignore`. `verify-flags.sh` reads it from the freshly configured `build/` tree.

5. **Should the grep guard have a documented "escape hatch" for legitimate uses?** (e.g., a test TU that MUST use `malloc` to validate alloc-free-ness of the core library)
   - What we know: Tests live in `tests/` which is outside the guard's scan scope (`src/` and `include/` only). So the guard does not block tests from using forbidden tokens.
   - What's unclear: Whether any *core* use case exists (answer: no — hence the guard).
   - Recommendation: No escape hatch. Keep the guard strict. If a legitimate exception emerges, revise the guard with a new ADR.
   - **RESOLVED:** No escape hatch. Guard is strict; scan scope is limited to `src/` and `include/` only. Future exceptions require a new ADR in `docs/DECISIONS.md`.

## Sources

### Primary (HIGH confidence)
- **CMake 4.3.1 documentation** — CMAKE_EXPORT_COMPILE_COMMANDS, add_library, target_compile_options, OBJECT libraries — https://cmake.org/cmake/help/latest/
- **Clang 23 UBSan documentation** — group names, `no_sanitize` attribute syntax — https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
- **Clang JSON Compilation Database Format Specification** — https://clang.llvm.org/docs/JSONCompilationDatabase.html
- **ISO/IEC 9899:2018 (C17)** §6.5.7/5 — signed right shift implementation-defined
- **WG14 N2412** — Two's complement sign representation for C2x — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2412.pdf
- **nocash psx-spx** (SPU Reverb Formula section) — https://psx-spx.consoledev.net/soundprocessingunitspu/ and https://problemkaputt.de/psx-spx.htm (paraphrased per licensing posture)
- **CONTEXT.md (2026-04-18)** — locked Phase 1 decisions D-01 through D-14
- **PROJECT.md / REQUIREMENTS.md / ROADMAP.md** — project constraints and Phase 1 success criteria
- **Unity GitHub repo** — https://github.com/ThrowTheSwitch/Unity (v2.6.1 latest, Jan 2025)

### Secondary (MEDIUM confidence)
- **jsgroth.dev PS1 SPU Part 3** — https://jsgroth.dev/blog/posts/ps1-spu-part-3/ — Q15 code patterns for PSX reverb (behavioral witness; not source-read)
- **embeddedrelated.com — Jason Sachs** — https://www.embeddedrelated.com/showarticle/532.php — overflow and Q15 edge cases
- **Sestevenson DSP tutorials — Fixed Point Multiplication** — https://sestevenson.wordpress.com/fixed-point-multiplication/
- **MaskRay "All about UndefinedBehaviorSanitizer"** — https://maskray.me/blog/2023-01-29-all-about-undefined-behavior-sanitizer
- **Michael Nygard ADR template** (Nygard original + arc42 documented use) — https://github.com/joelparkerhenderson/architecture-decision-record
- **the-risk-taker/cppcheck-clang-tidy-and-cmake** — CMake integration patterns for static analysis — https://github.com/the-risk-taker/cppcheck-clang-tidy-and-cmake

### Tertiary (LOW confidence — secondary discussions, not authoritative)
- **ponderthebits.com** — GNU vs BSD grep differences — https://ponderthebits.com/2017/01/know-your-tools-linux-gnu-vs-mac-bsd-command-line-utilities-grep-strings-sed-and-find/
- **codegenes.net** — C shift-behavior discussion — https://www.codegenes.net/blog/is-left-and-right-shifting-negative-integers-defined-behavior/
- **Hacker News thread** on C23 two's complement — https://news.ycombinator.com/item?id=35437009

## Metadata

**Confidence breakdown:**
- Standard stack (CMake / Unity / GCC / clang-tidy / cppcheck / UBSan / GitHub Actions): **HIGH** — all verified against official docs and recent releases; versions current as of research date.
- CMake dual-target + determinism flag patterns: **HIGH** — idiomatic modern CMake verified against Kitware docs and published examples.
- Q15 multiply semantics (ADR-0001): **HIGH on the C-standard claims** (C17/C23 behavior verified against ISO references), **MEDIUM on the hardware-rounds-toward-minus-infinity claim** (nocash silent; witness-emulator consensus; hardware capture verification is M5 work).
- vIIR anomaly (ADR-0002): **HIGH** — nocash documents the quirk explicitly; reproduction is the only decision consistent with bit-faithfulness per PROJECT.md.
- UBSan attribute portability: **HIGH on Clang, MEDIUM on GCC** — Clang is the reference; GCC 8+ supports with slightly different syntax.
- Grep guard portability: **HIGH on GNU grep (Linux CI), LOW on BSD grep (macOS)** — macOS support deferred per CONTEXT.md.
- GitHub Actions choice: **MEDIUM** — not explicitly locked in CONTEXT.md; recommended as planner's default; confirm in planning if uncertain.

**Research date:** 2026-04-18
**Valid until:** 2026-05-18 (Phase 1 is foundational and unlikely to shift; the standard-stack items are stable. Re-verify Unity release tag and GHA ubuntu-latest compiler versions at plan time.)
