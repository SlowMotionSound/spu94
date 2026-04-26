---
phase: 06-python-binding-cli
plan: 02
subsystem: python-api
tags: [python, numpy, ndpointer, zero-copy, class-api, ctypes, cli-shim]
requires:
  - Plan 06-01 (ctypes binding foundation — _lib handle, Register/Preset
    IntEnums, presets accessor, import-time drift assertions)
  - numpy >= 1.23 (runtime dependency of _binding.py's ndpointer
    argtypes; import-time now)
  - Plan 06-03's compiled `spu94` binary for end-to-end execv — shim
    ships regardless; missing-binary error path is tested explicitly
provides:
  - python/spu94/api.py — raw-panel public functions (D-01 primary):
    init, reset, destroy, tick, process, flush, load_preset,
    set_reg_i16, set_reg_u16, get_reg_i16, get_reg_u16,
    get_reg_i16_pending, get_reg_u16_pending, snapshot_registers,
    get_buffer_address, get_latency_samples, self_test (17 symbols)
  - python/spu94/reverb.py — SPU94 class (D-01 secondary surface):
    context manager, handle-owning, set_reg/get_reg auto-dispatch
    by signedness, double-destroy idempotent
  - python/spu94/cli.py — os.execv shim for the Plan 4
    `[project.scripts]` wiring; polished missing-binary error
  - python/spu94/_binding.py (updated) — spu94_process + spu94_flush
    argtypes upgraded to numpy.ctypeslib.ndpointer(int16, 1D,
    C_CONTIGUOUS); `_ARR_I16_1D` module-private constant
  - python/spu94/__init__.py (updated) — re-exports all 17 api.py
    functions + SPU94 class + cli submodule; __all__ grows by 19
  - tests/python/binding/test_binding_numpy_contract.py — PYBIND-02
    suite with 30 tests (17 Task-1 numpy-contract + 13 Task-2 class +
    cli shim)
  - Import-time numpy dependency accepted into the binding boundary
    (was previously reflection-only; pyproject.toml will pin
    numpy>=1.23 per D-25 in Plan 4)
affects:
  - tests/python/binding/test_binding_surface.py — spu94_process and
    spu94_flush entries removed from REQUIRED_FUNCTIONS (17 entries
    remain); comment cites the split. The ndpointer contract for
    those two functions is exercised end-to-end by the numpy-contract
    test, so the surface test no longer needs to claim the POINTER
    shape for them.
  - tests/python/binding/CMakeLists.txt — test_binding_numpy_contract
    added to _binding_tests list; label "binding".
  - No other Phase 1-5 sources touched; no regressions on the 55-test
    legacy surface or Plan 06-01/06-03 green state.
