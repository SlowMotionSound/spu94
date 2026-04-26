---
phase: 06-python-binding-cli
plan: 01
subsystem: python-binding
tags: [python, ctypes, binding, reflection, import-time]
requires:
  - Phase 5 public C API (libspu94.so export surface)
  - SPU94_REG__COUNT = 35, SPU94_PRESET__COUNT = 10 pinned in headers
  - spu94_presets[] .rodata table populated (Phase 5 Plan 03)
  - spu94_reg_name / spu94_reg_hw_offset / spu94_reg_type accessors (Phase 2 Plan 02)
  - pytest 9.x available on the dev workstation (Python 3.13.7 here; project min 3.10)
provides:
  - python/spu94/ ctypes binding package importable on a dev-built libspu94.so
  - spu94._lib CDLL handle with argtypes/restype for 20 public C symbols
  - spu94.Register — 35-member IntEnum built by runtime reflection
  - spu94.Preset — 10-member ABI-stable IntEnum
  - spu94.presets — dict-like accessor backed by ctypes.in_dll on spu94_presets[]
  - spu94.PresetInfo dataclass (frozen; .id / .name / .regs[35])
  - Import-time drift assertions (T-06-02..T-06-04) with RuntimeError("spu94 library mismatch: ...")
  - tests/python/binding/ pytest suite with ctest label "binding" (4 tests green)
  - Consumption surface for Plan 2 (_lib + constants) and Plan 5 (fuzz migration)
affects:
  - tests/python/CMakeLists.txt (one-line append of add_subdirectory(binding))
  - None of the existing Phases 1-5 tests; no regressions
tech-stack:
  added:
    - pytest 9.0.3 as the Python test framework for Phase 6 onward
    - ctypes.in_dll pattern for .rodata struct-array imports
    - Runtime reflection via IntEnum(name, members, module=__name__) functional API
  patterns:
    - Absolute-path CDLL loading (SPU94_LIB env first, Path(__file__).parent/.so second)
    - PatchedCDLL shim class for drift-test monkeypatching without a separate fake .so
    - Session-scoped pytest fixtures for one-shot reflection imports
    - Generator-expression ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>" (Phase 2 Pitfall-7 carryover)
key-files:
  created:
    - python/spu94/__init__.py
    - python/spu94/_binding.py
    - python/spu94/presets.py
    - tests/python/binding/__init__.py
    - tests/python/binding/conftest.py
    - tests/python/binding/CMakeLists.txt
    - tests/python/binding/test_binding_surface.py
    - tests/python/binding/test_binding_register_intenum.py
    - tests/python/binding/test_binding_preset_table.py
    - tests/python/binding/test_binding_drift_detection.py
  modified:
    - tests/python/CMakeLists.txt (one appended add_subdirectory call)
decisions:
  - Task 2 stub presets.py intentionally shipped with presets = None
    placeholder so __init__.py's `from .presets import Preset, presets` resolves
    during Task 2; Task 3 replaces presets.py with the full in_dll-backed
    implementation. Kept task-atomic diffs intact.
  - PatchedCDLL (in test_binding_drift_detection.py) chosen over building
    a separate fake .so — single-file test, no extra CMake surface, and the
    shim is narrow enough that it only intercepts the one symbol each test
    needs to perturb.
  - preset-name drift assertion uses a pinned _EXPECTED_NAMES tuple inside
    presets.py rather than pushing it to a separate config / JSON file.
    Intentional tripwire: any future preset reorder/rename requires a visible
    source edit + commit message linking to a DECISIONS.md update.
  - bool keys to _PresetTable.__getitem__ are explicitly rejected with
    TypeError. Without the guard, bool would silently resolve to True→1 /
    False→0 via Python's int-subclass behaviour; the explicit rejection
    surfaces caller bugs early.
  - Python import time measured at 22.2 ms on the dev workstation (not
    the "<5 ms" plan estimate). The extra cost comes from building the
    PresetInfo dataclasses and materializing 10×35 int16 tuples; still a
    one-time cost per process and well within the "accept" disposition
    for T-06-06. No optimization needed at this plan.
metrics:
  duration: ~1h (context load + 3 TDD tasks + SUMMARY)
  completed: 2026-04-21T21:26Z
---

# Phase 6 Plan 1: ctypes Binding Foundation Summary

