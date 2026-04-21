# Phase 6: Python Binding + CLI - Context

**Gathered:** 2026-04-21
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 6 delivers **the packaging around the Phase 1–5 algorithm** — the ctypes Python binding that wraps the Phase 5 public C API, a native `spu94` WAV-render CLI, a `pip install`-able wheel via scikit-build-core + cibuildwheel, and a polished-tone README documenting the whole thing.

The algorithm and its public C surface are locked (Phases 1–5). Phase 6 is how *humans* — Python users, command-line users, someone browsing the GitHub page — actually touch the project. For the first time in the project's arc, Phase 6's primary deliverables are consumed by non-C-library-author audiences, including the user himself (a recording/broadcast engineer, not a coder). The design bar is ergonomics without compromising the bit-faithful posture the C core maintains.

**In scope:**
- `python/spu94/` ctypes binding exposing the full Phase 5 public C API: `spu94_init` / `spu94_reset` / `spu94_destroy` / `spu94_tick` / `spu94_process` / `spu94_flush` / `spu94_load_preset` / `spu94_state_size` / `spu94_get_buffer_address` / `spu94_get_latency_samples` / `spu94_set_reg_i16` / `spu94_set_reg_u16` / `spu94_get_reg_i16` / `spu94_get_reg_u16` / `_pending` variants / `spu94_snapshot_registers` / `spu94_reg_name` / `spu94_reg_hw_offset` / `spu94_presets[]` table import
- Both public surface layers: raw-panel module functions (state handle passed explicitly) + thin `SPU94` class wrapper (state handle owned by the object) — class is sugar over the raw layer, not a second implementation
- numpy interop: strict `int16` C-contiguous input/output arrays with clear-error validation; zero-copy when contract is satisfied (never a per-call hidden copy)
- Register IntEnum built at import time via runtime reflection through `spu94_reg_name(i)` + `spu94_reg_hw_offset(i)` — live library is authoritative, Python has no parallel truth
- Import-time assertions for drift detection: cached `spu94_state_size()`, IntEnum count matches `SPU94_REG__COUNT`, preset count matches `SPU94_PRESET__COUNT`
- Factory preset table importable from Python (`spu94.presets` exposes all 10 keyed by name and by enum id); underlying data is the C `spu94_presets[]` `.rodata`
- Native `spu94` C binary: new `src/cli/main.c` compiled via CMake; argument parsing; dr_wav vendored at `vendor/dr_wav/` and linked into the binary ONLY (never into `libspu94`)
- CLI accepts `--preset <name>` and `--config <path.json>`; supports both flat register-map JSON and `{ "base": "...", "overrides": {...} }` override JSON (auto-detect by `base` key); `--list-presets`; `--help`; non-zero exit + one-line actionable stderr on error
- Python `entry_point` that shells out to the compiled binary, so `pip install` users get the `spu94` command without needing a separate CMake build
- Fuzz-script migration: `tests/python/fuzz_buffer.py`, `fuzz_reverb.py`, `fuzz_fir.py`, `fuzz_process.py` all drop hand-typed register constants and import from the new Python binding (single source of truth)
- `pyproject.toml` with scikit-build-core build backend + cibuildwheel config (manylinux_2_28, Python 3.10+, one wheel per platform)
- `README.md` (DOCS-04): polished-tone, extensive-scope (hero / status / build / Python walkthrough / CLI walkthrough / "For the DSP-curious" technical section / roadmap / architecture overview / licensing posture / bibliography / contributing)
- ADR landings in `docs/DECISIONS.md` — planner discretion for exact count and split

**Explicitly NOT in scope:**
- Witness-diff harness against lv2-psx-reverb output — Phase 7 (TEST-03)
- Golden-file regression tests per preset — Phase 7 (TEST-04)
- Modulation-harness verification per register — Phase 7 (TEST-05). The Phase 6 binding is what Phase 7's modulation harness will use to drive the sweeps.
- `docs/LEVERS-CATALOG.md` — Phase 7 (DOCS-02)
- `docs/BIBLIOGRAPHY.md` comprehensive entries — Phase 7 (DOCS-03). Phase 6's README cites a handful inline.
- MCU cross-compile validation — Phase 8. Phase 6's binding + CLI are Linux-host-only.
- JUCE / VST3 / AU / LV2 plugin wrapper — Milestone 4
- Named musical levers (Room Size, Pre Delay, etc.), parameter smoothing, CV mappings, plugin UI — Milestone 4
- Windows / macOS / aarch64 / musllinux wheels — deferred (Phase 6 ships Linux x86_64 only)

</domain>

<decisions>
## Implementation Decisions

### Area A — Python API shape (PYBIND-01, PYBIND-04)

- **D-01: Expose both layers.** Primary surface is raw-panel module functions (`spu94.init`, `spu94.load_preset(state, id)`, `spu94.process(state, L_in, R_in, L_out, R_out)`, `spu94.destroy`, etc.) — state handle passed explicitly, matches the C side 1:1. A thin `spu94.SPU94` class wraps the handle for ergonomic use (`rev = spu94.SPU94(); rev.load_preset("hall"); rev.process(L_in, R_in, L_out, R_out)`). The class forwards each method to the raw equivalent — no hidden state, no second implementation, no magic. Both are public and documented. User rationale: "honest machine on the inside, polite panel on the outside."
- **D-02: Factory presets importable as Python data.** `spu94.presets` exposes the 10 factory presets as a Python object keyed by both preset name (`spu94.presets["hall"]`) and preset enum id (`spu94.presets[spu94.Preset.HALL]`). Underlying storage is the C `spu94_presets[]` `.rodata` table, read via ctypes at import time. Python-side representation includes the preset name string plus the 35 register values — useful for introspection, preset diffing, and future custom-preset import/export.