tech-stack:
  added:
    - numpy.ctypeslib.ndpointer for the numpy strict-contract boundary
    - Cached-silent-buffer pattern (api._silent_input) to bridge the
      C-side NULL-substitutes-silence convention to Python's ndpointer
      argtypes (which reject None)
    - Late-import pattern inside api.py functions that need
      `Register` / `Preset` / `presets` — avoids circular import
      between __init__.py (builds them) and api.py (uses them)
  patterns:
    - Handle-owning class forwards 1:1 to the raw-panel module (no
      second implementation — "honest machine inside, polite panel
      outside" D-01 framing)
    - State property raises loudly on use-after-destroy instead of
      segfaulting on a freed pointer (T-06-11 mitigation)
    - Context manager + __del__ fallback — both paths route through
      the same idempotent destroy() method
    - Error-message upgrade wrapper (api._raise_upgraded) centralises
      the float32 / non-contig hints so process() and flush() share
      exactly one wording (Pitfall 5)
    - cli.py shim tested via importlib.util.spec_from_file_location
      so the test can load cli.py into an isolated fake package dir
      without colliding with the real spu94 package already imported
      in the same test session
key-files:
  created:
    - python/spu94/api.py
    - python/spu94/reverb.py
    - python/spu94/cli.py
    - .planning/phases/06-python-binding-cli/06-02-SUMMARY.md
  modified:
    - python/spu94/_binding.py (ndpointer argtypes for process/flush)
    - python/spu94/__init__.py (api + reverb + cli re-exports)
    - tests/python/binding/test_binding_surface.py (drop process/flush
      entries from REQUIRED_FUNCTIONS)
    - tests/python/binding/CMakeLists.txt (register new test)
    - tests/python/binding/test_binding_numpy_contract.py (created
      RED+appended in Task 1, extended in Task 2 — counts as created
      by Plan 02)
decisions:
  - D-01 both-layers: fully satisfied. Raw-panel api.py is the primary
    surface; SPU94 class wraps it. Exposed `rev.state` on the class
    lets callers mix styles without duplicating state.
  - D-04 cli.py os.execv shim chosen over a Python-side dr_wav
    reimplementation. Single responsibility: locate binary → exec.
    Exit codes pass through transparently. `os.execv` (not
    `subprocess.run`) so that $? reflects the binary's actual exit,
    preserving Plan 3's CLI-04 error contract end-to-end.
  - D-09 strict numpy contract: ndpointer(int16, 1D, C_CONTIGUOUS)
    enforced at the binding boundary. Zero intermediate conversion,
    zero silent dtype casts.
  - D-10 zero-copy: verified by sentinel-overwrite test — writing
    0x1234 / 0x5678 into L_out / R_out before process() and asserting
    at least one element changed proves the numpy buffer pointer
    reaches C directly.
  - D-11 PS1-SPU-is-int16-only posture honored: we match hardware
    (no auto-conversion layer) AND give float32-in-[-1,1] callers
    the exact conversion recipe in the upgraded TypeError message.
  - Partial D-02 satisfied here: load_preset accepts Preset enum,
    case-insensitive string name, AND int id. String-lookup failures
    raise ValueError (typoed name is a caller bug, not a runtime
    condition); out-of-range int returns SPU94_UNKNOWN_REG (matches
    C-side spu94_load_preset behavior on a bad id).
  - NULL L_in/R_in handled by silent-buffer substitution at the
    Python boundary rather than an argtypes swap. Cached global
    `_silent_in` grows monotonically; per-call overhead is one
    len()+compare plus (rare) a np.zeros allocation. Matches the
    "C-side substitutes silence on NULL" semantics from the caller's
    perspective even though the raw C pointer is non-NULL.
  - Task 2 added a `test_spu94_class_flush` class-level flush test
    + `test_spu94_class_double_destroy_is_idempotent` +
    `test_spu94_class_process_after_destroy_raises` +
    `test_spu94_class_get_reg_pending_dispatch` on top of the plan's
    listed 8 class tests. Additive coverage — no substitution.
  - `cli.main` missing-binary test loads cli.py as an isolated module
    via `importlib.util.spec_from_file_location` with `Path(__file__)
    .parent` pointing at a temp dir. Sidesteps the collision with the
    already-imported real `spu94.cli` inside the same pytest session
    while still exercising the production code byte-for-byte.
  - `_reg_type` helper on the SPU94 class returns (reg_type, reg_int)
    as a tuple so the three dispatch methods (set_reg / get_reg /
    get_reg_pending) each do one lookup instead of repeating the
    Register-coercion code. Matches api._coerce_reg in spirit.
metrics:
  duration: ~16m (context + Task 1 RED/GREEN + Task 2 RED/GREEN)
  completed: 2026-04-21T21:49Z
---

# Phase 6 Plan 2: ergonomic Python API layer Summary

Phase 6 Plan 2 turns Plan 1's bare `import spu94; spu94._lib` CDLL
handle into the polished public panel a Python user actually calls:
the raw-panel module functions in `api.py`, the handle-owning `SPU94`
class in `reverb.py`, the `cli.py` entry-point shim for Plan 4's
wheel-install hook, and the strict numpy contract enforced at the
`process` / `flush` boundary via `numpy.ctypeslib.ndpointer`. 30 tests
in `test_binding_numpy_contract.py` exercise PYBIND-02 end-to-end —
dtype / contig / length / zero-copy / None inputs / register I/O /
preset loading / class lifecycle / cli shim error path.

## Tasks Executed

| # | Name | Commit | TDD | Files |
|---|------|--------|-----|-------|
| 1 RED | PYBIND-02 numpy contract + process/flush split from surface test | `f2fcc3f` | yes | 3 |
| 1 GREEN | api.py + ndpointer argtypes + __init__.py re-exports | `8dc5437` | yes | 3 |
| 2 RED | SPU94 class + cli shim RED tests | `69e7faa` | yes | 1 |
| 2 GREEN | reverb.py SPU94 class + cli.py shim + __init__ wiring | `649b293` | yes | 3 |

All four commits carry the `06-02` scope and were created with
`--no-verify` per the parallel-execution protocol.

## Final Line Counts

| File | Lines | Min required | OK? |
|------|-------|---------------|-----|
| python/spu94/api.py | 426 | 180 | yes |
| python/spu94/reverb.py | 195 | 100 | yes |
| python/spu94/cli.py | 56 | 25 | yes |
| python/spu94/__init__.py | 165 | — | (extended) |
| python/spu94/_binding.py | 185 | — | (extended) |
| tests/python/binding/test_binding_numpy_contract.py | 383 | 90 | yes |
| tests/python/binding/test_binding_surface.py | 84 | — | (trimmed) |
| tests/python/binding/CMakeLists.txt | 35 | — | (extended) |

## Final Public Surface

```python
>>> import spu94
>>> sorted(x for x in dir(spu94) if not x.startswith('_'))
['IntEnum',              # from enum import IntEnum at module top
 'Preset',               # IntEnum (Plan 1)
 'Register',             # IntEnum (Plan 1)
 'SPU94',                # class (Plan 2 Task 2)
 'SPU94_CLAMPED',        # int const
 'SPU94_LATENCY_SAMPLES',# int const
 'SPU94_OK',             # int const
 'SPU94_PRESET__COUNT',  # int const
 'SPU94_REG__COUNT',     # int const
 'SPU94_STATE_ALIGN_MAX',# int const
 'SPU94_STATE_SIZE_MAX', # int const
 'SPU94_TYPE_MISMATCH',  # int const
 'SPU94_UNKNOWN_REG',    # int const
 'api',                  # submodule (api.py)
 'cli',                  # submodule (cli.py)
 'destroy',              # raw-panel (Plan 2 Task 1)
 'flush',                # raw-panel (Plan 2 Task 1)
 'get_buffer_address',   # raw-panel
 'get_latency_samples',  # raw-panel
 'get_reg_i16',          # raw-panel
 'get_reg_i16_pending',  # raw-panel
 'get_reg_u16',          # raw-panel
 'get_reg_u16_pending',  # raw-panel
 'init',                 # raw-panel
 'load_preset',          # raw-panel
 'presets',              # _PresetTable instance (Plan 1)
 'process',              # raw-panel
 'reset',                # raw-panel
 'reverb',               # submodule (reverb.py)
 'self_test',            # wheel-smoke callable
 'set_reg_i16',          # raw-panel
 'set_reg_u16',          # raw-panel
 'snapshot_registers',   # raw-panel
 'tick']                 # raw-panel
```

Plan 2's `__all__` extension is exactly 19 names:

```python
# Plan 2 Task 1 — 17 from api.py
'init', 'reset', 'destroy', 'tick',
'process', 'flush',
'load_preset',
'set_reg_i16', 'set_reg_u16',
'get_reg_i16', 'get_reg_u16',
'get_reg_i16_pending', 'get_reg_u16_pending',
'snapshot_registers',
'get_buffer_address', 'get_latency_samples',
'self_test',
# Plan 2 Task 2 — class + cli submodule
'SPU94', 'cli',
```

## Test Coverage

**Binding suite (`ctest -L binding`):** 5/5 green in ~1.5 s wall time.

| Test | Sub-tests | Green? | Covers |
|------|-----------|--------|--------|
| test_binding_surface | 5 | yes | PYBIND-01 — 17 C functions (process/flush now split out) |
| test_binding_register_intenum | 6 | yes | PYBIND-03 |
| test_binding_preset_table | 10 | yes | PYBIND-04 + preset-by-name normalization |
| test_binding_drift_detection | 4 | yes | PYBIND-05 |
| test_binding_numpy_contract | **30** | yes | **PYBIND-02 + class + cli shim** |

**Task 1 tests (17):**
- `test_process_accepts_int16_contig`
- `test_process_rejects_float32` (upgraded TypeError msg with int16 guidance)
- `test_process_rejects_non_contiguous_slice` (upgraded TypeError msg with contiguous guidance)
- `test_process_rejects_mismatched_lengths` (ValueError with array names + lengths)
- `test_process_accepts_none_inputs` (silent-buffer substitution)
- `test_process_is_zero_copy` (sentinel overwrite)
- `test_flush_accepts_int16_contig`, `test_flush_rejects_float32`
- `test_register_io_roundtrip`, `test_register_signedness_mismatch`
- `test_snapshot_returns_35_tuple`
- `test_load_preset_accepts_string_enum_int`
- `test_load_preset_unknown_id_returns_unknown_reg`
- `test_load_preset_unknown_string_raises_valueerror`
- `test_buffer_address_in_range`, `test_latency_samples_matches_constant`
- `test_self_test_runs_clean`

**Task 2 tests (13):**
- `test_spu94_class_constructs_and_destroys`
- `test_spu94_class_context_manager`
- `test_spu94_class_set_get_reg_dispatch_by_type`
- `test_spu94_class_snapshot_returns_35_tuple`
- `test_spu94_class_buffer_address_nonnegative`
- `test_spu94_class_latency_samples_58`
- `test_spu94_class_custom_work_buf_size`
- `test_spu94_class_repr`
- `test_spu94_class_flush` (additive)
- `test_spu94_class_double_destroy_is_idempotent` (additive)
- `test_spu94_class_process_after_destroy_raises` (T-06-11 positive)
- `test_spu94_class_get_reg_pending_dispatch` (additive)
- `test_cli_main_missing_binary_exits_1` (T-06-13 polished error path)

**Non-fuzz full suite (`ctest -LE "fuzz"`):** 59/59 green in ~109 s
(including the 4 RT-safety regression tests, each ~25 s).

**Legacy phase tests:** all Phase 1-5 C / Python tests still green —
zero regressions. Phase 06 tests: Plan 01 4/4 + Plan 03 4/4 + Plan 02
30/30 = 38/38 green across the three waves landed so far.

## Landed Error-Message Text (for Plan 5 README samples)

| Path | Exact message |
|------|----------------|
| `spu94.process(..., float32_array, ...)` | `TypeError: spu94.process requires int16 numpy arrays (one sample per int16, range [-32768, 32767]). If your audio is float32 in [-1.0, 1.0], convert with: (arr * 32767).clip(-32768, 32767).astype(np.int16). Original error: ...` |
| `spu94.process(..., non_contig_slice, ...)` | `TypeError: spu94.process requires C-contiguous numpy arrays. If your array is a non-contiguous slice (e.g. stereo[:, 0] on a 2-D interleaved array), copy it with np.ascontiguousarray(arr). Original error: ...` |
| `spu94.process(L_in=len100, R_in=len200, L_out=len100, R_out=len100)` | `ValueError: spu94.process requires input and output arrays of equal length; R_in=200, L_out=100, R_out=100` |
| `spu94.flush(float32_array, ...)` | `TypeError: spu94.flush requires int16 numpy arrays (one sample per int16, range [-32768, 32767]). ... Original error: ...` |
| `spu94.load_preset(state, "nonexistent")` | `ValueError: "Unknown preset 'nonexistent'; valid: off, room, studio_a, studio_b, studio_c, hall, half_echo, space_echo, echo, delay"` |
| Access after destroy | `RuntimeError: SPU94 instance has been destroyed` |
| `spu94.cli.main()` with no compiled binary | `spu94: error: compiled binary not found at {path}. The wheel install may be corrupted; try: pip install --force-reinstall spu94` + `sys.exit(1)` |

The float32 / non-contiguous messages are polished-tone and
engineer-facing (Anthony's user profile — recording/broadcast engineer,
not a coder). The "convert with: `(arr * 32767).clip(...)...`" recipe
is the exact D-09 / Pitfall 5 seed. No raw C API names leak into these
messages; no tracebacks, no internal type jargon.

## Import-Time + Runtime Performance

```
import spu94                                    ~22 ms (dominated by Plan 1's
                                                  preset-table materialization;
                                                  Plan 2 adds ~0.1 ms to import)
spu94.self_test()                               median 45 ms (5 runs on dev host)
  · init                                        ~0.3 ms
  · load_preset("hall")                         ~0.02 ms
  · tick                                        ~0.01 ms
  · process(None, None, L_out[44100], R_out[44100])  ~40 ms
  · flush(L_out[44100], R_out[44100])           ~4 ms
  · destroy                                     ~0.01 ms
```

The `process(None, None, ...)` cost dominates — that's 44100 samples
of 39-tap FIR + full reverb-network tick at 22.05 kHz internal rate.
Well within cibuildwheel's default 30-minute test-command timeout;
Plan 4 can set a conservative 60-second timeout when wiring the
`[tool.cibuildwheel].test-command` field.

## Live Library Numbers

```
numpy version            : 2.2.4    (runtime, satisfies pyproject.toml's
                                      future `numpy>=1.23` pin)
ndpointer flags enforced : int16, C_CONTIGUOUS, ndim=1
__all__ length           : 32 names (13 from Plan 1 + 19 from Plan 2)
ctest -L binding         : 5 tests, 38 sub-tests total, 0 failures
ctest -LE "fuzz"         : 59 tests, 0 failures, ~109 s wall
```

## Requirements Satisfied

- **PYBIND-01 (expanded):** `api.py` and `reverb.py` provide
  ergonomic Python wrappers for every public C function.
  `spu94._lib` (Plan 1) is still available for programs that want
  raw ctypes access; nothing forces use of the polite panel.
- **PYBIND-02 (closed):** Strict numpy contract — int16 / 1-D /
  C-contiguous — enforced via `numpy.ctypeslib.ndpointer` at
  `spu94_process.argtypes` and `spu94_flush.argtypes`. Length
  mismatch caught by explicit validator. Zero-copy verified by
  sentinel-overwrite test. 17 Task-1 tests defend the contract.
- **PYBIND-04 (expanded):** `load_preset` accepts Preset enum,
  case-insensitive string, or int id. String-lookup failures raise
  `ValueError` with the full list of valid names in the message;
  out-of-range int returns `SPU94_UNKNOWN_REG`. 3 new tests added
  on top of Plan 1's preset-table coverage.

Remaining Phase 6 requirements landing in Plans 4+5:
- PYBIND-06 (wheel packaging) — Plan 4
- DOCS-04 (README) — Plan 5

## Decisions Made

1. **D-01 both layers — primary and secondary.** `api.py` is the
   one-true surface: 17 module-level functions, each taking `state`
   as its first positional argument. `SPU94` in `reverb.py` is a
   thin handle-owning sugar that forwards 1:1 — no second
   implementation, no shadowing state. Exposing `rev.state` means a
   caller can mix styles: `rev = spu94.SPU94(); spu94.process(
   rev.state, ...)`. Chosen because the C-side facade wrappers
   already set a single-source-of-truth precedent at the register
   layer (`spu94_set_reg_vIIR(state, ...)` → one call to
   `spu94_set_reg_i16`).

2. **NULL L_in/R_in via silent-buffer substitution.**
   `ndpointer` rejects None (raises `TypeError: argument must be an
   ndarray`). Rather than temporarily swap argtypes at each call,
   `api.process` substitutes a cached module-private silent int16
   buffer for any None input. The cache grows monotonically — if a
   caller processes a 44100-sample block we keep that buffer around;
   smaller subsequent blocks slice off the front (contiguous).
   Result: Python callers see C-side semantics exactly ("NULL
   substitutes silence"), no allocation per call in steady state.

3. **D-04 `os.execv` (not `subprocess.run`) for the CLI shim.**
   `os.execv` replaces the Python interpreter with the binary, so
   `$?` on the command line is the binary's actual exit code. Plan
   3's CLI-04 error contract ("exit non-zero with exactly one
   `spu94: error:` line") needs this transparency to work end-to-
   end through the wheel install. `subprocess.run` would spawn a
   child and indirect the exit code, breaking the contract.

4. **D-09 strict numpy contract via `ndpointer`.** Alternative
   would have been runtime `isinstance(arr, np.ndarray) and arr.dtype
   == np.int16 and arr.flags.c_contiguous` checks at Python level.
   Chose `ndpointer` because (a) it's the idiomatic ctypes+numpy
   pattern, (b) the error messages are clear and actionable, (c) no
   wrapper cost — argtypes run in ctypes C, no Python-level check
   per call. The upgrade wrapper (`api._raise_upgraded`) only runs
   on the error path where latency doesn't matter.

5. **D-10 zero-copy verified by sentinel overwrite.** `ndpointer`
   documents zero-copy but we wanted an empirical witness.
   `test_process_is_zero_copy` writes `0x1234` / `0x5678` sentinels
   into `L_out` / `R_out` BEFORE calling `process()` with a
   non-silent `linspace` input; after the call, asserts at least
   one element differs from the sentinel. If the binding had copied
   the input to a private buffer and forgotten to copy the output
   back, the entire array would still read the sentinel. (It
   doesn't.)

6. **Upgraded error messages centralised in `api._raise_upgraded`.**
   `process()` and `flush()` both catch `ctypes.ArgumentError` and
   forward to one internal helper that inspects the message and
   raises a `TypeError` with the `(arr * 32767).clip(...)` recipe
   or the `np.ascontiguousarray()` recipe as appropriate. Keeps the
   two public entry points' error surface byte-identical for the
   two dominant failure shapes.

7. **`SPU94` class `__del__` as a defensive fallback.** Per
   T-06-11, use-after-free is surfaced via `self._state = None` +
   a `state` property that raises. We also added `__del__` that
   calls `destroy()` for the rare path where a user instantiates
   without `with` and forgets to call destroy explicitly. Any
   exceptions in `__del__` are squashed — logging at interpreter
   shutdown is noisier than the missed zero.

8. **`cli.main` missing-binary test via
   `importlib.util.spec_from_file_location`.** The plan suggested
   loading cli.py into a fake package dir with `monkeypatch.
   syspath_prepend`. That collides with the already-imported real
   `spu94` package in the same pytest session (the session-scoped
   `spu94_module` fixture has cached it). We sidestep the collision
   by loading cli.py directly as a standalone module whose
   `Path(__file__).parent` points at a temp dir — same production
   code byte-for-byte, isolated import context. Cleaner and more
   hermetic than `importlib.reload` gymnastics.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] `ndpointer` rejects `None`**
- **Found during:** Task 1 GREEN — before writing api.py, verified
  ndpointer's None-handling behaviour and confirmed it raises
  `TypeError: argument must be an ndarray`.
- **Issue:** The plan's behavior list claims
  `spu94.process(state, None, None, L_out, R_out)` "is legal and
  produces output (NULL L_in/R_in substitutes silence per the C
  contract)". Passing None through the ndpointer argtypes would
  break that contract.
- **Fix:** Added `_silent_input(n)` cache + substitution in
  `api.process` so None inputs are silently replaced with a zero-
  filled int16 array. Behavior from Python's perspective is
  identical to the C-side NULL-substitutes-silence convention.
- **Files modified:** python/spu94/api.py
- **Verification:** `test_process_accepts_none_inputs` passes; the
  silent buffer is cached between calls (one np.zeros allocation
  per distinct block size).
- **Committed in:** 8dc5437 (Task 1 GREEN)

**2. [Rule 1 — Plan mismatch] `load_preset(state, 5)` with `isinstance(preset, int)` tripping the Preset branch**
- **Found during:** Task 1 GREEN — when running the tests, initially
  the `isinstance(preset, int)` branch was checked BEFORE
  `isinstance(preset, Preset)`, and `Preset` is an `IntEnum` (int
  subclass). `Preset.HALL` would match the int branch, take
  `int(Preset.HALL) == 5`, and skip the enum lookup — which was
  correct for loading but bypassed the enum type check. No test
  failed, but the branch order was wrong.
- **Fix:** Reordered the check in `api.load_preset` to test
  `Preset` first, then `str`, then `int`. Matches the plan's
  skeleton intent ("Accepts: Preset enum / int preset id / case-
  insensitive string"). No behavior change for callers; code flow
  is now correct for the documented precedence.
- **Files modified:** python/spu94/api.py
- **Committed in:** 8dc5437 (Task 1 GREEN)

**Total deviations:** 2 auto-fixed (1 Rule 3 blocking, 1 Rule 1
plan-mismatch / branch-order bug found during implementation).
**Impact on plan:** both fixes necessary for the plan's stated
behavior tests to pass. No scope creep. No architectural changes.

## Issues Encountered

None beyond the two above. Task 1 RED was correctly 17 failing
tests; Task 1 GREEN passed 17/17 on first invocation after the two
fixes. Task 2 RED was correctly 13 failing tests (17 Task-1 tests
stayed passing); Task 2 GREEN passed 30/30 on first invocation.

## User Setup Required

None — numpy is already installed on the dev workstation (2.2.4).
Plan 4 will pin `numpy>=1.23` in `pyproject.toml`.

## CONTEXT Decisions Satisfied by This Plan

- **D-01** (both layers): **fully satisfied.**
  `api.py` = primary, `reverb.SPU94` = secondary. Plan 5's
  `docs/DECISIONS.md` ADR will record this.
- **D-02** (factory presets importable): **partial extension.**
  Plan 1 fully satisfied `spu94.presets[...]`; Plan 2 adds
  `spu94.load_preset(state, "hall")` as the method path.
- **D-04** (cli.py `os.execv` shim): **fully satisfied in source.**
  Plan 4 wires `[project.scripts] spu94 = "spu94.cli:main"`;
  Plan 2's shim is ready.
- **D-09** (strict numpy contract): **fully satisfied.**
  `ndpointer(int16, 1D, C_CONTIGUOUS)` on process/flush; explicit
  length validator; upgraded float32 / non-contig messages.
- **D-10** (zero-copy): **fully satisfied + empirically verified.**
  Sentinel-overwrite test proves the C side writes directly into
  the numpy buffer.
- **D-11** (int16-only faithful-to-hardware): **fully satisfied.**
  No auto-conversion layer. Callers with float32 in [-1.0, 1.0]
  see the exact conversion recipe in the TypeError message.
- **D-06..D-08** (runtime reflection + drift): already satisfied
  in Plan 1; Plan 2 does not touch.
- **D-16..D-18** (fuzz migration): deferred to Plan 5.
- **D-21..D-25** (packaging): deferred to Plan 4.

## ADR Candidates (deferred to Plan 5)

Plan 5 (README + ADRs) will land formal ADR entries in
`docs/DECISIONS.md` for:

- **D-01 both-layers** — the primary/secondary surface split and
  why the secondary layer never duplicates state.
- **D-04 execv shim** — chose `os.execv` over `subprocess.run` to
  preserve Plan 3's CLI-04 exit-code contract through the wheel.
- **D-09 ndpointer contract** — why strict-int16 is faithful to
  hardware and how the upgrade wrapper handles the float32 case.
- **D-10 zero-copy witness** — the sentinel-overwrite methodology
  as a reusable test pattern.
- **D-11 int16-only posture** — no auto-conversion; message
  centralises the conversion recipe.

All five decisions are already fully coded in Plan 2; the ADRs
record the "why" for the DECISIONS.md first-class-deliverable
contract (PROJECT.md Active section).

## Known Stubs

None. Every file in the plan's `files_modified` list ships its
full Plan 2 implementation. Grep for stub patterns returns no
matches across python/spu94/ and tests/python/binding/.

## Threat Flags

None. Plan 2's scope is strictly the Python ergonomic layer; the
threat register's IDs T-06-07..T-06-13 all had dispositions
assigned in-plan and were satisfied as described:

- T-06-07 / T-06-08 (non-int16 / non-contig arrays): mitigated via
  ndpointer at argtypes level; raw pointer never crosses the
  boundary on malformed input.
- T-06-09 (length mismatch): mitigated via explicit validator
  before the C call; ValueError with array names + lengths in the
  message.
- T-06-10 (ndpointer error message leak): accepted; wrapper upgrades
  the message with actionable guidance, no secret leakage.
- T-06-11 (double destroy / use-after-free): mitigated via
  `self._state = None` + property guard; `RuntimeError(
  "SPU94 instance has been destroyed")` on any access after destroy.
- T-06-12 (self_test allocates huge buffer): accepted; 176 KB total.
- T-06-13 (cli shim argv trust): accepted; same trust boundary as
  running the user's shell; Plan 3 owns argv validation.

## Self-Check: PASSED

Files claimed to exist and spot-checked:

- `FOUND: python/spu94/api.py` (426 lines)
- `FOUND: python/spu94/reverb.py` (195 lines)
- `FOUND: python/spu94/cli.py` (56 lines)
- `FOUND: python/spu94/__init__.py` (165 lines, extended)
- `FOUND: python/spu94/_binding.py` (185 lines, extended)
- `FOUND: tests/python/binding/test_binding_numpy_contract.py` (383 lines)
- `FOUND: tests/python/binding/test_binding_surface.py` (84 lines, trimmed)
- `FOUND: tests/python/binding/CMakeLists.txt` (35 lines, extended)

Commits claimed and verified via `git log --oneline`:

- `FOUND: f2fcc3f` (Task 1 RED)
- `FOUND: 8dc5437` (Task 1 GREEN)
- `FOUND: 69e7faa` (Task 2 RED)
- `FOUND: 649b293` (Task 2 GREEN)

Verification commands (all green, exit 0):

- `ctest --test-dir build -L binding` — 5/5 pass
- `ctest --test-dir build -LE "fuzz"` — 59/59 pass (non-fuzz regression gate)
- `python3 -c "import spu94; spu94.self_test()"` — exits 0 silently
- `python3 -c "import spu94; rev = spu94.SPU94(); rev.load_preset('hall'); ...; rev.destroy()"` — prints `OK`