Phase 6 Plan 1 lands the ctypes binding foundation: a dev-importable `spu94`
Python package whose `_lib` CDLL handle has argtypes/restype set for every
public C symbol, a 35-member `Register` IntEnum built by runtime reflection
against the live `libspu94.so`, a 10-member `Preset` IntEnum with an
`in_dll`-backed `presets` accessor, and import-time drift assertions that
catch struct-size / enum-count / preset-name drift loudly with
`RuntimeError("spu94 library mismatch: ...")`.

## Tasks Executed

| # | Name | Commit | TDD | Files |
|---|------|--------|-----|-------|
| 1 | Wave 0 — test scaffolding + ctest `binding` label wiring | `3600910` | no | 8 |
| 2 RED | Surface / register-intenum / drift RED tests | `9811e98` | yes | 3 |
| 2 GREEN | _binding.py + __init__.py + presets.py stub | `48454cd` | yes | 3 |
| 3 RED | Preset-table RED tests | `8bdbb1b` | yes | 1 |
| 3 GREEN | presets.py full in_dll-backed implementation | `92949d9` | yes | 1 |

All commits carry the `06-01` scope.

## Final Line Counts

| File | Lines |
|------|-------|
| python/spu94/__init__.py | 116 |
| python/spu94/_binding.py | 177 |
| python/spu94/presets.py | 211 |
| tests/python/binding/conftest.py | 70 |
| tests/python/binding/CMakeLists.txt | 33 |
| tests/python/binding/test_binding_surface.py | 97 |
| tests/python/binding/test_binding_register_intenum.py | 74 |
| tests/python/binding/test_binding_preset_table.py | 137 |
| tests/python/binding/test_binding_drift_detection.py | 156 |
| tests/python/binding/__init__.py | 0 |

All files comfortably clear the plan's `min_lines` thresholds
(`__init__.py` 116 ≥ 80; `_binding.py` 177 ≥ 120; `presets.py` 211 ≥ 60;
surface 97 ≥ 40; register_intenum 74 ≥ 20; preset_table 137 ≥ 30;
drift 156 ≥ 40; CMakeLists 33 ≥ 30).

## Live Library Reflection Numbers

Measured at import time on the dev workstation against
`build/src/spu94/libspu94.so`:

- `len(spu94.Register)` = **35** — matches `SPU94_REG__COUNT`.
- `len(spu94.Preset)` = **10** — matches `SPU94_PRESET__COUNT`.
- `len(spu94.presets)` = **10** — matches `SPU94_PRESET__COUNT`.
- `spu94.presets['hall'].id` = **5** — matches `SPU94_PRESET_HALL`.
- `spu94.Register.vIIR.value` = **5** — matches `SPU94_REG_vIIR`.
- `spu94._lib.spu94_state_size()` = **168 bytes** ≤ `SPU94_STATE_SIZE_MAX`.
- `spu94._lib.spu94_get_latency_samples()` = **58** = `SPU94_LATENCY_SAMPLES`.

## Import-Time Performance

```
import_ms = 22.2 ms  (Python 3.13.7; dev workstation; cold import)
```

Breakdown of work at `import spu94`:

1. `ctypes.CDLL(libspu94.so)` — one `dlopen`.
2. Twenty `_lib.<fn>.argtypes` / `.restype` assignments.
3. Drift assertion: one `spu94_state_size()` call.
4. Register reflection: 36 `spu94_reg_name(i)` calls (35 + sentinel).
5. Preset reflection: `(_CPreset * 10).in_dll(_lib, "spu94_presets")`.
6. Preset-name drift check: 10 byte-string decodes + compares.
7. Building 10 `PresetInfo` dataclasses, each with a 35-tuple of int16 values.

The plan's threat-register T-06-06 estimated `< 5 ms` for the reflection
step. The measured 22 ms is a honest restatement — the plan's estimate
underweighted the preset table materialization (10 PresetInfo objects ×
35-tuple construction). The `accept` disposition stands: this is a
one-time cost per process, and no consumer will pay it on a per-call
basis.

## Test Coverage

**Binding suite (`ctest -L binding`):** 4/4 green in ~0.9 s wall time.

| Test | Functions | Green? | Covers |
|------|-----------|--------|--------|
| test_binding_surface | 5 | yes | PYBIND-01 — 20 C functions' argtypes/restype + 9 constants |
| test_binding_register_intenum | 6 | yes | PYBIND-03 — 35-member IntEnum + ABI-order + bare names |
| test_binding_preset_table | 10 | yes | PYBIND-04 — string/enum/int keys, iteration, Off gains, behavioral witness via `spu94_load_preset` + `spu94_tick` + `spu94_snapshot_registers` |
| test_binding_drift_detection | 4 | yes | PYBIND-05 — state-size overflow, register count shrank, register count grew, positive-case import |