### Area B — CLI implementation (CLI-01, CLI-02, CLI-03, CLI-04)

- **D-03: Native C binary is the CLI.** A new `src/cli/main.c` (or equivalent path — planner's discretion) compiles to a standalone `spu94` executable via CMake. dr_wav is vendored at `vendor/dr_wav/` and linked into the CLI binary **ONLY** — never into `libspu94.so` (CLI-03 compliance). The binary has no Python / numpy dependency; someone who just wants to render WAV files never needs to touch Python.
- **D-04: Python entry_point shim.** `pip install` registers an `spu94` console_script (via `[project.scripts]` in `pyproject.toml`) that shells out to the compiled binary. Small implementation (~5-10 lines in `python/spu94/cli.py`). Ensures pip-only users get the `spu94` command alongside their Python binding without a separate CMake build step on their machine. The compiled binary ships inside the wheel alongside `libspu94.so`.
- **D-05: Error handling contract.** Any argument error (unknown preset, unparseable JSON, missing input file, malformed WAV) or I/O error exits non-zero with a one-line actionable stderr message (CLI-04). Standard-style prefix (e.g., `spu94: error: unknown preset 'hll' — valid: off, room, studio_a, studio_b, studio_c, hall, half_echo, space_echo, echo, delay`). No stack traces, no Python-style tracebacks. No success-on-stderr; stderr is for errors only.

### Area C — Register name sync + struct drift detection (PYBIND-03, PYBIND-05)

- **D-06: Runtime reflection builds the IntEnum.** At `import spu94`, Python walks the C library: for `i` in `0..SPU94_REG__COUNT-1`, calls `spu94_reg_name(i)` + `spu94_reg_hw_offset(i)`, assembles a Python `IntEnum` (e.g., `spu94.Register`) dynamically. Single source of truth — the live library is authoritative; Python has no parallel typed list that could drift. User rationale: "we already built the lookup functions in Phase 2, partly so this would be free to do."
- **D-07: Import-time assertions catch drift loudly.** On import, the binding performs:
  - `assert lib.spu94_state_size() <= SPU94_STATE_SIZE_MAX` — state has not grown past the public bound
  - `assert len(Register) == SPU94_REG__COUNT_py == lib_exposed_count` where the Python-side constant is built from the reflected enum length and cross-checked against what the library reports (catches e.g., library rebuilt with more registers but old wheel loaded)
  - `assert len(Preset) == SPU94_PRESET__COUNT == 10`
  - Cached values (`_state_size`, `_latency_samples`) stored as module-level constants for later reference
  Any mismatch raises `RuntimeError("spu94 library mismatch: ...")` with enough detail to diagnose (e.g., "expected 35 registers, library reports 36 — recompile python bindings or pin library version").
- **D-08: Struct-internal offsets that have no public C accessor stay hand-typed in the fuzz scripts that need them.** Phase 5's `fuzz_process.py` has offsets like `PENDING_MASK_OFFSET = 160`, `FIR_IDX_L_IN_OFFSET = 360`, etc. — reaching into the internal state struct for invariant checks. These do NOT belong in the public binding. They stay in the fuzz scripts with a clearly labeled warning block. Planner may introduce a tests-only `spu94_debug_offset(field_id)` accessor if judged valuable, but it's not required; the import-time `spu94_state_size()` check already catches structural drift.

### Area D — numpy input contract (PYBIND-02)

- **D-09: Strict `int16` C-contiguous arrays required on `spu94.process` / `spu94.flush`.** The binding validates, for each input/output array argument:
  - dtype is exactly `np.int16`
  - array is C-contiguous (`arr.flags['C_CONTIGUOUS']`)
  - all four arrays (L_in, R_in, L_out, R_out — except where NULL-allowed per the C contract) have the same length; `num_samples` is derived from it
  Any violation raises `TypeError` (dtype) or `ValueError` (layout / size mismatch) with an actionable message, e.g. `"spu94.process requires int16 arrays; got float32 for L_in — use arr.astype(np.int16) before calling"`.
- **D-10: Zero-copy guaranteed when contract holds.** When the strict contract is satisfied, the binding passes a raw pointer + length to C without copying. No intermediate numpy allocations, no per-call data shuffling. PYBIND-02 "zero-copy where possible" is satisfied via the bright-line contract — "possible" is defined as "you passed the right shape," not "we'll maybe-convert."
- **D-11: Rationale from the PS1 hardware.** The PS1 SPU has no "convert-on-input" layer because it has no non-int16 audio format to convert from. Everything in the hardware is 16-bit signed integer end-to-end. A forgiving Python binding that silently converted float32 → int16 would add a conversion layer the original hardware never had — exactly the kind of "helpful magic" the rest of the project is designed to avoid. Strict is *more* faithful, not less. (Recorded because the user raised this specifically during discussion.)

### Area E — `--config preset.json` JSON schema (CLI-02)

- **D-12: Dual shape with auto-detect by `"base"` key.** The CLI's JSON parser checks for a top-level `"base"` key:
  - **If `"base"` present** → treat as override patch: `{ "base": "<preset_name>", "overrides": { "<reg_name>": <value>, ... } }`. Load the named preset, then apply each override via the engine-layer setter. `overrides` may be empty.
  - **If `"base"` absent** → treat as flat register map: `{ "<reg_name>": <value>, ... }`. Every one of the 35 registers must be present; missing keys are a non-zero-exit error with a list of which keys are missing.
- **D-13: Value parsing accepts integers and hex strings.** JSON numeric integers for values that fit (`-32768` to `65535`); hex strings (`"0x3F00"`, `"-0x8000"`) for values the user prefers to write in hex. Signed (`v*`) registers accept negative integers / negative-hex; unsigned (`d*` / `m*`) registers accept only non-negative. Range-check against the target register's typed domain; overflow is a non-zero-exit error naming the register and the out-of-range value.
- **D-14: Unknown register names are errors, not ignored.** A JSON key that doesn't match any of the 35 register names (case-insensitive matched against `spu94_reg_name` results) fails with an error listing valid register names. No silent-skip.
- **D-15: README showcases the override shape as the "everyday" entry point.** Documentation prioritizes `{ "base": "hall", "overrides": { "vIIR": -8000 } }` as the example users copy. Flat-map is documented separately as "exact register specification for golden-file reproduction and debugging."

### Area F — Fuzz script migration (scope closer)

- **D-16: All four fuzz scripts migrate to the new binding.** `tests/python/fuzz_buffer.py`, `fuzz_reverb.py`, `fuzz_fir.py`, `fuzz_process.py` drop their hand-typed register-enum values, hand-typed state-size constants, and hand-typed type-classification tables. Replace with imports from the new binding (e.g., `from spu94 import Register, Preset, SPU94_REG__COUNT, SPU94_STATE_SIZE_MAX, is_signed_reg(...)`). Single source of truth for the register surface.
- **D-17: Struct-internal offset tables stay in fuzz scripts.** Per D-08, struct offsets that have no public C accessor (the `PENDING_MASK_OFFSET`, `FIR_IDX_*_OFFSET` values in `fuzz_process.py`) remain hand-typed in those scripts with a clearly labeled warning block. They're test-private knowledge, not library-public knowledge. Planner may choose to introduce a debug-accessor, but it's optional.
- **D-18: CMake test wiring unchanged.** Existing ctest targets (`fuzz_buffer`, `fuzz_reverb`, `fuzz_fir`, `fuzz_process`) continue to invoke the migrated scripts with `SPU94_LIB` env var set via the generator-expression pattern (`$<TARGET_FILE:spu94_shared>`). No change to test topology or CI wiring — the migration is internal to the Python source.

### Area G — README tone + scope (DOCS-04)

- **D-19: Polished tone throughout.** Confident, descriptive, product-doc voice. No apologetic framing ("WIP", "pre-1.0", "early development" as mood-setters). Status is communicated via a dedicated status block that names what ships today and what's coming, in roadmap-style language. The README reads like a real piece of audio gear's documentation, not a disclaimer.
- **D-20: Extensive scope — 11 sections.** In order:
  1. **Hero / pitch paragraph** — "SPU-94 is a bit-faithful software reimplementation of the PlayStation 1 SPU reverb, built from the published hardware spec."
  2. **Status block** — Milestone 1 state. Algorithm complete (reverb network + FIR + hard clip + 10 factory presets, bit-tested). Python bindings + CLI land this phase. Upcoming: witness-diff verification, MCU cross-compile, JUCE plugin, Eurorack hardware.
  3. **Quick install** — `pip install spu94` for the wheel; `cmake --build build` for from-source.
  4. **Python walkthrough** — both layers with small worked examples. Raw functions first (pass state handle), then class sugar (`rev = spu94.SPU94(); rev.load_preset("hall"); rev.process(...)`).
  5. **CLI walkthrough** — `--preset`, `--config` with both flat and override JSON shapes, `--list-presets`, `--help`.
  6. **For the DSP-curious** — Q15 truncation is the character, 39-tap half-band FIR at both I/O boundaries closes the brightness gap, vIIR=0x8000 hardware anomaly, "from spec not ported" philosophy, links to `docs/DECISIONS.md` and `docs/BIBLIOGRAPHY.md`. Written in the same polished voice, just meatier.
  7. **Roadmap summary** — M1 → M5 at a glance, matching `.planning/PROJECT.md`.
  8. **Architecture overview** — signal flow (short prose or tiny ASCII). Decimator → reverb tick → interpolator; state layout; register surface.
  9. **Licensing posture** — deliberate deferral of MIT vs Apache-2.0 until end of M1; pointer to `LICENSE` placeholder; nocash paraphrase-not-transcribe discipline; GPL witnesses consulted not copied.
  10. **Acknowledgments / bibliography** — nocash psx-spx, Sony SDK documentation, hitmen c02 SPU docs, dr_wav. Witness implementations (Mednafen, lv2-psx-reverb, DuckStation, MiSTer) named as behavioral witnesses, not source material.
  11. **Contributing** — build instructions, test invocation (`ctest`, `pytest`), what a good PR looks like (with / without ADR, tests required, DECISIONS.md updates for gray-area changes).

### Area H — Packaging (PYBIND-06)

- **D-21: manylinux_2_28 Linux wheel.** Baseline glibc 2.28+ (Ubuntu 20.04+, Debian 11+, RHEL 8+, Fedora 30+). manylinux2014 (CentOS 7 base) is EOL; SPU-94 doesn't target it. Single platform tag: `manylinux_2_28_x86_64`.
- **D-22: Python 3.10+ minimum.** Matches the existing fuzz-harness Python requirement (`find_package(Python3 3.10 REQUIRED)` in `tests/python/CMakeLists.txt`). Older versions (3.8, 3.9) add compat surface without real user base. `requires-python = ">=3.10"` in `pyproject.toml`.
- **D-23: One wheel per platform, not per Python version.** Because the binding is pure ctypes (no Python C API, no `Py_*` symbols touched), a single wheel works across every Python 3.10+ minor version on a given platform. Wheel tag: `py3-none-manylinux_2_28_x86_64` (or the cibuildwheel equivalent — planner confirms the precise tag during build).
- **D-24: Wheel layout.** `libspu94.so` ships inside the `spu94/` package directory alongside `__init__.py` (e.g., `python/spu94/libspu94.so` after the build step). `__init__.py` locates it at import time via `pathlib.Path(__file__).parent / "libspu94.so"`, with `SPU94_LIB` env-var override for dev convenience (continues the pattern from the existing fuzz scripts). The compiled `spu94` CLI binary also ships inside the package or a co-installed `scripts/` location so the `[project.scripts]` entry_point can find it.
- **D-25: `pyproject.toml` holds the build config.** Specifies `[build-system]` with `scikit-build-core>=0.8`, `[project]` metadata, `[project.scripts]` entry_point, `[tool.scikit-build]` with CMake build-type and install layout, `[tool.cibuildwheel]` with manylinux image selection, Python version matrix, and a smoke-test command (`python -c "import spu94; spu94.smoke_test()"` or similar — planner decides).

### Architectural Principles (carried forward from Phases 1–5)

- **D-22 Extensibility Seams (Phase 2):** No new seams added in Phase 6. The binding and CLI are pure consumers of the Phase 5 public surface; they do not extend it.
- **D-23 Observability (Phase 2):** The Python binding preserves read-only observability. Register reads (active + pending), buffer address, state size, latency samples are all exposed through the binding as read-only. No new Python-side mutators that bypass the C write policy.
- **ADR-0001 (Q15 truncation):** No new multiplies in Phase 6. The binding is glue code, not math.
- **ADR-0005 (Pitfall 4 single-call-site discipline):** The binding's `spu94.process` and `spu94.flush` are the sole Python callers of their respective C symbols. No secondary code paths.
- **PROJECT.md licensing posture:** nocash docs are paraphrased when quoted in the README; never transcribed. dr_wav is vendored with its own license note preserved.

### Claude's Discretion (within the locked decisions above)

- Exact naming of raw-panel module functions (`spu94.process` vs `spu94.process_block` vs `spu94.run_block`). Planner picks consistent naming and applies throughout.
- Exact class name (`SPU94`, `Reverb`, `SPU94Reverb`). Suggestion: `SPU94` as the symmetric counterpart to the package name.
- Exact wording of numpy contract violation error messages (D-09) — examples above are seeds, planner refines.
- Internal organization of `python/spu94/`: single `__init__.py` with everything vs. split into `_binding.py` (raw ctypes protos), `api.py` (raw-panel functions), `reverb.py` (the `SPU94` class), `presets.py` (preset import), `cli.py` (entry_point). Suggestion: split — easier to audit, matches one-concern-per-TU convention.
- Internal layout of the C CLI source (`src/cli/main.c` vs `src/cli/spu94_cli.c` vs `tools/spu94/main.c`). Vendored dr_wav exact path (`vendor/dr_wav/` suggested).
- Exact IntEnum class names (`spu94.Register` vs `spu94.Reg`; `spu94.Preset` vs `spu94.PresetID`).
- Whether the `SPU94` class uses a `__enter__` / `__exit__` context manager pattern (`with spu94.SPU94() as rev: ...`) in addition to plain construction. Recommendation: yes — cheap and Pythonic.
- ASCII signal-flow diagram vs prose for the README's architecture overview.
- `pyproject.toml` exact metadata field values (classifiers, keywords, project URLs, author email, maintainer).
- cibuildwheel matrix specifics — whether to test the built wheel via `pytest` or via a minimal smoke test.
- Whether to ship a `spu94[dev]` extra for development deps (pytest, numpy-specific version pin) or keep dev deps in a separate `requirements-dev.txt`.
- Whether the CLI entry_point shim uses `os.execv` (replaces the Python process) or `subprocess.run` (spawns a child). `os.execv` is slightly cleaner (exit code passes through naturally).
- Exact number and split of ADRs appended to `docs/DECISIONS.md` — planner decides whether each of D-01..D-25 gets its own ADR or natural groupings combine (Python API shape, CLI architecture, numpy contract, config JSON, fuzz migration, README, packaging).

### Folded Todos

None — no pending todos matched Phase 6 scope at discussion time (`gsd-tools todo match-phase 6` returned empty `matches`).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before proceeding.**

### Project Spec (internal)

- `.planning/PROJECT.md` — Constraints that govern Phase 6: ctypes (not pybind11 / cffi) per "Tech stack (tooling)" line; numpy + scipy + matplotlib + pytest for tooling; dr_wav vendored for CLI only not library; Linux primary; scikit-build-core for wheel; "SPU-94 is a living instrument, not a preset engine" (Phase 6 exposes every parameter via the Python binding, respecting this); paraphrase-not-transcribe discipline applies to the README and any nocash citations.
- `.planning/REQUIREMENTS.md` — Phase 6 owns PYBIND-01, PYBIND-02, PYBIND-03, PYBIND-04, PYBIND-05, PYBIND-06, CLI-01, CLI-02, CLI-03, CLI-04, DOCS-04 (11 requirements total).
- `.planning/ROADMAP.md` § Phase 6 — four success criteria must all be TRUE. SC-1 (full public C API reachable from Python via ctypes with matching IntEnum), SC-2 (`spu94 --preset hall in.wav out.wav` and `spu94 --config preset.json in.wav out.wav` both succeed, dr_wav-backed), SC-3 (`pip install -e .` installs a wheel built via scikit-build-core; Linux wheel producible via cibuildwheel), SC-4 (README documents build + Python/CLI examples + licensing + status banner; unfamiliar reader can render their first WAV).

### Prior Phase CONTEXT.md files (must read for consistency)

- `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-CONTEXT.md` — D-03 `python/spu94/` location reserved; scikit-build-core compatibility posture; `docs/DECISIONS.md` ADR format Phase 6 continues.
- `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md` — D-01 two-layer register I/O (Phase 6's binding wraps the **engine layer**, not the facade); D-17 `spu94_reg_name` public accessor (used for runtime reflection); D-16 `spu94_reg_hw_offset` (same); D-20 `spu94_snapshot_registers` (exposed via the Python binding for preset-diff and debug dumps); D-22 extensibility seams and D-23 observability principle — Phase 6 preserves both, adds no new seams or mutators.
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md` — algorithm details not directly exposed by Phase 6, but understood as the black-box the public API wraps.
- `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-CONTEXT.md` — D-09 `spu94_get_latency_samples()` contract exposed by the binding; the 39-tap FIR story becomes a featured paragraph in the README's "For the DSP-curious" section.
- `.planning/phases/05-public-api-presets-integration/05-CONTEXT.md` — D-01..D-10 establish the public surface the binding wraps: planar stereo `spu94_process` + `spu94_flush` + `spu94_load_preset`, `spu94_presets[]` table, `spu94_preset_id_t` enum, `spu94_preset_t` struct, any block size N ≥ 1, in-place allowed, int16 planar contract. Phase 6's strict numpy contract (D-09) directly mirrors Phase 5's D-01.

### Public C API surface (wrapped by the binding)

- `include/spu94/spu94.h` — umbrella header. Full type and function list exposed: `spu94_state` (opaque), `spu94_result_t`, `spu94_init` / `spu94_reset` / `spu94_destroy` / `spu94_tick` / `spu94_process` / `spu94_flush` / `spu94_load_preset`, `spu94_state_size`, `spu94_get_buffer_address`, `spu94_get_latency_samples`, `SPU94_LATENCY_SAMPLES`, `SPU94_STATE_SIZE_MAX`, `SPU94_STATE_ALIGN_MAX`, `spu94_preset_id_t` (SPU94_PRESET_OFF..SPU94_PRESET_DELAY + SPU94_PRESET__COUNT=10), `spu94_preset_t`, `extern const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT]`.
- `include/spu94/spu94_registers.h` — `spu94_reg_t` enum (35 entries, `SPU94_REG__COUNT`), engine-layer `spu94_set_reg_i16` / `spu94_set_reg_u16` / `spu94_get_reg_i16` / `spu94_get_reg_u16` + `_pending` variants, `spu94_reg_name`, `spu94_reg_hw_offset`, `spu94_snapshot_registers`. **The binding uses the engine layer, not the facade.**
- `include/spu94/spu94_register_facade.h` — 105 hand-written inline per-register wrappers. Phase 6's binding does NOT wrap these (engine layer is designed for bulk iteration; facade is for readable C call sites).
- `include/spu94/spu94_q15.h` — Q15 helpers (`q15_mul_truncate`, `q15_mul_truncate_with_err`, `sat_s16`, `q15_add_sat`). The binding does **NOT** expose these — they're internal to the library's own math; exposing them from Python would invite users to re-implement pieces of the reverb in Python, which is out of scope for the ctypes binding.

### ADR Log (appended in Phase 6)

- `docs/DECISIONS.md` — existing: ADR-0001..ADR-0011 (Phases 1-3), ADR-0012..ADR-0020 (Phase 4 FIR), ADR-Phase-5-A..F (Phase 5 public API + presets + RT-safety). Phase 6 appends ADRs for: Python API shape (D-01..D-02), CLI implementation (D-03..D-05), runtime reflection + drift detection (D-06..D-08), numpy contract (D-09..D-11), `--config` JSON format (D-12..D-15), fuzz migration (D-16..D-18), README (D-19..D-20), packaging (D-21..D-25). Planner decides whether each decision gets its own ADR or natural groupings combine.

### External References (paraphrased only — do NOT transcribe per PROJECT.md licensing posture)

- **dr_wav** (github.com/mackron/dr_libs, single-header `dr_wav.h`) — public-domain / MIT-0 dual-licensed single-header C WAV library. Vendored into `vendor/dr_wav/` for the CLI. Used **only** in the CLI binary; never linked into `libspu94.so`. dr_wav's own LICENSE is preserved in the vendored directory.
- **scikit-build-core documentation** (scikit-build.readthedocs.io/projects/scikit-build-core/) — wheel layout conventions, `pyproject.toml` structure, `[tool.scikit-build]` options, CMake integration patterns, include/exclude policy.
- **cibuildwheel documentation** (cibuildwheel.readthedocs.io) — manylinux image selection, Linux wheel tagging, `[tool.cibuildwheel]` config, `test-command` for in-build smoke tests.
- **PEP 600 / manylinux_2_28** — glibc baseline spec for modern Linux wheels.
- **numpy.ctypeslib** — `ndpointer` usage for strict dtype + flag validation; `c_int16 *` pointer type for pass-through.
- **ctypes stdlib docs** — `CDLL`, `POINTER`, `c_int16`, `c_uint32`, function `argtypes` / `restype` declaration, `Structure` for preset data (if needed; planner may use `ndpointer(dtype='i2', shape=(35,))` instead).
- **setuptools entry_points / `[project.scripts]`** — for the `spu94 = "spu94.cli:main"` Python shim that calls the compiled C binary.

### Not to be read as primary source

- Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, MiSTer source — per PROJECT.md licensing posture. Their Python-binding or CLI patterns are **not** consulted. Phase 6 binding is original work from the C header surface + scikit-build-core docs.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (from Phases 1–5)

- `libspu94.so` (built by the existing CMake target `spu94_shared`) — full Phase 5 public API compiled and linked. Binding's sole load target. `nm -D libspu94.so` reports 19 T-symbols as of end of Phase 5 (plus the Phase 5 additions: `spu94_process`, `spu94_flush`, `spu94_load_preset`, `spu94_presets[]` data symbol).
- `tests/python/fuzz_buffer.py`, `fuzz_reverb.py`, `fuzz_fir.py`, `fuzz_process.py` — proven ctypes pattern (CDLL loader via `SPU94_LIB` env var; function `argtypes` / `restype` declaration; `numpy.ctypeslib.ndpointer` for array arguments; hand-synced register constants). Phase 6 generalizes this pattern into a reusable binding module, then migrates these scripts to import from it.
- `python/spu94/.gitkeep` — package directory reserved by Phase 1 D-03; otherwise empty. Phase 6 populates.
- `CMakeLists.txt` + `cmake/spu94_warnings.cmake` — existing build system (cmake ≥ 3.20, C11, determinism flags, `spu94_obj` OBJECT library feeding `spu94_shared` + `spu94_static`). Phase 6 adds a new CLI executable target under `src/cli/` (name at planner discretion) that links `spu94_shared` + vendored dr_wav. Integrates cleanly with scikit-build-core via `[tool.scikit-build]` pointing at the root `CMakeLists.txt`.
- `tests/CMakeLists.txt`, `tests/python/CMakeLists.txt` — existing ctest topology with `find_package(Python3 3.10 REQUIRED)` and `$<TARGET_FILE:spu94_shared>` env-var generator expression. Phase 6's migrated fuzz scripts keep the same targets; only the Python internals change.
- `docs/DECISIONS.md`, `docs/BIBLIOGRAPHY.md` — existing documentation file structure. Phase 6 appends ADRs to DECISIONS; the README's "For the DSP-curious" section links into both.
- `LICENSE` — placeholder per Phase 1 D-05; final pick deferred to end of M1. Phase 6 references the placeholder, does not replace it.

### Established Patterns

- **ctypes `CDLL` + explicit `argtypes` / `restype`** (from Phase 2 Plan 05 `fuzz_buffer.py`) — Phase 6 continues this pattern but centralizes the prototype declarations into `python/spu94/_binding.py` (or equivalent) instead of repeating them per fuzz script.
- **`SPU94_LIB` env-var-first, installed-path-fallback** — Phase 6's `__init__.py` first checks `os.environ.get('SPU94_LIB')` (dev convenience — developers can point at a freshly built `build/src/spu94/libspu94.so`), then falls back to `pathlib.Path(__file__).parent / 'libspu94.so'` (installed-wheel case).
- **Hand-computed inline reference tables in tests** (Phase 1 D-10) — Phase 6's binding unit tests (`tests/unit/binding/` or `tests/python/binding/`) use the same inline `{ input, expected_result }` pattern for the strict numpy validator's error cases and for the `--config` JSON parser.
- **ADR-style entries prepended to `docs/DECISIONS.md`** (Phase 1 D-12) — Phase 6 appends a block of ADRs for D-01..D-25.
- **One-concern-per-TU grain** (Phase 2) — Phase 6's Python package: `_binding.py` (raw ctypes wrappers), `api.py` (raw-panel public functions), `reverb.py` (the `SPU94` class), `presets.py` (preset table import + accessor), `cli.py` (entry_point shim), `__init__.py` (public exports + import-time assertions + IntEnum construction). CLI: `src/cli/main.c` + `vendor/dr_wav/`.
- **Pitfall 4 single-call-site discipline** (ADR-0005) — the binding's `spu94.process` / `spu94.flush` / `spu94.load_preset` are each the sole Python callers of their corresponding C entry point. No secondary code paths inside the Python package.
- **Paraphrase-not-transcribe** (PROJECT.md) — the README's "For the DSP-curious" section paraphrases nocash's explanatory prose; every technical fact is cited with a bibliography-style reference, not a quote.

### Integration Points

- **Phase 5 C API is the sole dependency boundary.** The binding calls only public symbols declared in `include/spu94/spu94.h` + `include/spu94/spu94_registers.h`. Internal headers (`src/spu94/*_internal.h`) are never consumed by Python.
- **Phase 7 (verification)** uses the Phase 6 binding heavily. Golden-file generation (TEST-04) iterates presets via `spu94.presets` and snapshots `spu94.process(...)` outputs. Witness-diff harness (TEST-03) uses the binding to run the same inputs. Modulation test per register (TEST-05) uses the runtime-reflected IntEnum + `set_reg_i16` / `set_reg_u16` to sweep each of the 35 registers under live audio (sine, frequency sweep, random walk). Phase 7 does not need to re-implement any binding logic — it consumes Phase 6 as-is.
- **Phase 8 (MCU cross-compile)** does NOT consume the Python binding or CLI. The MCU smoke-test harness is pure C (`mcu-smoke/main.c`) calling `libspu94` symbols directly. Phase 6's binding and CLI are Linux-host-only; MCU doesn't run Python.
- **Future Milestone 4 (JUCE plugin)** does NOT consume the Python binding. The plugin is C++ wrapping `libspu94` directly. The Python binding remains developer-side tooling (tests, plots, scripting, preset-diff). The CLI continues as a standalone render tool.
- **Future Milestone 5 (Hardware validation)** uses the Python binding to drive witness-diff against captured PS1 hardware output. The binding's runtime reflection and strict numpy contract make it suitable for high-confidence test harness use.

</code_context>

<specifics>
## Specific Ideas

### Rough shape of the Python package

```
python/spu94/
├── __init__.py         # public exports, import-time assertions, IntEnum construction
├── _binding.py         # raw ctypes CDLL + function prototype declarations
├── api.py              # raw-panel public functions: init, process, flush, load_preset, destroy, etc.
├── reverb.py           # the SPU94 class (thin wrapper over api.py)
├── presets.py          # preset table import + Preset IntEnum + spu94.presets accessor
├── cli.py              # entry_point shim that calls the compiled `spu94` binary
├── libspu94.so         # compiled library (installed from the build step)
└── spu94               # compiled CLI binary (installed from the build step; may live in scripts/)
```

### Rough shape of the CLI source tree

```
src/cli/
├── CMakeLists.txt      # spu94 executable target; links spu94_shared + dr_wav
└── main.c              # argument parser + dr_wav read/write + spu94_process loop + WAV tail flush
vendor/
└── dr_wav/
    ├── dr_wav.h        # public-domain single-header library (vendored verbatim)
    └── LICENSE         # dr_wav's own license note, preserved
```

### Rough `pyproject.toml` content

```toml
[build-system]
requires = ["scikit-build-core>=0.8"]
build-backend = "scikit_build_core.build"

[project]
name = "spu94"
version = "0.1.0"
description = "Bit-faithful PlayStation 1 SPU reverb — Python binding + CLI"
requires-python = ">=3.10"
dependencies = ["numpy>=1.23"]
authors = [{ name = "Anthony Accurso" }]
readme = "README.md"

[project.scripts]
spu94 = "spu94.cli:main"

[tool.scikit-build]
cmake.version = ">=3.20"
cmake.build-type = "Release"
wheel.packages = ["python/spu94"]
# planner adds install rules to copy libspu94.so + spu94 binary into the package dir

[tool.cibuildwheel]
build = ["cp310-*", "cp311-*", "cp312-*"]
skip = ["*-musllinux_*", "*-manylinux_i686"]
manylinux-x86_64-image = "manylinux_2_28"
test-command = "python -c \"import spu94; spu94.self_test()\""
```

### Strict numpy contract validator — rough error message seeds

- `"spu94.process requires int16 arrays; got {arr.dtype} for {name}. Use arr.astype(np.int16) before calling."`
- `"spu94.process requires C-contiguous arrays; got a non-contiguous slice for {name}. Use np.ascontiguousarray(arr) before calling."`
- `"spu94.process requires all input/output arrays to be the same length; got L_in={L_in.shape}, R_in={R_in.shape}, L_out={L_out.shape}, R_out={R_out.shape}."`

### CLI usage examples for the README

```
spu94 --preset hall input.wav output.wav
spu94 --preset hall --tail-seconds 2 input.wav output.wav
spu94 --config my_override.json input.wav output.wav
spu94 --list-presets
spu94 --help
```

### `--config` JSON examples for the README

Override shape (everyday tweaking):
```json
{
  "base": "hall",
  "overrides": {
    "vIIR":    -8000,
    "dCOMB1":  "0x1000"
  }
}
```

Flat shape (exact reproduction / golden-file use):
```json
{
  "mBASE":   "0x3F00",
  "vIIR":    -32768,
  "vCOMB1":   20000,
  "vCOMB2":   20000,
  "vCOMB3":   20000,
  "vCOMB4":   20000,
  "... 29 more entries": "..."
}
```

### DSP-curious README paragraph seeds (flavor for the executor)

- **Q15 truncation as character:** "Most modern fixed-point DSP rounds; the PS1 truncates. That bias toward zero on every multiply is part of what gives the reverb its sound — a subtle asymmetry the human ear reads as *vintage*."
- **39-tap half-band FIR at both I/O boundaries:** "The SPU's reverb network runs internally at 22.05 kHz. Every sample arriving at the 44.1 kHz input boundary passes through a 39-tap half-band decimating FIR; every sample leaving passes through the matching interpolator. Omitting this filter — as some open-source emulators do — makes the reverb audibly brighter than hardware. SPU-94 reproduces Sony's coefficients verbatim in integer arithmetic."
- **The vIIR anomaly:** "Writing the value `0x8000` (the most negative int16) to the `vIIR` register causes the reverb to negate its output. It's a hardware quirk, not a bug; SPU-94 reproduces it faithfully."
- **From spec, not ported:** "SPU-94's algorithm was written from nocash's published register documentation and the Sony SDK, not ported from any existing emulator. Mednafen, lv2-psx-reverb, DuckStation, and MiSTer are consulted as behavioral witnesses when the spec is ambiguous — their *output audio* is compared against SPU-94's output, but their source code is not read as a primary development activity."

### Architecture overview ASCII (one candidate for the README)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Caller (CLI / Python user / M4 plugin — all paths look the same)       │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │  int16 stereo @ 44.1 kHz
                            ▼
                   ┌────────────────────┐
                   │  spu94_process()   │  block-based, in-place legal
                   └────────┬───────────┘
                            │
            per sample:     │
                            ▼
                   ┌────────────────────┐
                   │ 39-tap FIR decim.  │  44.1 → 22.05 kHz
                   └────────┬───────────┘
                            │
                            ▼
                   ┌────────────────────┐
                   │    spu94_tick()    │  per 22.05-kHz tick
                   │                    │
                   │  apply pending →   │  tick-latched register commits
                   │  reverb body:      │  (SAME IIR → DIFF IIR →
                   │                    │   4-tap comb → APF1 → APF2)
                   │  buffer advance    │  MAX(mBASE, (ba+2) & 0x7FFFE)
                   └────────┬───────────┘
                            │
                            ▼
                   ┌────────────────────┐
                   │  39-tap FIR interp │  22.05 → 44.1 kHz
                   └────────┬───────────┘
                            │  int16 stereo @ 44.1 kHz
                            ▼
                   ┌────────────────────┐
                   │  hard clip stage   │  mix-bus saturation to ±0x7FFF
                   └────────┬───────────┘
                            │
                            ▼
                        (caller receives)
```

### Smoke test for cibuildwheel's `test-command`

`python -c "import spu94; spu94.self_test()"` where `self_test()` is a public Python function that:
1. Builds a 1-second silent buffer
2. Creates a state, loads Hall preset
3. Calls `spu94.process(state, silent_L, silent_R, out_L, out_R)`
4. Asserts output is non-garbage (specific bound checks)
5. Destroys state
6. Returns without error

</specifics>

<deferred>
## Deferred Ideas

### Deferred to Phase 7 (Verification)

- **Witness-diff harness** against lv2-psx-reverb output (TEST-03). The Phase 6 binding is the machinery Phase 7 uses to drive SPU-94 in the comparison.
- **Golden-file regression tests** per preset × standard input set (TEST-04). Phase 7 snapshots `spu94.process(...)` outputs for each preset.
- **Modulation test per register** (TEST-05). Phase 7 uses the Phase 6 `Register` IntEnum + `set_reg_*` functions to sweep each of the 35 registers under live audio.
- **`docs/LEVERS-CATALOG.md`** (DOCS-02). Phase 7's job; Phase 6 may seed entries opportunistically in the README's "DSP-curious" section.
- **`docs/BIBLIOGRAPHY.md`** comprehensive entries (DOCS-03). Phase 6's README cites a handful inline; Phase 7 produces the full bibliography.
- **pytest-benchmark harness** (BUILD-06). Phase 7's job; the Phase 6 binding provides the machinery but not the timing-regression CI wire-up.

### Deferred to Phase 8 (MCU Cross-Compile)

- Cortex-M7 cross-compile toolchain file + `mcu-smoke/main.c` + `.text` size check + `readelf -d` no-malloc check (BUILD-03). Phase 6 does not build for MCU; CLI is Linux-host.

### Deferred to Milestone 4 (Plugin Era)

- **JUCE / VST3 / AU / LV2 wrapper** around `libspu94`. Phase 6's binding is developer tooling; the plugin is the consumer-facing realtime product.
- **Named musical levers** (Room Size, Pre Delay, Decay, Diffusion, Damping, etc.) — M4 per PROJECT.md.
- **Parameter smoothing** — M4 work.
- **Mid-stream preset morph / crossfade** — M4 UX feature on top of Phase 5's atomic `spu94_load_preset` primitive. The Python binding exposes the primitive; M4 builds the morph on top.
- **Plugin UI** (factory preset menu, register-level advanced mode, CV-input mapping) — M4 work.
- **Custom preset import / export** — M4 UX feature. Phase 6's `spu94.presets` exposes the 10 factory presets; user-authored presets via the `--config` flat shape work today at the CLI, but importing/exporting via the plugin UI is M4.

### Deferred to Milestone 5 (Hardware Validation)

- PSX homebrew harness + digital capture pipeline.
- Hardware-vs-SPU-94 witness-diff (uses Phase 6 binding + Phase 7 witness harness).
- Eurorack module (PCB, panel, period-appropriate DAC, CV inputs) — explicitly future direction per PROJECT.md.

### Deferred Platform Support

- **Windows wheels** — scikit-build-core + cibuildwheel both support Windows; Phase 6 targets Linux x86_64 only to keep scope. Post-M1 deferred.
- **macOS wheels** — same; Phase 6 skips. Post-M1 deferred.
- **aarch64 Linux wheels** — deferred. Easy to add via cibuildwheel once demand is demonstrated.
- **musllinux / Alpine support** — deferred; glibc-only for Phase 6.
- **PyPy support** — deferred; CPython 3.10+ only. ctypes works on PyPy in principle but no test coverage planned.

### Deferred Python-Binding Features

- **Async / callback-based processing** — out of scope; Phase 6's binding is synchronous block-call only. M4 plugin host handles realtime callback semantics.
- **Observer / change-callback pattern for registers** — Phase 2 D-21 deferred to Controllers era; Phase 6 respects that. Polling via `spu94.get_reg_*` is the only read path.
- **Direct exposure of `q15_mul_truncate` or `sat_s16`** — explicitly NOT exposed. These are internal math helpers; exposing them from Python would invite users to re-implement pieces of the reverb in Python, which is out of scope.

### Raised in Discussion, Routed Elsewhere

- **"Does the PS1 auto-convert audio formats in realtime?"** (user, 2026-04-21) — Clarified during Phase 6 discuss: no, the PS1 SPU has no conversion layer because it has no non-int16 audio to convert from. Everything inside the SPU is 16-bit signed integer. Phase 6's strict numpy contract (D-09..D-11) reinforces this posture at the Python-to-C boundary — the binding refuses non-int16 input so it doesn't invent a conversion the hardware never had. Recorded to avoid re-litigating the strict-vs-forgiving choice in future phases.
- **"I actually kind of want all three tones in there to some extent"** (user, 2026-04-21) — Superseded by the final "polished tone, extensive scope" pick for the README. The multi-tone instinct is honored naturally by extensive scope: the hero/pitch reads polished; the status block reads honest-factual; the "For the DSP-curious" section reads deep-technical. All three appear — but the surrounding voice is polished throughout, not apologetic.
- **"I am not a coder"** (user, 2026-04-21) — This is permanent orientation for every Phase 6 user-facing artifact. The README, CLI `--help` text, and error messages are written for a recording/broadcast engineer, not for a software developer. Technical depth in the "DSP-curious" section is framed in signal-flow / character / DSP-musical terms, not in code-architecture terms.

### Reviewed Todos (not folded)

None — no pending todos existed at discussion time (confirmed via `gsd-tools todo match-phase 6` returning empty `matches`).

### Scope Creep Rejections

None raised during discussion. The four main gray areas (Python API shape, CLI implementation, register sync, numpy contract) and four smaller items (config JSON, fuzz migration, README, packaging) all stayed within the Phase 6 domain boundary (binding + CLI + wheel + README).

</deferred>

---

*Phase: 06-python-binding-cli*
*Context gathered: 2026-04-21*
*Next step: `/gsd-plan-phase 6` — planner consumes this CONTEXT.md. Planner will first invoke research (for scikit-build-core + cibuildwheel layout specifics, dr_wav vendoring tidy patterns, ctypes IntEnum-from-reflection idioms), then task breakdown across approximately 5 plans.*