**Non-fuzz full suite (`ctest -LE "fuzz"`):** 54/54 green in ~107 s,
including all Phase 2 register / Phase 3 reverb / Phase 4 FIR / Phase 5
RT-safety / Phase 5 preset-loader tests. Confirms zero regression against
Phases 1–5.

**Full suite including 10-min fuzz:** ran at the end of Task 1 (before
any Python-side code landed against the same libspu94.so) — 56/56 green
in ~670 s. Tasks 2 and 3 modified only Python sources (no C code, no
shared-lib rebuild), so the earlier full-suite green applies to the
plan's final state.

## Requirements Satisfied

- **PYBIND-01** — Full C API surface reachable via ctypes with correct
  argtypes/restype. 20 public symbols declared in `_binding.py` and
  cross-checked by `test_binding_surface::test_all_functions_present`.
- **PYBIND-03** — `spu94.Register` IntEnum has exactly 35 members whose
  names match the ABI order and whose values are 0..34. Reflected from
  the live library at import time via `spu94_reg_name`.
- **PYBIND-04** — `spu94.presets[name]` / `spu94.presets[Preset.X]` /
  `spu94.presets[int]` all resolve to the same 35-element int16 tuple
  matching `spu94_presets[].regs`. Normalized keys accept "Studio A" /
  "studio_a" / "STUDIO_A". Element-wise equality verified against
  `spu94_load_preset` + `spu94_tick` + `spu94_snapshot_registers` for Hall.
- **PYBIND-05** — Import-time drift assertions fire with
  `RuntimeError("spu94 library mismatch: ...")` on state-size overflow,
  register-count shrink, register-count grow, and preset-name mismatch.

Remaining Phase 6 requirements (PYBIND-02 numpy contract, PYBIND-06 wheel
packaging, CLI-01..04, DOCS-04 README) are delivered by Plans 2–5.

## Decisions Made

1. **Task 2 `presets.py` stub vs holding `__init__.py`'s import line.**
   The plan's `__init__.py` skeleton hard-codes `from .presets import
   Preset, presets`. Task 2's `<files>` omits `presets.py`; Task 3
   claims it. Picked: ship a minimal Task 2 stub (just `Preset` enum +
   `presets = None`) so `__init__.py` imports cleanly during Task 2's
   GREEN, then replace the full file in Task 3. Result: per-task commits
   stay atomic; Task 3's diff is a clean module-level rewrite.

2. **PatchedCDLL vs a fabricated fake .so for drift tests.**
   The plan's skeleton offered both. Picked: `PatchedCDLL` shim class
   inside `test_binding_drift_detection.py`. Rationale: one file, no
   CMake surface change, no fragile fake-.so generation step. The shim
   delegates most calls to a real CDLL handle and overrides only the
   specific symbol each test perturbs.

3. **`_EXPECTED_NAMES` drift assertion in `presets.py`.**
   Intentional tripwire against T-06-04. Any future preset reorder /
   rename forces an edit to `_EXPECTED_NAMES` (a visible, reviewable
   source change) plus a commit message linking to a DECISIONS.md
   update — matching the project's "no silent divergences" posture.

4. **Reject `bool` keys explicitly in `_PresetTable.__getitem__`.**
   `bool` is an `int` subclass in Python, so `presets[True]` would
   silently return preset 1 (ROOM). Adding an `isinstance(key, bool)`
   guard that raises TypeError surfaces caller bugs early. Cheap
   discipline, matches the rest of the project's fail-loudly stance.

5. **Functional `IntEnum(...)` API for `Register`.**
   The plan's acceptance criteria explicitly forbade `class Register:
   ...`. Using `IntEnum("Register", members, module=__name__)` keeps
   the 35 members generated from `spu94_reg_name`, not hard-coded —
   single source of truth stays with the live library.

## Deviations from Plan

None. The plan's skeletons were followed with three minor, documented
refinements:

1. **Task 2 `presets.py` stub introduced** (discussed above). The plan
   listed `presets.py` only under Task 3's `<files>`, but Task 2's
   `__init__.py` skeleton imports from it. Deferring the full module
   to Task 3 while shipping a minimal stub in Task 2 keeps TDD commit
   atomicity clean and was the simplest reading of the plan's intent.
   Not flagged as a Rule-2 deviation because the stub is strictly a
   placeholder, not missing functionality.

2. **Drift-test shim uses `_ShimFnPtr` callable class** rather than the
   skeleton's `type("F", (), {"__call__": staticmethod(...), ...})()`
   pattern. Cleaner, easier to read, identical behavior. Within the
   planning note that said "adjust as needed so that `ctypes.CDLL` is
   patched ... If the cleaner approach is a shim .so or a
   `types.SimpleNamespace`, use that."

3. **Added `test_normalized_name_aliases` and
   `test_unknown_name_raises_keyerror`** on top of the plan's 8 listed
   preset tests. Covers the explicit space→underscore normalization
   contract (D-03 CLI --preset matching) and the KeyError-with-valid-
   list error path that Plan 3's CLI will consume. Additive, not a
   substitution.

## Known Stubs

None. Task 2's `presets = None` placeholder was replaced by Task 3
with the full `_PresetTable()` instance; no placeholder survives in the
plan's final state.

## Threat Flags

None. Plan 1's scope is strictly library loading + reflection +
drift detection. The threat register's six IDs (T-06-01..T-06-06) all
had dispositions assigned in-plan and were satisfied as described. No
new surface introduced outside the plan's `<threat_model>`.

## CONTEXT Decisions Satisfied by This Plan

- **D-01** (expose both layers — raw-panel + SPU94 class): scaffolding only;
  Plan 1 ships `_lib` + constants + reflection; Plan 2 lands the
  raw-panel API and the class wrapper.
- **D-02** (factory presets importable): **fully satisfied**.
  `spu94.presets["hall"]` / `spu94.presets[Preset.HALL]` / `spu94.presets[5]`
  all resolve; iteration yields PresetInfo objects in preset-id order.
- **D-06** (runtime reflection builds IntEnum): **fully satisfied**.
  `Register` is built by walking `spu94_reg_name(0..34)`; the sentinel
  `spu94_reg_name(35)` must return NULL. No hand-typed Python enum.
- **D-07** (import-time drift assertions): **fully satisfied**.
  state-size overflow, register count grow, register count shrink,
  and preset-name drift all raise `RuntimeError("spu94 library
  mismatch: ...")`. Four test cases defend the failure paths.
- **D-08** (struct-internal offsets stay hand-typed in fuzz scripts):
  not touched this plan. Fuzz-script migration is Plan 5.
- **D-17** (fuzz migration compatibility): Plan 1 publishes a clean
  consumption surface (`from spu94._binding import _lib,
  SPU94_REG__COUNT, SPU94_OK, ...`) exactly as Plan 5 will need. No
  further work this plan.

## ADR Candidates

- **Plan 1 ADR draft:** `_EXPECTED_NAMES` as the drift-lock mechanism
  for `spu94_presets[]` — intentional tripwire that forces future
  preset-table changes to land alongside a documented DECISIONS.md
  entry. Small enough to fold into a future ADR about the Phase 6
  binding's drift-defense posture (the three layers: state-size check,
  register-count sentinel check, preset-name check) rather than its
  own ADR. Recommend a single combined Phase-6 ADR once Plan 2 lands
  its numpy-contract + SPU94 class.

## Self-Check: PASSED

Files claimed to exist and spot-checked:

- `FOUND: python/spu94/__init__.py`
- `FOUND: python/spu94/_binding.py`
- `FOUND: python/spu94/presets.py`
- `FOUND: tests/python/binding/__init__.py`
- `FOUND: tests/python/binding/conftest.py`
- `FOUND: tests/python/binding/CMakeLists.txt`
- `FOUND: tests/python/binding/test_binding_surface.py`
- `FOUND: tests/python/binding/test_binding_register_intenum.py`
- `FOUND: tests/python/binding/test_binding_preset_table.py`
- `FOUND: tests/python/binding/test_binding_drift_detection.py`

Commits claimed and verified via `git log`:

- `FOUND: 3600910` (Task 1 — Wave 0 scaffolding)
- `FOUND: 9811e98` (Task 2 RED)
- `FOUND: 48454cd` (Task 2 GREEN)
- `FOUND: 8bdbb1b` (Task 3 RED)
- `FOUND: 92949d9` (Task 3 GREEN)

Verification commands (all green, exit 0):

- `ctest --test-dir build -L binding` — 4/4 pass
- `ctest --test-dir build -LE "fuzz"` — 54/54 pass (non-fuzz regression gate)
- `ctest --test-dir build` full suite — 56/56 pass (from Task 1 boundary;
  Tasks 2+3 were Python-only)
- `python3 -c "import spu94; ..."` — import succeeds, prints expected
  live-library numbers
