# Phase 6: Python Binding + CLI - Research

**Researched:** 2026-04-21
**Domain:** Python packaging (scikit-build-core + cibuildwheel) + ctypes ergonomics + native CLI vendoring
**Confidence:** HIGH (all 2025-2026 sources; tool versions verified against live PyPI)

## Summary

Phase 6 is a packaging phase. The algorithm is locked; the surface layer around it is where every Phase 6 decision lives — how a Python user finds `spu94.Register`, how a pip-install user gets the `spu94 --preset hall in.wav out.wav` command, how a wheel file reaches PyPI's Linux glibc baseline without rebuilding per Python minor.

The four technical risk areas are (1) **scikit-build-core layout** — getting `libspu94.so` and the `spu94` executable both installed inside `python/spu94/` with a working `$ORIGIN`-relative RPATH; (2) **cibuildwheel `py3-none` tag** — producing a single wheel that covers Python 3.10..3.14 rather than one per minor; (3) **ctypes runtime reflection** — walking `spu94_reg_name(i)` at import time to build `IntEnum` dynamically with clean diagnostics on drift; and (4) **numpy strict int16 contract** — using `np.ctypeslib.ndpointer` as the validation surface so zero-copy falls out of the shape of the accepted input rather than being a separate codepath. None of these are exotic; all have ecosystem precedent and official docs covering exactly this scenario.

**Primary recommendation:** Use `scikit-build-core>=0.10` with `wheel.py-api = "py3"` and `cibuildwheel>=3.4.1` with `build = ["cp310-manylinux_x86_64"]` + `skip` everything else — this produces the single `spu94-0.1.0-py3-none-manylinux_2_28_x86_64.whl` the user wants. Vendor dr_wav (public-domain / MIT-0) and jsmn (MIT, 471 lines) as single-header drop-ins under `vendor/`, both linked only into the CLI binary. Use `numpy.ctypeslib.ndpointer(dtype='i2', ndim=1, flags='C_CONTIGUOUS')` for the numpy contract — it produces TypeError with `"array must have data type int16"` and `"array must have flags ['C_CONTIGUOUS']"` automatically, which is exactly the D-09 message shape.

## Project Constraints (from CLAUDE.md)

No project-level CLAUDE.md exists in this repo. The global user-level CLAUDE.md governs:

- Execution style: hands-on guided walkthroughs for deployed-system work. This is NOT a deployed-system phase (packaging + binding), so the walkthrough discipline does not apply to plan execution itself — but the user-facing artifacts (README, CLI `--help`, error messages) must remain plain-language and oriented toward a recording/broadcast engineer, not a software developer.

Project posture (from PROJECT.md):

- Plain C99/C11 core; ctypes (not pybind11 / cffi) per "Tech stack (tooling)" line.
- dr_wav is linked into the CLI binary only, never into `libspu94.so`.
- Paraphrase-not-transcribe on any nocash quotation in the README.
- GPL-sources-are-witnesses-not-primary — Mednafen / lv2-psx-reverb / DuckStation / MiSTer source code is NOT consulted for Python binding patterns. Original work from scikit-build-core + cibuildwheel docs only.

## User Constraints (from CONTEXT.md)

### Locked Decisions (D-01..D-25, verbatim from `06-CONTEXT.md`)

**Area A — Python API shape:**
- **D-01:** Expose BOTH raw-panel module functions (state handle explicit, matches C 1:1) AND a thin `SPU94` class wrapping the handle. Class is sugar over the raw layer, not a second implementation.
- **D-02:** Factory presets importable as Python data via `spu94.presets` keyed by name AND by enum id. Underlying storage is the C `spu94_presets[]` `.rodata` read at import time.

**Area B — CLI implementation:**
- **D-03:** Native C binary via CMake. dr_wav vendored at `vendor/dr_wav/` and linked into the CLI binary ONLY.
- **D-04:** Python entry_point shim in `python/spu94/cli.py` that shells out to the compiled binary (`[project.scripts]`).
- **D-05:** Any error exits non-zero with a one-line actionable stderr message. No tracebacks. Standard-style prefix (e.g., `spu94: error: unknown preset 'hll' — valid: off, room, ...`).

**Area C — Register sync + struct drift:**
- **D-06:** Runtime reflection builds the `Register` IntEnum at import — iterate `spu94_reg_name(i)` + `spu94_reg_hw_offset(i)` for `i in 0..SPU94_REG__COUNT-1`. Live library is authoritative.
- **D-07:** Import-time assertions: `spu94_state_size() <= SPU94_STATE_SIZE_MAX`, `len(Register) == 35`, `len(Preset) == 10`. Mismatch → `RuntimeError("spu94 library mismatch: ...")`.
- **D-08:** Struct-internal offsets (`PENDING_MASK_OFFSET`, `FIR_IDX_*_OFFSET`) have no public C accessor and stay hand-typed in fuzz scripts with labeled warning blocks. Planner may optionally add a tests-only `spu94_debug_offset(field_id)` accessor (not required).

**Area D — numpy contract:**
- **D-09:** Strict int16 C-contiguous arrays required on `spu94.process` / `spu94.flush`. Equal length across L_in, R_in, L_out, R_out. Violation → `TypeError` (dtype) / `ValueError` (layout/size) with actionable message.
- **D-10:** Zero-copy guaranteed when contract holds. No per-call conversion.
- **D-11:** PS1 hardware has no format-conversion layer — strict is *more* faithful than forgiving. Recorded because user raised it explicitly.

**Area E — `--config preset.json`:**
- **D-12:** Dual shape with auto-detect by `"base"` key. Override shape OR flat register map (35 keys required if flat).
- **D-13:** Values accepted as JSON integers OR hex strings (`"0x3F00"`). Signed (`v*`) vs unsigned (`d*` / `m*`) range-checked.
- **D-14:** Unknown register names are errors (not ignored).
- **D-15:** README showcases override shape as everyday entry; flat as debug / golden-file reproduction.

**Area F — Fuzz migration:**
- **D-16:** All four fuzz scripts drop hand-typed register constants, import from new binding.
- **D-17:** Struct offsets stay hand-typed with warning blocks.
- **D-18:** CMake / ctest wiring unchanged.

**Area G — README:**
- **D-19:** Polished tone throughout. No apologetic framing. Status communicated via a dedicated status block.
- **D-20:** 11 sections (hero, status, quick install, Python walkthrough, CLI walkthrough, DSP-curious, roadmap, architecture overview, licensing posture, bibliography, contributing).

**Area H — Packaging:**
- **D-21:** `manylinux_2_28` Linux wheel (glibc 2.28+).
- **D-22:** Python 3.10+ minimum.
- **D-23:** One wheel per platform, not per Python minor. Pure ctypes → `py3-none-manylinux_2_28_x86_64` tag.
- **D-24:** Wheel layout: `libspu94.so` + CLI binary ship inside `spu94/` package dir alongside `__init__.py`.
- **D-25:** `pyproject.toml` holds build config (scikit-build-core + cibuildwheel tables).

### Claude's Discretion

- Exact naming of raw-panel functions (`spu94.process` vs `spu94.process_block`).
- Exact class name (`SPU94` vs `Reverb` vs `SPU94Reverb`) — recommendation: `SPU94`.
- Exact wording of numpy error messages (D-09 has seeds; planner refines).
- Internal organization of `python/spu94/` — single file vs split (recommended split: `_binding.py`, `api.py`, `reverb.py`, `presets.py`, `cli.py`, `__init__.py`).
- CLI source layout (`src/cli/main.c` vs `tools/spu94/main.c`) — vendored dr_wav path suggestion: `vendor/dr_wav/`.
- Exact IntEnum class names (`spu94.Register` vs `spu94.Reg`).
- Whether `SPU94` class uses `__enter__` / `__exit__` — recommendation: yes.
- ASCII signal-flow diagram vs prose for README architecture overview.
- `pyproject.toml` exact metadata (classifiers, keywords, URLs).
- cibuildwheel matrix — `pytest` test vs minimal smoke test.
- Whether to ship `spu94[dev]` extra.
- Whether CLI shim uses `os.execv` (recommended — exit code passes naturally) or `subprocess.run`.
- ADR split in `docs/DECISIONS.md` (per-decision vs natural groupings).

### Deferred Ideas (OUT OF SCOPE for Phase 6)

- Witness-diff harness against lv2-psx-reverb (Phase 7, TEST-03)
- Golden-file regression tests per preset (Phase 7, TEST-04)
- Modulation test per register (Phase 7, TEST-05)
- `docs/LEVERS-CATALOG.md` (Phase 7, DOCS-02)
- `docs/BIBLIOGRAPHY.md` comprehensive entries (Phase 7, DOCS-03)
- MCU cross-compile (Phase 8, BUILD-03)
- JUCE / VST3 / AU / LV2 wrapper (Milestone 4)
- Named musical levers / parameter smoothing / CV mappings / plugin UI (Milestone 4)
- Mid-stream preset morph / crossfade (Milestone 4)
- Custom preset import/export via UI (Milestone 4)
- Windows / macOS / aarch64 / musllinux wheels (post-M1)
- PyPy support
- Async / callback-based processing
- Observer / change-callback pattern for registers (Controllers era)
- Direct exposure of `q15_mul_truncate` or `sat_s16` to Python

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PYBIND-01 | ctypes wrapper exposing the full C API | Standard Stack § ctypes + Architecture Patterns § raw-panel/class split |
| PYBIND-02 | numpy array interop via `ndpointer`; zero-copy where possible | Standard Stack § numpy.ctypeslib.ndpointer + Code Example § numpy contract validator |
| PYBIND-03 | Register identifiers exposed as Python IntEnum matching C enum values | Architecture Patterns § runtime-reflection IntEnum + Code Example § IntEnum builder |
| PYBIND-04 | Factory preset fixtures loadable from Python | Architecture Patterns § preset table import via ctypes Structure |
| PYBIND-05 | Struct-layout drift caught by runtime assertion at import time | Code Example § import-time assertion block |
| PYBIND-06 | Buildable into a wheel via scikit-build-core + cibuildwheel on Linux | Standard Stack § scikit-build-core + cibuildwheel; Code Example § `pyproject.toml` |
| CLI-01 | `spu94` CLI reads WAV, applies preset, writes WAV | Standard Stack § dr_wav; Code Example § CLI main loop |
| CLI-02 | Accepts `--preset <name>` OR `--config <path.json>` | Standard Stack § getopt_long + jsmn; Code Example § JSON parser |
| CLI-03 | Uses vendored dr_wav; not linked into core library | Don't Hand-Roll § WAV I/O; Common Pitfalls § dr_wav scope leak |
| CLI-04 | Exits non-zero with one-line actionable stderr on any error | Code Example § error-taxonomy table |
| DOCS-04 | README with build + minimal examples + licensing + status banner | Architecture Patterns § polished-tone README structure |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| scikit-build-core | `>=0.10` (current: 0.12.2, 2026-03-05) | Python build backend that drives CMake from `pyproject.toml` | `[VERIFIED: pypi.org/pypi/scikit-build-core/json]` — 2025-era replacement for classic scikit-build; first-class `wheel.py-api = "py3"` support; maintained by the same pypa-adjacent group that ships cibuildwheel. |
| cibuildwheel | `>=3.4.1` (current: 3.4.1, 2026-04-02) | Build Linux wheels in manylinux Docker containers | `[VERIFIED: pypi.org/pypi/cibuildwheel/json]` — canonical PyPA tool; manylinux_2_28 is default since May 2025 on x86_64; explicit `py3-none-{platform}` mode for ctypes projects since v2.6. NOTE: cibuildwheel 3.x itself requires Python 3.11+ to run (`[CITED: cibuildwheel PyPI metadata]`). This is a build-tool requirement only — the built wheel still supports Python 3.10+ per D-22. |
| numpy | `>=1.23` (current: 2.4.4, 2026-03-29) | Stereo int16 audio array transport | `[VERIFIED: pypi.org/pypi/numpy/json]` + CONTEXT D-09/D-10. Pinned to `>=1.23` because that's the first release where `numpy.ctypeslib.ndpointer` flag validation behaviour has been stable; 2.x is fully compatible with the binding (empirically verified on numpy 2.2.4 in the dev environment — `ndpointer` rejects `float32` with `"array must have data type int16"` and non-contiguous with `"array must have flags ['C_CONTIGUOUS']"` unchanged from 1.x). |
| ctypes | Python stdlib (3.10+) | Foreign function interface | CONTEXT D-01 locked choice. Zero dependency surface beyond the interpreter. |
| dr_wav (`dr_wav.h`) | v0.14.6 (2026) | Single-header C WAV reader/writer | `[VERIFIED: raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h]` — dual-licensed public-domain OR MIT-0. Compatible with MIT/Apache-2.0 posture. Single `#define DR_WAV_IMPLEMENTATION` + `#include` in one TU. |
| jsmn | v1.1.0 (stable, 2020) | Single-header JSON tokenizer for the CLI `--config` parser | `[VERIFIED: raw.githubusercontent.com/zserge/jsmn/master/LICENSE]` — MIT licensed; 471 lines total; zero allocations; produces tokens via a flat `jsmntok_t[]` array the caller sizes and owns. Best fit for the flat register-map + override shape in D-12..D-15; no stdlib dependencies beyond `<stddef.h>`. |
| GNU getopt_long | libc (glibc 2.28+) | CLI argument parsing (`--preset`, `--config`, `--list-presets`, `--help`, `--tail-seconds`) | `[VERIFIED: /usr/include/getopt.h on dev host; ldd 2.42]` — universally available on the manylinux_2_28 baseline; no vendored dependency. |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| auditwheel | `>=6.0` (latest via pip) | Linux wheel repair (bundles external `.so` deps + rewrites RPATH) | Invoked automatically by cibuildwheel via `repair-wheel-command`; the binding does NOT need external-lib bundling since `libspu94.so` ships inside the wheel. auditwheel still rewrites any `DT_NEEDED` references inside `libspu94.so` (libc only — already a manylinux-policy-compatible symbol set per Phase 1/5 grep guard). `[CITED: auditwheel README, pypa/auditwheel on GitHub]` |
| setuptools | N/A — not used | (would be used for pybind11-style projects; scikit-build-core replaces it) | — |
| Python 3 + pytest | Python 3.10+ | Binding unit tests (dtype rejects, IntEnum import smoke) | Phase 6 test harness; already a Phase 1-5 dependency for fuzz scripts |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| ctypes | cffi | `[ASSUMED]` cffi's advantage is declarative C headers parsed at build; but CONTEXT D-01 is locked on ctypes per PROJECT.md "minimize maintenance surface" line. Rejected. |
| ctypes | pybind11 | pybind11 is C++-only; cannot bind a C library without adding C++ surface. PROJECT.md forbids C++ in the core. Rejected. |
| scikit-build-core | setuptools + `bdist_wheel` override | The [joerick/python-ctypes-package-sample](https://github.com/joerick/python-ctypes-package-sample) uses this pattern — it's the cibuildwheel official example. But setuptools doesn't integrate with CMake natively; scikit-build-core exists specifically to drive CMake from `pyproject.toml` without a `setup.py` maze. Rejected in favor of scikit-build-core. |
| scikit-build-core | meson-python | meson-python is fine but the project is already on CMake (Phases 1–5); switching build systems is out of scope. Rejected. |
| cibuildwheel | `python -m build` directly | `build` produces a `linux_x86_64` wheel, not a `manylinux_2_28_x86_64` wheel. Plain linux wheels can't be uploaded to PyPI. cibuildwheel wraps auditwheel + manylinux Docker images to produce distributable wheels. Required for D-21. |
| dr_wav | libsndfile | libsndfile is LGPL-2.1 — compatible with MIT/Apache-2.0 permissively but requires dynamic linking discipline (or a static-link licensing exception) that's more surface than we need. dr_wav is public-domain/MIT-0 and handles the int16 WAV subset we care about trivially. Rejected. |
| dr_wav | Custom WAV reader | The WAV format has trap doors (odd chunk layouts, extensible fmt with DS64, 24-bit quirks). CONTEXT says explicitly "don't hand-roll WAV I/O" — dr_wav handles malformed inputs gracefully, which CLI-04 one-line-error semantics depend on. Rejected custom. |
| jsmn | cJSON | cJSON is MIT, ~900 lines `.c` + header. Heavier: it builds a tree of `cJSON*` nodes (allocations per node). jsmn is zero-allocation — the caller owns the token array. For our flat register map + one-level-nested override shape, jsmn is a better fit. Chose jsmn. |
| jsmn | parson | parson is MIT, ~2000 lines. Heavier than cJSON. Rejected. |
| jsmn | Hand-rolled recursive-descent parser | Would be ~150 lines for our specific grammar. Declined — jsmn's token array is safer against malformed input (handles trailing commas, quoted keys, number/string ambiguity) with a vetted parser. MIT, 471 lines is cheap. |
| getopt_long | argparse-c / docopt.c | Both require vendoring. getopt_long is already in libc on every manylinux target. No vendor. |
| manylinux_2_28 | manylinux_2_34 | 2_34 is newer (Rocky Linux 9 / glibc 2.34+) but uses x86-64-v2 microarchitecture targeting, which can silently produce binaries that don't run on older hardware (`[CITED: cibuildwheel docs § manylinux image defaults warning]`). For a community reverb tool, 2_28 is the safer choice (Ubuntu 20.04+, Debian 11+, RHEL 8+, Fedora 30+ — covers ~99% of modern Linux). |
| manylinux_2_28 | manylinux2014 | EOL as of June 2025; would require opt-in to an EOL image. Rejected. |

**Installation (for a developer building from source):**

```bash
# One-time dev deps (outside any venv)
pip3 install --user --break-system-packages pipx
pipx install cibuildwheel
pipx install build  # for local non-containerized wheel builds (debug)

# Inside the project
pip3 install -e .  # editable install — triggers scikit-build-core → CMake → libspu94.so + CLI into python/spu94/
```

**Version verification (performed 2026-04-21):**

```bash
curl -sL https://pypi.org/pypi/scikit-build-core/json | jq -r .info.version
# -> 0.12.2 (uploaded 2026-03-05)

curl -sL https://pypi.org/pypi/cibuildwheel/json | jq -r .info.version
# -> 3.4.1 (uploaded 2026-04-02)

curl -sL https://pypi.org/pypi/numpy/json | jq -r .info.version
# -> 2.4.4 (uploaded 2026-03-29)
```

Pin to minimum-working versions in `pyproject.toml`, not latest — latest's recency is good evidence the tools are actively maintained, but pinning `>=0.10` for scikit-build-core and `>=3.1` for cibuildwheel gives users room to upgrade without forcing them to bleeding-edge. Both tools respect their `minimum-version` / back-compat discipline `[CITED: scikit-build-core configuration docs § "Minimum version & defaults"]`.

## Architecture Patterns

### Recommended Python Package Structure

```
python/spu94/
├── __init__.py       # Public re-exports; import-time assertions; IntEnum + Preset IntEnum
├── _binding.py       # Raw ctypes CDLL + prototype declarations (argtypes / restype)
├── api.py            # Raw-panel public functions (state handle explicit)
├── reverb.py         # SPU94 class (thin wrapper over api.py + __enter__/__exit__)
├── presets.py        # Preset table import via ctypes.Structure; .presets accessor
├── cli.py            # entry_point shim that os.execv's the compiled binary
├── libspu94.so       # Installed by CMake install(TARGETS spu94_shared)
└── spu94             # Installed by CMake install(TARGETS spu94_cli) — the native CLI binary
```

Rationale: one-concern-per-module mirrors the C-side Plan 02/03 discipline. Auditing a bug in the ctypes prototypes goes to `_binding.py`; a bug in the strict numpy contract goes to `api.py`; a bug in the preset import goes to `presets.py`. `__init__.py` is ~50 lines: imports, assertions, enum build, re-exports.

### Recommended CLI Source Tree

```
src/cli/
├── CMakeLists.txt    # spu94_cli executable target; links spu94_shared + dr_wav + jsmn; getopt_long; installs into python/spu94/
└── main.c            # argument parsing + dr_wav open/close + spu94_process loop + JSON parsing
vendor/
├── dr_wav/
│   ├── dr_wav.h      # v0.14.6 vendored verbatim
│   └── LICENSE       # dr_wav's dual public-domain / MIT-0 notice preserved
└── jsmn/
    ├── jsmn.h        # MIT vendored verbatim (471 lines)
    └── LICENSE       # jsmn's MIT notice preserved
```

Rationale: two separate `vendor/<lib>/` directories each with its own `LICENSE` preserved matches PROJECT.md licensing discipline (paraphrase-not-transcribe for prose; direct inclusion with license-preservation for permissive single-header libs is fine).

### Pattern 1: scikit-build-core CMake install into package dir

**What:** CMake installs `libspu94.so` and the `spu94_cli` binary into `${SKBUILD_PLATLIB_DIR}/spu94/` so they end up alongside `__init__.py` inside the wheel.

**When to use:** Every ship path — wheel build, editable install, from-source dev install.

**Example:**

```cmake
# src/spu94/CMakeLists.txt  (existing — unchanged for lib target; Phase 6 ADDS install rule)
add_library(spu94_shared SHARED $<TARGET_OBJECTS:spu94_obj>)
set_target_properties(spu94_shared PROPERTIES OUTPUT_NAME spu94)

# Phase 6: install into the Python package directory (only under scikit-build-core)
if(DEFINED SKBUILD_PROJECT_NAME)
    install(TARGETS spu94_shared
            LIBRARY DESTINATION "${SKBUILD_PROJECT_NAME}"
    )
endif()

# src/cli/CMakeLists.txt  (new)
add_executable(spu94_cli main.c)
set_target_properties(spu94_cli PROPERTIES OUTPUT_NAME spu94)
target_link_libraries(spu94_cli PRIVATE spu94_shared)
target_include_directories(spu94_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/dr_wav
    ${CMAKE_SOURCE_DIR}/vendor/jsmn
)

# $ORIGIN RPATH — the installed CLI sits next to libspu94.so inside the package dir,
# so it finds libspu94.so via $ORIGIN/. Same RPATH strategy documented in
# scikit-build-core's dynamic_link.md. macOS-equivalent: @loader_path (Phase 6 is
# Linux-only, but keep the if(APPLE) branch for future portability).
if(APPLE)
    set(origin_token "@loader_path")
else()
    set(origin_token "$ORIGIN")
endif()
set_property(TARGET spu94_cli PROPERTY INSTALL_RPATH "${origin_token}")

if(DEFINED SKBUILD_PROJECT_NAME)
    install(TARGETS spu94_cli
            RUNTIME DESTINATION "${SKBUILD_PROJECT_NAME}"
    )
endif()
```

**Source:** `[CITED: scikit-build-core docs § Authoring your CMakeLists — SKBUILD_PROJECT_NAME / SKBUILD_PLATLIB_DIR]` + `[CITED: scikit-build-core docs § Dynamic linking — $ORIGIN / @loader_path pattern]` on raw.githubusercontent.com/scikit-build/scikit-build-core/main/docs/guide/.

**Why this shape:**
- `install(TARGETS ... LIBRARY DESTINATION "${SKBUILD_PROJECT_NAME}")` installs `libspu94.so` into the platlib subdirectory named after the package — `site-packages/spu94/libspu94.so` after pip install.
- `install(TARGETS ... RUNTIME DESTINATION "${SKBUILD_PROJECT_NAME}")` does the same for the executable.
- The `$ORIGIN` RPATH is relative to the binary's own location at runtime, so the CLI finds `libspu94.so` regardless of where the wheel was unpacked.
- Guarding the install rules with `if(DEFINED SKBUILD_PROJECT_NAME)` keeps the install rules inert for normal CMake builds (so `cmake --build build && ctest` still works without scikit-build-core driving).

### Pattern 2: `pyproject.toml` — scikit-build-core + cibuildwheel combined config

**What:** A single `pyproject.toml` holds both build-backend config (scikit-build-core) and downstream wheel-matrix config (cibuildwheel) — the user builds with `pip install .` for dev and `cibuildwheel --only cp310-manylinux_x86_64` for distribution wheels.

**When to use:** This is the `D-25` locked artifact.

**Example:**

```toml
[build-system]
requires = ["scikit-build-core>=0.10"]
build-backend = "scikit_build_core.build"

[project]
name = "spu94"
version = "0.1.0"
description = "Bit-faithful PlayStation 1 SPU reverb — Python binding + CLI"
readme = "README.md"
requires-python = ">=3.10"
authors = [{ name = "Anthony Accurso", email = "anthonyaccurso@gmail.com" }]
license = { file = "LICENSE" }  # placeholder per Phase 1 D-05
keywords = ["audio", "dsp", "reverb", "playstation", "ps1", "spu", "bit-faithful"]
classifiers = [
    "Development Status :: 4 - Beta",
    "Intended Audience :: Developers",
    "Intended Audience :: Science/Research",
    "Operating System :: POSIX :: Linux",
    "Programming Language :: C",
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3 :: Only",
    "Topic :: Multimedia :: Sound/Audio",
    "Topic :: Multimedia :: Sound/Audio :: Analysis",
]
dependencies = [
    "numpy>=1.23",
]

[project.urls]
Homepage = "https://github.com/<owner>/spu94"
Source   = "https://github.com/<owner>/spu94"
Issues   = "https://github.com/<owner>/spu94/issues"

[project.scripts]
# The Python shim in python/spu94/cli.py calls the compiled binary installed
# alongside it. Planner chooses os.execv (clean exit-code pass-through) over
# subprocess.run.
spu94 = "spu94.cli:main"

[tool.scikit-build]
minimum-version    = "build-system.requires"  # read from build-system.requires
cmake.version      = ">=3.20"                 # matches existing CMakeLists.txt minimum
cmake.build-type   = "Release"
wheel.packages     = ["python/spu94"]         # where the Python source lives
wheel.py-api       = "py3"                    # D-23: pure ctypes → one wheel per PLATFORM, not per Python minor
# scikit-build-core auto-finds python/spu94; wheel.packages is explicit for clarity.

[tool.cibuildwheel]
# D-23: pure ctypes → one wheel per platform. Picking exactly ONE build
# identifier (cp310-manylinux_x86_64) produces the wheel; because wheel.py-api
# = "py3" is set, the resulting wheel is labeled py3-none-manylinux_2_28_x86_64
# and satisfies Python 3.10..3.14+ without rebuild (cibuildwheel will still
# RUN tests across selected Python versions; see test-command below).
build = ["cp310-manylinux_x86_64"]
# Defensive skip — everything else. Prevents accidental matrix expansion.
skip = [
    "pp*",                # PyPy
    "cp3??t-*",           # free-threaded
    "*-musllinux_*",      # musl
    "*-manylinux_i686",   # 32-bit x86
    "*-win*",             # Windows
    "*-macosx*",          # macOS
    "*-android*", "*-ios*", "*-pyodide*",
]
manylinux-x86_64-image = "manylinux_2_28"  # D-21
archs = ["x86_64"]

# Smoke test runs on every supported Python minor INSIDE the manylinux
# container. Planner picks the smoke-test shape.
test-command = 'python -c "import spu94; spu94.self_test()"'
# Planner may widen with pytest:
# test-command = "pytest {project}/tests/python/binding"
# test-requires = ["pytest"]
```

**Source:**
- scikit-build-core sections `[CITED: raw.githubusercontent.com/scikit-build/scikit-build-core/main/docs/configuration/index.md — wheel.packages, wheel.py-api]`.
- cibuildwheel sections `[CITED: raw.githubusercontent.com/pypa/cibuildwheel/main/docs/options.md — build, skip, manylinux-x86_64-image, archs, test-command, test-skip]`.
- `py3-none-{platform}` tag support added in cibuildwheel v2.6 `[CITED: raw.githubusercontent.com/pypa/cibuildwheel/main/docs/changelog.md]`.

**Why this shape:**
- `build = ["cp310-manylinux_x86_64"]` alone would build *one* CPython 3.10 wheel. `wheel.py-api = "py3"` overrides the Python-tag portion of the filename so the output is `spu94-0.1.0-py3-none-manylinux_2_28_x86_64.whl`.
- cibuildwheel sees `py3-none` and recognizes the same wheel is reused across all selected Python versions — it doesn't rebuild for cp311/cp312/cp313/cp314, but it does RUN `test-command` in each to verify runtime compatibility.
- `skip` is defensive redundancy. `build` alone would already exclude everything not matching; the skip list is a tripwire against future cibuildwheel defaults changing.

### Pattern 3: Runtime-reflected IntEnum (D-06)

**What:** Build `spu94.Register` IntEnum dynamically at import time by iterating `spu94_reg_name(i)` + `spu94_reg_hw_offset(i)`. Live library is authoritative — Python has no hand-typed parallel list.

**When to use:** Always for register names. Same pattern for `spu94.Preset` — iterate `0..SPU94_PRESET__COUNT` and call a (to-be-added — planner may introduce `spu94_preset_name(id)`, or read from `spu94_presets[id].name`) name accessor.

**Example:**

```python
# python/spu94/__init__.py  (abbreviated)
import ctypes
from enum import IntEnum
from pathlib import Path
import os

# ---------- Library location ----------------------------------------------
# Environment override for dev convenience (matches the pre-Phase-6 fuzz
# scripts); falls back to the installed libspu94.so next to this file.
_LIB_PATH = os.environ.get("SPU94_LIB") or str(Path(__file__).parent / "libspu94.so")
_lib = ctypes.CDLL(_LIB_PATH)

# ---------- Minimal prototype declarations needed at import time ---------
_lib.spu94_state_size.restype  = ctypes.c_size_t
_lib.spu94_state_size.argtypes = []
_lib.spu94_reg_name.restype     = ctypes.c_char_p
_lib.spu94_reg_name.argtypes    = [ctypes.c_int]
_lib.spu94_reg_hw_offset.restype = ctypes.c_uint16
_lib.spu94_reg_hw_offset.argtypes = [ctypes.c_int]
_lib.spu94_get_latency_samples.restype = ctypes.c_uint32
_lib.spu94_get_latency_samples.argtypes = []

# ---------- Runtime drift detection ---------------------------------------
# Public constants (mirror the C macros; recorded at import time for later
# reference). The import-time asserts below validate them against the live
# library.
SPU94_STATE_SIZE_MAX = 16384            # from include/spu94/spu94.h
SPU94_REG__COUNT     = 35               # expected — validated below
SPU94_PRESET__COUNT  = 10               # expected — validated below

_state_size = _lib.spu94_state_size()
if _state_size > SPU94_STATE_SIZE_MAX:
    raise RuntimeError(
        f"spu94 library mismatch: spu94_state_size() = {_state_size} exceeds "
        f"SPU94_STATE_SIZE_MAX = {SPU94_STATE_SIZE_MAX}. "
        f"Library is from a future incompatible build — upgrade the Python binding."
    )

# ---------- Reflect Register IntEnum from the live library ----------------
def _reflect_registers():
    members = {}
    for i in range(SPU94_REG__COUNT):
        name_bytes = _lib.spu94_reg_name(i)
        if not name_bytes:
            raise RuntimeError(
                f"spu94 library mismatch: spu94_reg_name({i}) returned NULL. "
                f"Library reports fewer than SPU94_REG__COUNT={SPU94_REG__COUNT} registers. "
                f"Recompile the bindings against the live library, or pin the library version."
            )
        members[name_bytes.decode("ascii")] = i
    # Extra safety: one-past-end must return NULL.
    if _lib.spu94_reg_name(SPU94_REG__COUNT) is not None:
        raise RuntimeError(
            f"spu94 library mismatch: spu94_reg_name({SPU94_REG__COUNT}) returned non-NULL. "
            f"Library has MORE than {SPU94_REG__COUNT} registers. Recompile the bindings."
        )
    return IntEnum("Register", members, module=__name__)

Register = _reflect_registers()
# Validate cached cross-check
assert len(Register) == SPU94_REG__COUNT, "internal: Register enum length mismatch"

# Similar pattern for Preset (planner fills in using spu94_presets[id].name via
# ctypes.Structure binding — see Code Examples § preset table import).
```

**Source:** `[CITED: docs.python.org/3/library/enum.html § IntEnum.__call__ functional API]` + live verification of behavior on dev host (numpy 2.2.4, Python 3.13.7 — ndpointer rejections confirmed empirically in this session).

**Error taxonomy (for D-07 mismatch diagnostics):**

| Failure | Message shape |
|---------|--------------|
| State struct grew | `spu94 library mismatch: spu94_state_size() = N exceeds SPU94_STATE_SIZE_MAX = M. Library is from a future incompatible build — upgrade the Python binding.` |
| Register count shrank | `spu94 library mismatch: spu94_reg_name(i) returned NULL. Library reports fewer than SPU94_REG__COUNT=N registers. Recompile the bindings against the live library, or pin the library version.` |
| Register count grew | `spu94 library mismatch: spu94_reg_name(N) returned non-NULL. Library has MORE than N registers. Recompile the bindings.` |
| Preset count mismatched | Analogous; planner adds when building Preset enum. |
| Library not found | Standard `OSError: libspu94.so: cannot open shared object file` from `ctypes.CDLL(...)` — let it propagate unchanged (clear diagnostic already). |

### Pattern 4: numpy strict int16 contract via ndpointer

**What:** Declare `argtypes` of `_lib.spu94_process` using `numpy.ctypeslib.ndpointer(dtype='i2', ndim=1, flags='C_CONTIGUOUS')` — numpy does the validation; Python receives clean `TypeError` on violation.

**When to use:** Every `_lib.spu94_process` / `_lib.spu94_flush` prototype.

**Example:**

```python
# python/spu94/_binding.py  (excerpt)
import ctypes
import numpy as np

_ARR_I16_1D = np.ctypeslib.ndpointer(dtype=np.int16, ndim=1, flags='C_CONTIGUOUS')

_lib.spu94_process.restype = None
_lib.spu94_process.argtypes = [
    ctypes.c_void_p,   # state
    _ARR_I16_1D,       # L_in
    _ARR_I16_1D,       # R_in
    _ARR_I16_1D,       # L_out
    _ARR_I16_1D,       # R_out
    ctypes.c_uint32,   # num_samples
]
```

```python
# python/spu94/api.py  (excerpt)
def process(state, L_in, R_in, L_out, R_out):
    """Block-process stereo int16 @ 44.1 kHz. D-09: strict contract."""
    n = len(L_in)
    if not (len(R_in) == len(L_out) == len(R_out) == n):
        raise ValueError(
            f"spu94.process requires equal-length arrays; got "
            f"L_in={len(L_in)} R_in={len(R_in)} L_out={len(L_out)} R_out={len(R_out)}"
        )
    _lib.spu94_process(state, L_in, R_in, L_out, R_out, n)
    # ndpointer raises TypeError automatically on dtype / flags mismatch.
    # We only need to add the cross-array length check ourselves.
```

**Empirical verification (performed 2026-04-21 on dev host):**

```
>>> import numpy as np, ctypes
>>> t = np.ctypeslib.ndpointer(dtype=np.int16, ndim=1, flags='C_CONTIGUOUS')
>>> t.from_param(np.zeros(10, dtype=np.float32))
TypeError: array must have data type int16
>>> t.from_param(np.zeros((10, 2), dtype=np.int16)[:, 0])
TypeError: array must have flags ['C_CONTIGUOUS']
>>> t.from_param(np.zeros(10, dtype=np.int16))
<_ctypes.LP_c_short object at ...>   # accepted — zero-copy
```

The error messages are exactly the shape CONTEXT D-09 seeds. Planner can wrap the TypeError with an outer `try/except` that adds the argument name (`L_in`, `R_in`, etc.) for more actionable diagnostics if desired — but the base ndpointer messages are already acceptable.

**Zero-copy guarantee:** `ndpointer.from_param(arr)` returns `arr.ctypes.data_as(ctypes.POINTER(c_int16))` equivalent — the pointer aliases the numpy array's buffer. No intermediate allocation. `[CITED: numpy/main/doc/source/reference/routines.ctypeslib.rst]`.

### Pattern 5: Preset table import via ctypes.Structure

**What:** Mirror the C `spu94_preset_t` struct as a `ctypes.Structure` with `_fields_ = [("name", c_char_p), ("regs", c_int16 * 35)]`. Read the `spu94_presets[]` symbol via `ctypes.CDLL` and expose each preset as a Python namedtuple or dataclass.

**When to use:** Import-time preset table construction (D-02).

**Example:**

```python
# python/spu94/presets.py  (excerpt)
import ctypes
from dataclasses import dataclass
from enum import IntEnum
from . import _binding

_SPU94_REG_COUNT = 35
_SPU94_PRESET_COUNT = 10

class _CPreset(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("regs", ctypes.c_int16 * _SPU94_REG_COUNT),
    ]

# Read the extern const spu94_preset_t spu94_presets[10] symbol directly.
# This is well-defined ctypes — the .in_dll() accessor binds to a named symbol.
_presets_arr = (_CPreset * _SPU94_PRESET_COUNT).in_dll(
    _binding._lib, "spu94_presets"
)

# Build Preset IntEnum from the preset names in the C-side table.
_preset_names = [_presets_arr[i].name.decode("ascii") for i in range(_SPU94_PRESET_COUNT)]
Preset = IntEnum("Preset", {
    n.upper().replace(" ", "_"): i for i, n in enumerate(_preset_names)
}, module=__name__)

@dataclass(frozen=True)
class PresetInfo:
    id: int
    name: str  # human-readable
    regs: tuple  # 35 int16 values indexed by Register
    def __iter__(self):
        return iter((self.id, self.name, self.regs))

_preset_infos = {}
for i in range(_SPU94_PRESET_COUNT):
    info = PresetInfo(
        id=i,
        name=_presets_arr[i].name.decode("ascii"),
        regs=tuple(_presets_arr[i].regs[j] for j in range(_SPU94_REG_COUNT)),
    )
    _preset_infos[i] = info
    _preset_infos[info.name] = info
    _preset_infos[info.name.upper().replace(" ", "_")] = info

class _PresetTable:
    """Dict-like accessor: .presets['hall'] or .presets[Preset.HALL]."""
    def __getitem__(self, key):
        if isinstance(key, Preset):
            return _preset_infos[int(key)]
        if isinstance(key, int):
            return _preset_infos[key]
        return _preset_infos[key.lower()]
    def __iter__(self):
        return (_preset_infos[i] for i in range(_SPU94_PRESET_COUNT))
    def __len__(self):
        return _SPU94_PRESET_COUNT

presets = _PresetTable()
```

**Source:** `[CITED: docs.python.org/3/library/ctypes.html § Structure, § .in_dll() accessor]`. The `.in_dll()` approach for reading an extern array is exactly the mechanism ctypes was designed to support.

### Pattern 6: Entry-point shim via os.execv (CONTEXT Discretion)

**What:** `spu94 = "spu94.cli:main"` in `[project.scripts]` registers a Python shim. The shim locates the compiled `spu94` binary next to `__init__.py` and `os.execv`s it, passing through `argv[1:]`.

**When to use:** Always for the `spu94` command.

**Example:**

```python
# python/spu94/cli.py
"""CLI entry_point shim — locates the compiled spu94 binary and replaces the
Python process with it. Exit codes pass through naturally via os.execv."""
import os
import sys
from pathlib import Path

def main():
    here = Path(__file__).parent
    binary = here / "spu94"
    if not binary.exists():
        # Windows ships spu94.exe; this branch is a future-proofing hint.
        binary_exe = here / "spu94.exe"
        if binary_exe.exists():
            binary = binary_exe
        else:
            print(
                f"spu94: error: compiled binary not found at {binary}. "
                f"The wheel install may be corrupted; try pip install --force-reinstall spu94.",
                file=sys.stderr,
            )
            sys.exit(1)
    # os.execv replaces the Python interpreter with the binary.
    # argv[0] is conventionally the program name; argv[1:] are user args.
    os.execv(str(binary), [str(binary), *sys.argv[1:]])
```

**Source:** Standard `os.execv` pattern; documented in `[CITED: docs.python.org/3/library/os.html § os.execv]`. The `os.execv` approach is the one recommended by `[CITED: auditwheel repair.py _replace_elf_script_with_shim]` for the `.data/scripts/` case; we're using the same pattern for a binary inside the package dir.

### Pattern 7: CLI argument parsing via getopt_long (CLI-02)

**What:** `getopt_long` from libc handles `--preset <name>`, `--config <path>`, `--list-presets`, `--help`, `--tail-seconds <N>`, and short-option equivalents if desired.

**When to use:** `src/cli/main.c` top-level argument dispatch.

**Example:**

```c
// src/cli/main.c  (abbreviated skeleton)
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spu94/spu94.h>

static void print_help(void) {
    fputs(
        "Usage: spu94 [OPTIONS] INPUT.wav OUTPUT.wav\n"
        "\n"
        "Process a WAV file through the PS1 SPU reverb.\n"
        "\n"
        "Options:\n"
        "  --preset <name>        Apply a named factory preset (e.g., hall).\n"
        "  --config <path.json>   Apply a register-map or override preset from JSON.\n"
        "  --tail-seconds <N>     Append N seconds of reverb tail to the output.\n"
        "  --list-presets         List the 10 factory presets and exit.\n"
        "  -h, --help             Show this message and exit.\n"
        "\n"
        "Examples:\n"
        "  spu94 --preset hall in.wav out.wav\n"
        "  spu94 --config override.json in.wav out.wav\n",
        stdout);
}

int main(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"preset",       required_argument, NULL, 'p'},
        {"config",       required_argument, NULL, 'c'},
        {"tail-seconds", required_argument, NULL, 't'},
        {"list-presets", no_argument,       NULL, 'l'},
        {"help",         no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    const char *preset_name = NULL, *config_path = NULL;
    double tail_seconds = 0.0;
    int opt;
    while ((opt = getopt_long(argc, argv, "p:c:t:lh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'p': preset_name = optarg; break;
            case 'c': config_path = optarg; break;
            case 't': tail_seconds = strtod(optarg, NULL); break;
            case 'l': /* enumerate spu94_presets[0..9].name; exit(0); */ break;
            case 'h': print_help(); return 0;
            default:  fprintf(stderr, "spu94: error: try --help\n"); return 2;
        }
    }
    if (preset_name && config_path) {
        fprintf(stderr, "spu94: error: --preset and --config are mutually exclusive\n");
        return 2;
    }
    if (!preset_name && !config_path) {
        fprintf(stderr, "spu94: error: --preset or --config is required\n");
        return 2;
    }
    if (argc - optind != 2) {
        fprintf(stderr, "spu94: error: expected INPUT.wav OUTPUT.wav (got %d positional args)\n",
                argc - optind);
        return 2;
    }
    const char *in_path  = argv[optind];
    const char *out_path = argv[optind + 1];
    // ... dr_wav read, spu94_init / load_preset or config apply, spu94_process loop, flush, dr_wav write ...
    return 0;
}
```

**Source:** `[CITED: GNU libc manual — getopt_long]`, verified available on manylinux_2_28 + dev host via `/usr/include/getopt.h`.

**Why getopt_long:**
- Zero vendored dependency (libc).
- Supports `--long` options with values (`--preset hall` and `--preset=hall`).
- Handles `-h` short-option alias naturally.
- Matches convention of every standard Unix audio tool (`sox`, `ffmpeg`, `lame`).

### Pattern 8: JSON parsing for `--config` via jsmn (CLI-02, D-12..D-15)

**What:** Parse the `--config` JSON into a `jsmntok_t[]` array, walk the tokens looking for `"base"` (auto-detect override vs flat), validate register names against the reflected `spu94_reg_name` table, parse values as integers OR `"0x..."` hex strings, range-check.

**When to use:** `src/cli/main.c` when `--config <path.json>` is passed.

**Example:**

```c
// src/cli/json_config.c  (concept)
#define JSMN_STATIC
#define JSMN_HEADER
#include <jsmn.h>

// Parse a value that may be a JSON integer or a "0x.." / "-0x.." hex string.
// Returns 0 on success, -1 on parse error; out is int32_t so we can cover the
// full int16 ∪ uint16 domain.
static int parse_int_or_hex(const char *json, const jsmntok_t *t, int32_t *out) {
    int len = t->end - t->start;
    const char *s = json + t->start;
    char buf[32];
    if (len >= (int)sizeof(buf)) return -1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    char *endp;
    long v;
    if (t->type == JSMN_STRING) {
        // Hex string: "0x3F00" or "-0x8000"
        v = strtol(buf, &endp, 16);
    } else if (t->type == JSMN_PRIMITIVE) {
        v = strtol(buf, &endp, 10);
    } else {
        return -1;
    }
    if (*endp != '\0') return -1;
    *out = (int32_t)v;
    return 0;
}

// Walk tokens: if top-level has "base", treat as override; else flat-map.
// Apply to the spu94 state via spu94_set_reg_i16 / spu94_set_reg_u16 — these
// already enforce signedness policy per CONTEXT D-13.
```

**Source:** `[CITED: raw.githubusercontent.com/zserge/jsmn/master/jsmn.h]` — the `jsmntok_t` layout (type, start, end, size) is the only API surface; no allocations.

**Why jsmn:**
- 471 lines, MIT — smallest JSON library that handles the grammar.
- No allocations — caller provides `jsmntok_t[N]` array; jsmn fills it.
- Strict enough — rejects trailing commas, nested duplicates, unterminated strings.
- `JSMN_STATIC` + `JSMN_HEADER` + `#define JSMN_STATIC` in one TU keeps it out of the linker surface.

### Pattern 9: polished-tone README structure (DOCS-04, D-19, D-20)

**What:** 11 sections in the order locked by D-20. Hero paragraph → Status block → Quick install → Python walkthrough → CLI walkthrough → For the DSP-curious → Roadmap → Architecture overview → Licensing posture → Acknowledgments/bibliography → Contributing.

**When to use:** `README.md` at the repository root.

**Structural seeds by section:**

1. **Hero / pitch paragraph.** One paragraph, 3–4 sentences. First sentence names what SPU-94 is in plain terms; second says what makes it distinctive; third lists the primary ways a user interacts with it. Voice: confident, descriptive, product-doc. Reference tones: Valhalla DSP product pages, Eventide H-series manuals, Universal Audio plugin pages.

2. **Status block.** A dedicated section titled "Current state" or "Status" (planner picks). Lists what ships today, what's coming. Roadmap-style bullets, not disclaimers. Example voice: "Milestone 1 status (April 2026): the reverb network, the 4-tap comb / APF1 / APF2 filter chain, the 39-tap half-band FIR at both sample-rate boundaries, and all 10 factory presets are bit-tested against a 10⁶-step mid-stream-fuzz harness. Python bindings and the `spu94` CLI land in this release. Upcoming: witness-diff verification against lv2-psx-reverb; MCU cross-compile; JUCE plugin."

3. **Quick install.** `pip install spu94` for the wheel; `cmake --build build && ctest` for from-source.

4. **Python walkthrough.** Two small examples — raw-panel functions first, then class sugar. Both print "hello world" (load Hall, process a 1-second silent buffer, assert output is numerically distinct from zero after the FIR group delay settles, call destroy).

5. **CLI walkthrough.** `--preset hall in.wav out.wav`; `--config override.json in.wav out.wav`; `--list-presets`; `--help`. Include one sample override JSON block and explanation of the flat vs override shape.

6. **For the DSP-curious.** Four-paragraph deep dive. Paragraph seeds from CONTEXT's `<specifics>` section. Voice stays polished — technical depth delivered as engineering craft, not apologetics.

7. **Roadmap summary.** M1..M5 at a glance, matching `.planning/PROJECT.md`.

8. **Architecture overview.** ASCII signal-flow diagram (from CONTEXT specifics) OR short prose. Both are valid.

9. **Licensing posture.** `LICENSE` placeholder; MIT vs Apache-2.0 deferred to end of M1; nocash paraphrase-not-transcribe; dr_wav preservation; jsmn preservation; GPL witnesses named as witnesses.

10. **Acknowledgments / bibliography.** nocash psx-spx, Sony SDK documentation, hitmen c02 SPU docs, dr_wav, jsmn. Witness implementations (Mednafen, lv2-psx-reverb, DuckStation, MiSTer) named as behavioral witnesses, not source material.

11. **Contributing.** `ctest` invocation, `pytest tests/python/binding` invocation, what a good PR looks like (with/without ADR; tests required; DECISIONS.md updates for gray-area changes).

**Reference points:**
- **libsndfile README** `[CITED: github.com/libsndfile/libsndfile/blob/master/README.md]` — good example of a polished-tone audio-library README that mentions gotchas matter-of-factly without ever sounding apologetic.
- **rubberband README** `[CITED: github.com/breakfastquay/rubberband/blob/default/README.md]` — another well-balanced polished-tone example; documents technical depth in signal-flow terms.
- **Valhalla DSP plugin manuals** (paywalled, but mentally model the voice) — concise, confident, descriptive.

### Anti-Patterns to Avoid

- **Auto-converting numpy dtypes.** Silently converting `float32` → `int16` inside the binding would invent a conversion layer the hardware never had (D-11). The correct response to `float32` is `TypeError`, not coercion.
- **Hand-typed Python parallel lists of register names.** CONTEXT D-06 is clear: live library is authoritative. Do not maintain a `REG_NAMES = ["vLOUT", "vROUT", ...]` list in the Python source; reflect it at import time.
- **Generic bdist_wheel with `setuptools`.** Our Phase 1–5 build is CMake-based. Using setuptools to drive CMake via `setup.py` + `build_ext` override is a recognized pattern (joerick/python-ctypes-package-sample) but adds maintenance surface; scikit-build-core is the 2025 way.
- **Linking dr_wav into `libspu94.so`.** CLI-03 explicit. The CLI binary links dr_wav; `libspu94.so` does NOT. Verified per build via the CI assertion in Pitfalls §.
- **Shell-style CLI without getopt_long.** Hand-parsing `argv` is error-prone (doubled-dash handling, `=`-separated values, etc.). getopt_long is in libc; use it.
- **Embedding the compiled CLI binary inside the `.data/scripts/` directory of the wheel.** auditwheel handles this but via a shim pattern that adds complexity. Installing into the package dir (`python/spu94/spu94`) + entry-point shim (`python/spu94/cli.py`) is the cleaner path — the RPATH stays simple (`$ORIGIN` finds `libspu94.so` in the same dir) and the Python shim handles the PATH integration.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV file I/O | A custom PCM-parser reading the WAV format | **dr_wav.h** (public-domain / MIT-0, single-header) | WAV has trap doors: odd chunk layouts, extensible fmt, DS64, 24-bit oddities, broken files from consumer DAWs. dr_wav handles them with tested code. CLI-04 "one-line error" requires graceful rejection of malformed input. |
| JSON parsing for `--config` | A custom tokenizer + parser for the override/flat schema | **jsmn.h** (MIT, 471 lines, zero-alloc) | JSON has trap doors (escape sequences, number parsing, trailing commas, nested quoting). jsmn is small, vetted, and matches our needs exactly (callers own the token array; no allocations). |
| CLI argument parsing | Hand-parsing `argv` with `strcmp` | **getopt_long** (libc) | Doubled-dash handling, `--opt=val` vs `--opt val`, short-option merging — all handled correctly by getopt_long. |
| ctypes IntEnum from C enum | Hand-maintaining a Python list mirroring the C enum values | **Runtime reflection via `spu94_reg_name` + IntEnum functional API** | Drift is guaranteed across phases; the C side is authoritative. Phase 2 Plan 02 added `spu94_reg_name` + `spu94_reg_hw_offset` partly to enable this (STATE.md Phase 2 Plan 02 Decisions). |
| Strict numpy dtype/contig validation | Manually checking `arr.dtype == np.int16` + `arr.flags['C_CONTIGUOUS']` at every entry point | **`numpy.ctypeslib.ndpointer(dtype='i2', ndim=1, flags='C_CONTIGUOUS')`** | ndpointer bakes validation into the `argtypes` declaration; numpy raises `TypeError` with specific messages automatically. Zero-copy is automatic when the contract holds. |
| Linux wheel repair (external dependency bundling) | Hand-patching RPATH + copying `.so` dependencies into the wheel | **auditwheel** (invoked by cibuildwheel) | auditwheel is the official PyPA tool; handles RPATH rewrites, SONAME mangling, symbol-set verification against the manylinux policy. |
| Cross-platform Python C-API free wheel tagging | Hand-overriding `bdist_wheel.get_tag()` | **scikit-build-core `wheel.py-api = "py3"` + cibuildwheel `py3-none-{platform}` recognition** | Both tools coordinate to produce the correct `py3-none-manylinux_2_28_x86_64` tag. No setuptools override code. |
| Install `.so` into Python package dir from CMake | Hand-writing `install(CODE "...")` macros to `copy_file_to_platlib` | **`install(TARGETS ... LIBRARY DESTINATION "${SKBUILD_PROJECT_NAME}")`** under `if(DEFINED SKBUILD_PROJECT_NAME)` guard | Canonical scikit-build-core pattern; `SKBUILD_PROJECT_NAME` is always set by the backend during wheel build. |
| Entry-point shim | Hand-writing a bash script or Python wrapper that searches PATH | **`[project.scripts]` + `python/spu94/cli.py` with `os.execv`** | Standard Python pattern; pip handles the PATH integration via `bin/spu94` (linux) or `Scripts/spu94.exe` (Windows); shim is 5 lines. |

**Key insight:** Every one of the seven "don't hand-roll" items has a Phase-1-or-earlier precedent (Unity vendored, Python ctypes pattern used in fuzz scripts, scikit-build-core reserved in Phase 1 D-03). Phase 6's work is *connecting* already-vetted pieces, not inventing new machinery.

## Runtime State Inventory

Phase 6 is a packaging + binding phase, not a refactor or rename. There is no existing Python package being renamed; `python/spu94/` is currently empty (a `.gitkeep` placeholder from Phase 1 D-03). The fuzz scripts under `tests/python/` use hand-typed register IDs; Phase 6 migrates them to import from the new binding (D-16), which is a **code edit** — no runtime state to migrate.

The table below is populated for completeness, not because Phase 6 triggers refactor-style risks:

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — no databases or datastores involved in Phase 6. | None |
| Live service config | None — no external services, dashboards, or APIs involved. | None |
| OS-registered state | None — no Task Scheduler, launchd, systemd, or cron jobs. | None |
| Secrets / env vars | `SPU94_LIB` env var used by the four fuzz scripts (for dev convenience — points at a freshly built `.so`). Phase 6 retains the same env var in `__init__.py` so the existing scripts + the new binding read the same override. No secret rename. | None — pattern preserved. |
| Build artifacts | `build/src/spu94/libspu94.so` + `build/src/spu94/libspu94.a` (existing, unchanged). Phase 6 adds `build/src/cli/spu94` (the CLI binary) and, during wheel build, `wheelhouse/spu94-0.1.0-py3-none-manylinux_2_28_x86_64.whl`. | Standard CMake build artifacts; cleaned by `rm -rf build/` as always. |

**Nothing found in category:** Every category answered explicitly above. Phase 6 is a greenfield phase on top of the Phase 1–5 foundation; no runtime migration work.

## Common Pitfalls

### Pitfall 1: Wheel tag becomes `cp310-cp310-manylinux_2_28_x86_64` instead of `py3-none-manylinux_2_28_x86_64`

**What goes wrong:** The wheel is built with the default Python tag (cp310 / cp311 / cp312) instead of the universal `py3` tag. cibuildwheel rebuilds the same identical `.so` once per Python minor; the user gets 4 wheels of 4 identical bytes when they wanted 1.

**Why it happens:** Forgetting `wheel.py-api = "py3"` in `[tool.scikit-build]`. scikit-build-core defaults to CPython tags if not told otherwise.

**How to avoid:** Set `wheel.py-api = "py3"` in `pyproject.toml`. Verify with `unzip -l dist/*.whl | head -1` — the filename shows the tag.

**Warning signs:** `ls wheelhouse/` shows 3+ `.whl` files instead of 1; each wheel's `.dist-info/WHEEL` says `Tag: cp310-cp310-manylinux_...` instead of `Tag: py3-none-manylinux_...`.

### Pitfall 2: auditwheel tries to bundle `libc.so.6` and fails

**What goes wrong:** auditwheel repair panics because it can't locate a required symbol in the manylinux policy set — often because the build environment linked against a too-new libc symbol (e.g., glibc 2.34+ when targeting manylinux_2_28 which is glibc 2.28).

**Why it happens:** Building outside the manylinux Docker container. cibuildwheel runs the build INSIDE the container; local `pip install .` for dev does not.

**How to avoid:** Always run the distribution build via `cibuildwheel --only cp310-manylinux_x86_64`. Local `pip install -e .` produces a `linux_x86_64` wheel that's fine for dev but NOT uploadable to PyPI.

**Warning signs:** `auditwheel show` output says `The following external shared libraries are required by the wheel: libc.so.6 (newer than glibc_2.28)`. Fix: rebuild inside the container.

### Pitfall 3: libspu94.so accidentally links against dr_wav

**What goes wrong:** dr_wav is linked into `libspu94.so` in addition to the CLI binary. CLI-03 is violated — consumers who use just the Python binding get a useless dependency baked in, and (worse) the library now depends on a specific WAV format implementation that should be swappable at the CLI layer.

**Why it happens:** A copy-paste bug in the CLI `CMakeLists.txt`: `target_link_libraries(spu94_shared PRIVATE dr_wav)` instead of `target_link_libraries(spu94_cli PRIVATE ...)`.

**How to avoid:** A CI assertion after each build checks dr_wav's symbols are NOT in `libspu94.so`:

```bash
# scripts/ci/verify-no-drwav-in-libspu94.sh (Phase 6 adds this)
if nm -D build/src/spu94/libspu94.so | grep -qE 'drwav_(init|read|close|write)'; then
    echo "FAIL: dr_wav symbols detected in libspu94.so (CLI-03 violated)"
    exit 1
fi
```

Wire as a new CI job in `.github/workflows/ci.yml` alongside the existing `verify-no-heap-symbols.sh`.

**Warning signs:** `nm -D libspu94.so | grep drwav` returns any output.

### Pitfall 4: ctypes CDLL falls back to system library

**What goes wrong:** `ctypes.CDLL("libspu94.so")` — relative name — tries to find `libspu94.so` via the linker's search path. On a dev machine with a stale `/usr/lib/libspu94.so` (or a SONAME collision with another project), Python loads the wrong library silently.

**Why it happens:** Passing a non-absolute path to `ctypes.CDLL`.

**How to avoid:** Always use an absolute path. The `__init__.py` pattern from Pattern 3 above does this via `Path(__file__).parent / "libspu94.so"` (for installed wheels) or `os.environ["SPU94_LIB"]` (for dev).

**Warning signs:** The import-time drift assertion fires with "spu94 library mismatch" complaining about reg counts or state size that make no sense — because Python loaded a different library than you built.

### Pitfall 5: Silent numpy conversion via implicit astype

**What goes wrong:** A user passes `np.zeros(1024, dtype=np.float32)` to `spu94.process`. `ndpointer` rejects it with TypeError, but the user catches the TypeError and uses `arr.astype(np.int16)` — losing precision silently.

**Why it happens:** The error message doesn't steer the user toward the correct fix. "Array must have data type int16" is less helpful than "use `(arr * 32767).astype(np.int16)` before calling — float32 in [-1, 1] must be scaled to int16 range first."

**How to avoid:** Planner refines the error message wrapping per D-09 seeds:

```python
try:
    _lib.spu94_process(state, L_in, R_in, L_out, R_out, n)
except ctypes.ArgumentError as e:
    msg = str(e)
    if "int16" in msg:
        raise TypeError(
            "spu94.process requires int16 arrays (one sample per int16, "
            "range [-32768, 32767]). If your audio is float32 in [-1.0, 1.0], "
            "convert with: (arr * 32767).clip(-32768, 32767).astype(np.int16)."
        ) from None
    raise
```

**Warning signs:** User reports "the output sounds quiet / distorted after pre-processing."

### Pitfall 6: spu94_presets[] symbol not found via ctypes.in_dll

**What goes wrong:** `(_CPreset * 10).in_dll(_lib, "spu94_presets")` raises `AttributeError: symbol not found`.

**Why it happens:** Depending on the linker and flags, `spu94_presets` may not be exported as a D-type symbol. Phase 5 confirmed it IS (the user has seen the `D spu94_presets` in `nm -D` output), but a build-system regression (someone adding `-fvisibility=hidden` without an `__attribute__((visibility("default")))` annotation) would hide it.

**How to avoid:** Extend the Phase 5 Plan 04 `test_phase5_linksym.sh` CI gate to explicitly assert `nm -D build/src/spu94/libspu94.so | grep -qE 'D[[:space:]]+spu94_presets'`.

**Warning signs:** Phase 6 import fails with `AttributeError: .../libspu94.so: undefined symbol: spu94_presets`.

### Pitfall 7: Editable install (pip install -e .) requires separate CMake build

**What goes wrong:** `pip install -e .` with scikit-build-core is supported, but if you already have a stale `build/` directory from a prior `cmake -B build`, CMake's generator-expression `$<TARGET_FILE:spu94_shared>` in `tests/python/CMakeLists.txt` can point at the old build, not the one installed by scikit-build-core.

**Why it happens:** scikit-build-core's editable-install mode rebuilds only on demand; the `build/` directory scikit-build-core uses is different from the one `cmake -B build` uses.

**How to avoid:** Document in CONTRIBUTING that dev workflow is either "cmake -B build && ctest" (bypasses the Python binding; full C test suite) OR "pip install -e . && pytest tests/python/binding" (Python-binding-aware). Don't mix.

**Warning signs:** `ctest` passes but `pytest` imports a stale binding that missing a new symbol.

### Pitfall 8: jsmn / dr_wav header visibility leaks to library callers

**What goes wrong:** The CLI's `CMakeLists.txt` adds `target_include_directories(spu94_cli PUBLIC vendor/dr_wav)`. PUBLIC propagates to transitive dependencies — any target linking `spu94_cli` (which won't happen for a binary target, but...) or other targets that inherit headers from its propagation chain would see dr_wav.

**Why it happens:** Copy-paste from a library target.

**How to avoid:** Use `target_include_directories(spu94_cli PRIVATE vendor/dr_wav)` — PRIVATE does not propagate. Verified in the existing `src/spu94/CMakeLists.txt` which uses `target_link_libraries(spu94_obj PRIVATE spu94_warnings)` exactly for this reason.

**Warning signs:** Another target in `add_subdirectory(src/...)` suddenly sees `dr_wav.h` in its include path.

## Code Examples

See **Architecture Patterns** §1–§9 above — each pattern has a complete runnable code snippet drawn from Context7-equivalent (live raw GitHub) sources.

Consolidated quick reference:

- **`_binding.py` skeleton:** Pattern 4 (numpy ndpointer)
- **`__init__.py` skeleton:** Pattern 3 (runtime reflection IntEnum)
- **`presets.py` skeleton:** Pattern 5 (ctypes.Structure in_dll)
- **`cli.py` skeleton:** Pattern 6 (os.execv shim)
- **`src/cli/main.c` skeleton:** Pattern 7 (getopt_long)
- **JSON parser skeleton:** Pattern 8 (jsmn tokens)
- **`pyproject.toml`:** Pattern 2 (scikit-build-core + cibuildwheel combined)
- **`src/cli/CMakeLists.txt` + install rules:** Pattern 1 (SKBUILD_PROJECT_NAME install)

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `setup.py` + `bdist_wheel` override + `build_ext` subclass | `pyproject.toml` + `scikit-build-core` backend | 2023 onward (scikit-build-core 0.1 released Q1 2023; 0.10 gave stable py-api tagging) | Modern projects have no `setup.py`. All config in `pyproject.toml`. |
| `manylinux2014` (CentOS 7 base) | `manylinux_2_28` (AlmaLinux 8 base) | May 2025 (cibuildwheel default changed) | CentOS 7 EOL Jun 2024; manylinux2014 support is deprecated. |
| One wheel per CPython minor (cp310, cp311, cp312) | `py3-none-{platform}` for ctypes projects | cibuildwheel v2.6 (2023) | Pure-ctypes projects ship 1 wheel instead of 3-5. |
| `numpy.ctypeslib` `ndpointer` with weak validation | `ndpointer` with full dtype + flags + ndim enforcement | numpy 1.22+ (stable since 2022) | Empirically verified on numpy 2.2.4 in dev environment — still the current recommended path. |
| Bundling dr_wav into `libspu94.so` | Linking dr_wav into the CLI binary only | Never consider; CONTEXT D-03 + CLI-03 locked | Respects the "WAV I/O is an application concern, not a library concern" discipline. |

**Deprecated / outdated patterns to avoid:**
- `distutils` — removed in Python 3.12. Any build-system guide mentioning `distutils.command.build_ext` is pre-2021.
- `manylinux1` / `manylinux2010` — both EOL as of 2022 / 2024.
- `pkg_resources` / `entry_points.txt` with setuptools — replaced by `importlib.metadata` and `[project.scripts]`.
- `cibuildwheel` 1.x — we're on 3.x; the 1.x → 2.x migration was 2022; 2.x → 3.x was 2025.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `numpy.ctypeslib.ndpointer` error message shape is stable across numpy 1.23..2.4.x | Pattern 4 (numpy contract validator) | Low — empirically verified in this session on 2.2.4; messages have been stable since numpy 1.22. |
| A2 | cibuildwheel auditwheel step treats ELF binaries inside the package dir (not `.data/scripts/`) as regular files that get RPATH rewrites but not shimmed | Pattern 1 (CMake install pattern), Pitfalls §4 | Moderate — confirmed via `repair.py` source reading that the shim path (`_replace_elf_script_with_shim`) fires only for `.data/scripts/` paths, not arbitrary package-internal ELFs. Worst case if wrong: the first wheel build surfaces the issue immediately; falls back to `.data/scripts/` layout with the shim. |
| A3 | `jsmn` handles malformed / truncated JSON gracefully enough to produce a CLI-04 one-line error | Pattern 8 (JSON parser) | Low — jsmn returns `JSMN_ERROR_PART` / `JSMN_ERROR_INVAL` / `JSMN_ERROR_NOMEM`; the planner's CLI glue needs to map those three error codes to the one-line stderr contract. |
| A4 | `os.execv` works on all manylinux_2_28 hosts | Pattern 6 (entry-point shim) | Very low — `os.execv` is POSIX and has worked on Linux forever. |
| A5 | `ctypes.CDLL` on a `libspu94.so` in the package dir (absolute path via `pathlib.Path(__file__).parent`) is reliable across Linux distributions in the manylinux_2_28 baseline | Pattern 3 (reflection IntEnum) | Very low — this is exactly how every scikit-build-core-shipped ctypes package works (`[CITED: joerick/python-ctypes-package-sample, scikit-build-core examples]`). |
| A6 | `spu94_presets` is exported as a D-type symbol from `libspu94.so` (readable via `ctypes.in_dll`) | Pattern 5 (preset table import) | Low — empirically confirmed via `nm -D libspu94.so | grep spu94_presets` on the existing Phase 5 build. A Pitfall §6 CI assertion guards the regression path. |
| A7 | `wheel.py-api = "py3"` in scikit-build-core produces the `py3-none-{platform}` tag even when `build = ["cp310-manylinux_x86_64"]` is the only cibuildwheel build entry | Pattern 2 (pyproject.toml) | Moderate — the scikit-build-core docs say `py-api = "py3"` produces `py3-none-*` tags; cibuildwheel recognizes and deduplicates. First wheel build validates empirically. Fallback if wrong: use cibuildwheel's `--only cp310-manylinux_x86_64` with a `setuptools` style `bdist_wheel` override (the joerick pattern). |
| A8 | getopt_long is in the `manylinux_2_28` glibc baseline | Pattern 7 (CLI arg parsing) | Zero — getopt_long has been in glibc since the 1990s; it's in every manylinux image. |

**If this table is surprising to Anthony:** the moderate-risk items (A2, A7) are the ones the first real wheel build will exercise. All other assumptions are either empirically verified in this research session or drawn from current (2025-2026) official documentation.

## Open Questions (RESOLVED)

1. **Do we want `pytest` as the smoke test, or a minimal `python -c "import spu94; spu94.self_test()"`?**
   - What we know: CONTEXT specifics mention both options; planner's discretion.
   - What's unclear: Running `pytest` inside cibuildwheel's manylinux container requires copying tests via `test-sources`, adding ~30s per Python minor to CI runtime.
   - **RESOLVED:** Minimal smoke test for cibuildwheel (`python -c "import spu94; spu94.self_test()"`). Full `pytest` suite runs in the regular ci.yml workflow against `pip install -e .`. Best of both: smoke test validates the wheel works in an isolated manylinux env; full pytest validates behavior against the dev-built `libspu94.so`. Plan 04 implements this choice.

2. **Does `spu94.self_test()` belong in the public API or tucked in a private module?**
   - What we know: It's small (1-second buffer, load Hall, process, assert bounded output, destroy).
   - What's unclear: If it's a public function, users can call it for diagnostics. If private, it's only for CI.
   - **RESOLVED:** Public. A function that verifies "the wheel installed correctly" is a legitimate support tool. Document it in the README troubleshooting section. Plan 02 exposes `spu94.self_test()` as a public package-level function.

3. **Should the override JSON shape have a schema file (JSON Schema) shipped with the wheel?**
   - What we know: CONTEXT D-12..D-15 specifies the grammar inline.
   - What's unclear: Ship a `spu94-preset.schema.json` file? Nice-to-have for tooling integration.
   - **RESOLVED:** Out of scope for Phase 6. Defer unless a user asks. The CLI's error messages on unknown keys + type mismatches are the primary authority.

4. **Is a static-linked CLI option worth offering (e.g., `spu94_cli_static` target that links `libspu94.a`)?**
   - What we know: CMake currently produces both `libspu94.so` and `libspu94.a`.
   - What's unclear: A statically-linked CLI binary would be relocatable without the wheel — useful for a user who wants to drop the binary on a server and run it without Python at all.
   - **RESOLVED:** Out of scope for Phase 6. The wheel-installed CLI + `[project.scripts]` entry point already gives pip-only users the CLI. A standalone static binary is a distribution-channel decision, not a binding concern.

5. **cibuildwheel 3.x requires Python 3.11+ to RUN. The repo's dev host is 3.13 (fine), but CI docs may need a note.**
   - What we know: `[VERIFIED: pypi.org/pypi/cibuildwheel/3.4.1/json]` requires `>=3.11`.
   - What's unclear: Nothing — this is a pure build-tool dep. Users downloading the wheel only need Python 3.10+.
   - **RESOLVED:** Planner adds a CONTRIBUTING.md note: "cibuildwheel requires Python 3.11+; the build tool is used for CI only. End users need Python 3.10+ to use the wheel." (Plan 05 README/contributing.)

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Python 3 | Binding + CLI shim + build tools | ✓ | 3.13.7 (dev host); CI uses 3.10–3.14 | — |
| numpy | Binding import | ✓ | 2.4.4 (dev host); min required by pyproject: `>=1.23` | — |
| CMake | Build | ✓ | 3.31.6 (dev host); min required by CMakeLists: `>=3.20` | — |
| gcc (C11) | Core library + CLI | ✓ | gcc 15.2.0 (dev host) | clang (already in ci.yml matrix) |
| Docker | cibuildwheel (Linux wheel build in manylinux container) | ✓ | Docker 29.1.3 (dev host) | Podman (cibuildwheel supports via `CIBW_CONTAINER_ENGINE`) |
| glibc 2.28+ | manylinux_2_28 compatibility baseline | ✓ | glibc 2.42 (dev host — newer than baseline; cibuildwheel's container enforces 2.28) | — |
| getopt_long | CLI argument parsing | ✓ | In glibc 2.42 on dev host; in all manylinux_2_28 baseline images | — |
| scikit-build-core | Build backend | ✗ | — | `pip install scikit-build-core>=0.10` (add to dev instructions) — the package is only needed as a build-system requirement in `pyproject.toml`, so Phase 6 installs it implicitly. |
| cibuildwheel | Linux wheel distribution build | ✗ | — | `pipx install cibuildwheel` (add to CI + CONTRIBUTING) — Phase 6 Plan must include installation steps or a CI action snippet. |
| auditwheel | Invoked by cibuildwheel | ✗ | — | Installed automatically inside the manylinux container by cibuildwheel; no dev-host install needed. |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:** scikit-build-core and cibuildwheel both need to be added to the dev + CI workflow. Since both are well-established PyPA / pypa-adjacent tools with stable install-via-pip / pipx pathways, this is a standard "add to dev dependencies" task — not a blocker.

## Validation Architecture

> workflow.nyquist_validation is enabled in `.planning/config.json` — full section included.

### Test Framework

| Property | Value |
|----------|-------|
| Framework (C) | **Unity 2.5.2** (vendored at `tests/unit/unity/`; already wired across 23 C Unity TUs through Phase 5). `[VERIFIED: tests/ directory listing]` |
| Framework (Python) | **pytest** (new for Phase 6 binding unit tests) + the existing Phases 2/3/4/5 random-walk fuzz harnesses under `tests/python/` |
| Framework (CLI) | **Plain Python + subprocess** against the built CLI binary — one-shot behavior tests (`--preset hall in.wav out.wav` round-trip). Planner may use pytest for structure. |
| Framework (wheel) | **cibuildwheel's own `test-command`** runs the smoke test inside the manylinux container on every supported Python minor |
| Config file | `tests/CMakeLists.txt` (existing top-level ctest config) + new `tests/python/binding/CMakeLists.txt` + new `tests/cli/CMakeLists.txt` + `pyproject.toml` `[tool.cibuildwheel]` for the wheel-side smoke test |
| Quick run command | `ctest --test-dir build --output-on-failure -L binding` (ctest label filter for the Phase 6-owned tests; see below for label scheme) |
| Full suite command | `ctest --test-dir build --output-on-failure` (runs all Phases 1–6 tests together, confirming no regression in Phase 1-5 tests) |

### Phase Requirements → Test Map

Test topology: four layers. Each layer validates a subset of Phase 6 requirements; running the full suite confirms end-to-end.

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| **PYBIND-01** | Full C API callable via ctypes: `spu94_init` / `spu94_reset` / `spu94_destroy` / `spu94_tick` / `spu94_process` / `spu94_flush` / `spu94_load_preset` / `spu94_state_size` / `spu94_get_buffer_address` / `spu94_get_latency_samples` / `spu94_set_reg_*` / `spu94_get_reg_*` / `_pending` variants / `spu94_snapshot_registers` / `spu94_reg_name` / `spu94_reg_hw_offset` / `spu94_presets[]` all exposed through the binding and callable with their documented signatures | Unit (python/pytest) | `ctest -L binding`; exposed as `ctest -R test_binding_surface` | ❌ Wave 0 |
| **PYBIND-02** | `spu94.process` accepts int16 C-contiguous arrays; rejects `float32` with TypeError; rejects non-contiguous slice with TypeError; rejects mismatched lengths with ValueError; zero-copy verified (the C-side sees the numpy buffer unchanged — a 1-sample test writes a sentinel 0x1234 to L_in, calls process, then verifies the C-side READ via `snapshot_registers` or an output-side sentinel matches) | Unit (pytest) | `ctest -R test_binding_numpy_contract` | ❌ Wave 0 |
| **PYBIND-03** | `spu94.Register` is an IntEnum with exactly 35 members, names match `spu94_reg_name(i)` for every i, values match the C enum order (0..34) | Unit (pytest) | `ctest -R test_binding_register_intenum` | ❌ Wave 0 |
| **PYBIND-04** | `spu94.presets['hall']` and `spu94.presets[Preset.HALL]` both return the same PresetInfo; the reg values match `spu94_presets[SPU94_PRESET_HALL].regs` | Unit (pytest) | `ctest -R test_binding_preset_table` | ❌ Wave 0 |
| **PYBIND-05** | Import-time drift assertions fire with a `RuntimeError("spu94 library mismatch: ...")` message when `SPU94_STATE_SIZE_MAX` / register count / preset count are inconsistent with the live library (positive-case assertion — the import succeeds) | Unit (pytest) | `ctest -R test_binding_drift_detection` | ❌ Wave 0 |
| **PYBIND-06** | `pip install -e .` works; `python -c "import spu94; spu94.self_test()"` succeeds; `python -m build --wheel` produces a `py3-none-manylinux_2_28_x86_64.whl` (validated via cibuildwheel smoke test — unzip the wheel, read `WHEEL` metadata, assert the Tag line) | Integration (pytest + build tooling) | `bash scripts/ci/verify-wheel-tag.sh` (new Phase 6 CI script) | ❌ Wave 0 |
| **CLI-01** | `spu94 --preset hall in.wav out.wav` on a known 1-second stereo 44.1 kHz WAV produces a valid WAV file; output WAV is properly formatted; round-trip read→process→write preserves sample count + sample rate + channel count | Integration (pytest + subprocess) | `ctest -R test_cli_preset_hall_roundtrip` | ❌ Wave 0 |
| **CLI-02** | `spu94 --config flat_registermap.json in.wav out.wav` succeeds; `spu94 --config override_hall.json in.wav out.wav` succeeds; `spu94 --list-presets` prints the 10 preset names; `spu94 --help` exits 0 with usage text | Integration (pytest + subprocess) | `ctest -R test_cli_config_and_list` | ❌ Wave 0 |
| **CLI-03** | `nm -D build/src/spu94/libspu94.so` does NOT contain any `drwav_*` symbol; verifies dr_wav is CLI-only | Linker-symbol audit (bash) | `bash scripts/ci/verify-no-drwav-in-libspu94.sh` (new Phase 6 CI script) | ❌ Wave 0 |
| **CLI-04** | `spu94` with no args exits 2 and prints one-line stderr (no traceback); `--preset xxx` on bad preset exits 2 with `spu94: error: unknown preset 'xxx'`; `--config nonexistent.json` exits 2; `--config malformed.json` exits 2 with a one-line error; malformed WAV input exits 2 with a one-line error | Integration (pytest + subprocess) | `ctest -R test_cli_error_paths` | ❌ Wave 0 |
| **DOCS-04** | README is present; contains 11 sections in the D-20 order; includes `pip install spu94`, `cmake --build build`, both Python and CLI walkthrough examples, a status banner, and a licensing posture summary; fresh-clone-and-build-first-wave tests that an unfamiliar reader can run the first example (manual verification; no automated test but the README gets a `test-sources` copy in cibuildwheel so the test-command docs paths are stable) | Documentation presence check + manual | `bash scripts/ci/verify-readme-sections.sh` (new Phase 6 CI script — greps for section headings) | ❌ Wave 0 |

### ctest Label Scheme

Phase 6 adds these labels to `tests/CMakeLists.txt`:

- `binding` — Python binding unit tests (new `tests/python/binding/`)
- `cli` — CLI integration tests (new `tests/cli/`)
- `wheel` — Wheel + packaging tests (CI-only; not run under plain ctest)

Existing labels stay: `fuzz`, `fir`, `process`, `preset`, `rt_safety`.

### Sampling Rate

- **Per task commit (developer loop):** `ctest --test-dir build -L binding -L cli` (expected <10 s — pytest + subprocess bring a small fixed overhead; no fuzz harness).
- **Per wave merge:** `ctest --test-dir build --output-on-failure` (full suite — all 52+ ctest entries from Phases 1–5 + new Phase 6 entries; expected <15 min including the `fuzz_process` 10-minute target).
- **Phase gate (before `/gsd-verify-work`):** Full suite green + `bash scripts/ci/verify-wheel-tag.sh` + `bash scripts/ci/verify-no-drwav-in-libspu94.sh` + manual README first-wave walkthrough (reader unfamiliar with the project builds and renders `hall` preset from README alone).

### Wave 0 Gaps

Phase 6 introduces new test infrastructure — the binding layer and the CLI layer don't exist yet. Wave 0 creates:

- [ ] `tests/python/binding/` directory with `conftest.py` (shared fixtures: `spu94_lib_path`, `sample_wav_file`), `test_binding_surface.py`, `test_binding_numpy_contract.py`, `test_binding_register_intenum.py`, `test_binding_preset_table.py`, `test_binding_drift_detection.py`.
- [ ] `tests/cli/` directory with `test_cli_preset_hall_roundtrip.py`, `test_cli_config_and_list.py`, `test_cli_error_paths.py`, `conftest.py` with `spu94_cli_path` and `sample_wav_file` fixtures.
- [ ] `tests/fixtures/` — new 1-second 44.1 kHz stereo int16 WAV file generated at build time (deterministic; reproducible across hosts); one sample `override_hall.json` and one sample `flat_registermap.json`.
- [ ] `tests/python/binding/CMakeLists.txt` + `tests/cli/CMakeLists.txt` — add_test() wiring with `SPU94_LIB=$<TARGET_FILE:spu94_shared>` env-var pattern from Phase 2 Plan 05 (Pitfall 7 mitigation).
- [ ] `scripts/ci/verify-wheel-tag.sh` — parses a wheel's `.dist-info/WHEEL` file, asserts `Tag: py3-none-manylinux_2_28_x86_64`.
- [ ] `scripts/ci/verify-no-drwav-in-libspu94.sh` — `nm -D` grep for drwav symbols.
- [ ] `scripts/ci/verify-readme-sections.sh` — greps README for all 11 section headings in order.
- [ ] `pytest` added to dev requirements (currently Phases 2–5 use raw Python, not pytest).

*(The `fuzz_buffer.py` / `fuzz_reverb.py` / `fuzz_fir.py` / `fuzz_process.py` migration per D-16 is NOT in Wave 0 — it's structural work for a later wave since it doesn't need new test infrastructure, just editing existing test files.)*

### Validation Architecture Summary

Phase 6 is validated at **four distinct layers**, each with an independent failure mode:

1. **Binding layer (Python ctypes):** PYBIND-01..05. Tests: pytest under `tests/python/binding/`. Runs against a dev-built `libspu94.so` via `SPU94_LIB` env var. Catches argtypes / restype mismatches, dtype / layout violations, IntEnum drift.

2. **CLI layer (native C binary):** CLI-01..04. Tests: pytest + subprocess under `tests/cli/`. Catches dr_wav usage errors, JSON parsing regressions, argument-parser regressions, error-message regressions.

3. **Library purity layer (linker-symbol audits):** CLI-03 and D-08. Tests: bash scripts in `scripts/ci/`. `nm -D libspu94.so` must not contain dr_wav or jsmn symbols. `nm -D libspu94.so` must contain `spu94_presets` as a D symbol (Pitfall §6). Binary-level invariants, not behavior.

4. **Wheel / packaging layer:** PYBIND-06. Tests: cibuildwheel's own `test-command` + `scripts/ci/verify-wheel-tag.sh`. `python -c "import spu94; spu94.self_test()"` inside a manylinux_2_28 container. The wheel file's Tag line asserts `py3-none-manylinux_2_28_x86_64`.

Each layer fails independently — a drift in the binding surfaces in Layer 1 only; a dr_wav leak into `libspu94.so` surfaces in Layer 3 only; a wheel-tag regression surfaces in Layer 4 only. This matches the ADR-Phase-5-E methodology (per-axis CI gates preserved per-axis diagnosis).

## Security Domain

> `security_enforcement` is not set to `false` in `.planning/config.json` — absent key treated as enabled per spec. However, Phase 6 is a desktop DSP library + CLI that processes audio files under the end-user's own privileges; the attack surface is the JSON config parser + WAV parser (both caller-provided files the user already owns). No authentication / authorization / session management applies.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | — (no auth flow; user runs spu94 on their own files) |
| V3 Session Management | No | — (no sessions) |
| V4 Access Control | No | — (no multi-user model; user's own file permissions apply) |
| V5 Input Validation | **Yes** | (a) **JSON input (`--config`):** jsmn rejects malformed JSON; register-name lookup rejects unknown keys; value range-check rejects out-of-domain integers and hex strings (D-12..D-15). (b) **numpy input (process/flush):** `ndpointer` validates dtype, contig, ndim; planner adds cross-array length validation. (c) **WAV input:** dr_wav is the validation boundary; dr_wav errors map to CLI-04 one-line stderr. |
| V6 Cryptography | No | — (no crypto surface) |

### Known Threat Patterns for Phase 6 Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed WAV causes heap / stack corruption in dr_wav | Tampering | dr_wav is a hardened single-header library with extensive regression tests; we use the exact upstream source v0.14.6 (no patches). If a CVE is announced against dr_wav, the `vendor/dr_wav/dr_wav.h` file is a 1-file update. |
| Malformed JSON causes heap / stack corruption in jsmn | Tampering | jsmn is zero-allocation and operates on a caller-owned token array; the worst malformed JSON can cause is `JSMN_ERROR_INVAL` / `JSMN_ERROR_PART` / `JSMN_ERROR_NOMEM`. No heap surface. |
| Out-of-range integer in `--config` hex string causes UB in C cast | Tampering | Planner's JSON parser uses `strtol(buf, &endp, 16)` + explicit range check vs `INT16_MIN..INT16_MAX` (signed) or `0..0xFFFF` (unsigned) BEFORE casting to `int16_t` / `uint16_t`. |
| Symlink / path-traversal in `--config <path>` | Tampering | CLI opens the file with `fopen(path, "r")` under the user's own privileges — no attempt to chroot. Standard POSIX file-open semantics. Out of scope: if an attacker can write a JSON file to an untrusted path, the user has bigger problems than `spu94` reading it. |
| Pickle / code execution via `--config` | — | jsmn is a pure tokenizer; no code evaluation path. |
| DoS via gigantic JSON token count | Availability | jsmn token array is caller-sized (planner picks a static cap, e.g., 1024 tokens — enough for 35 reg keys + overrides + object structure + slack; rejects larger as `JSMN_ERROR_NOMEM`). |
| DoS via huge WAV file | Availability | dr_wav streams in chunks; the CLI processes `spu94_process` in blocks. Caller's disk/RAM bounds the damage; not our layer to enforce. |

**Not applicable to Phase 6:**
- SQL injection (no database)
- XSS / HTML injection (no web surface)
- SSRF (no network access)
- Supply-chain for JavaScript dependencies (no JS)

**Secrets handling:** The `SPU94_LIB` env var is a development convenience, not a secret. No API keys, tokens, passwords, or credentials in the Phase 6 code path.

## Sources

### Primary (HIGH confidence)

- **scikit-build-core docs (`[CITED: raw.githubusercontent.com/scikit-build/scikit-build-core/main/docs/configuration/index.md]`)** — `wheel.packages`, `wheel.py-api`, `wheel.install-dir`, `[tool.scikit-build]` structure.
- **scikit-build-core docs (`[CITED: raw.githubusercontent.com/scikit-build/scikit-build-core/main/docs/guide/cmakelists.md]`)** — `SKBUILD_PROJECT_NAME`, `SKBUILD_PLATLIB_DIR`, `SKBUILD_STATE`.
- **scikit-build-core docs (`[CITED: raw.githubusercontent.com/scikit-build/scikit-build-core/main/docs/guide/dynamic_link.md]`)** — `$ORIGIN` / `@loader_path` RPATH pattern, `install(TARGETS ...)` usage.
- **cibuildwheel docs (`[CITED: raw.githubusercontent.com/pypa/cibuildwheel/main/docs/options.md]`)** — `build` / `skip`, `manylinux-x86_64-image`, `archs`, `test-command`, `test-sources`, `test-skip`.
- **cibuildwheel changelog (`[CITED: raw.githubusercontent.com/pypa/cibuildwheel/main/docs/changelog.md]`)** — `py3-none-{platform}` tag support history, manylinux_2_28 default.
- **cibuildwheel resources (`[CITED: raw.githubusercontent.com/pypa/cibuildwheel/main/cibuildwheel/resources/pinned_docker_images.cfg]`)** — exact pinned manylinux image SHAs (currently `2026.04.08-5`).
- **PyPI metadata for scikit-build-core, cibuildwheel, numpy (`[VERIFIED: pypi.org/pypi/*/json]`)** — current versions, release dates, Python compatibility matrix.
- **dr_wav upstream (`[CITED: raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h + LICENSE]`)** — v0.14.6; dual public-domain / MIT-0.
- **jsmn upstream (`[CITED: raw.githubusercontent.com/zserge/jsmn/master/jsmn.h + LICENSE]`)** — MIT; 471 lines.
- **auditwheel source (`[CITED: raw.githubusercontent.com/pypa/auditwheel/main/src/auditwheel/repair.py]`)** — ELF scanning, RPATH rewrite, `_replace_elf_script_with_shim` behavior for `.data/scripts/`.
- **numpy.ctypeslib docs (`[CITED: raw.githubusercontent.com/numpy/numpy/main/doc/source/reference/routines.ctypeslib.rst]`)** — `ndpointer` signature.
- **Empirical verification on dev host (2026-04-21):** `numpy.ctypeslib.ndpointer` error messages on `float32` and non-contiguous arrays; `nm -D libspu94.so` symbol list; getopt_long header presence.

### Secondary (MEDIUM confidence)

- **joerick/python-ctypes-package-sample** `[CITED: github.com/joerick/python-ctypes-package-sample]` — the cibuildwheel-official ctypes reference project. Uses setuptools (we use scikit-build-core), but its `bdist_wheel.get_tag() = ("py3", "none", plat)` approach confirms the py3-none pattern is the canonical shape for ctypes projects.
- **Chris Krycho blog on Python enums + ctypes** `[CITED: v4.chriskrycho.com/2015/ctypes-structures-and-dll-exports.html]` — 2015; still-accurate pattern for `CtypesEnum(IntEnum)` + `from_param` classmethod. Useful context; we don't need the full pattern (our IntEnum values are consumed as raw ints in ctypes calls).

### Tertiary (LOW confidence — flagged for validation if used)

- None currently. All critical claims are verified against primary sources.

## Metadata

**Confidence breakdown:**

- Standard stack: **HIGH** — every tool version verified against live PyPI (2026-04-21); scikit-build-core, cibuildwheel, numpy releases all from March–April 2026.
- Architecture: **HIGH** — every pattern sourced from official docs; code snippets include source citations; patterns verified empirically where possible.
- Don't hand-roll: **HIGH** — every "use X" recommendation names a specific library with verified license and version.
- Common pitfalls: **HIGH** — Pitfalls 1–8 drawn from published issue trackers + source reading; each has a prevention strategy and a detection heuristic.
- Security domain: **MEDIUM** — no CVE lookup performed for dr_wav / jsmn v0.14.6 / 1.1.0; planner should run a CVE scan against the vendored versions before release.

**Research date:** 2026-04-21.
**Valid until:** ~2026-05-21 (30 days — ecosystem is stable but monthly cadence on cibuildwheel / scikit-build-core + active numpy 2.x series means a minor-version check before the Phase 7/8 phase-gate release is worthwhile).

---

*Phase: 06-python-binding-cli*
*Research gathered: 2026-04-21*
*Next step: `/gsd-plan-phase 6` consumes this RESEARCH.md + the 06-CONTEXT.md produced earlier. Planner likely shapes 5 plans: (1) ctypes `_binding.py` + runtime reflection + import-time asserts + pytest scaffolding; (2) raw-panel `api.py` + `SPU94` class + `presets.py` importer + `cli.py` entry-point shim; (3) native `spu94` CLI binary + dr_wav vendoring + jsmn vendoring + `--config` JSON parser + CLI integration tests; (4) `pyproject.toml` + scikit-build-core CMake install rules + cibuildwheel wiring + wheel-tag CI verification; (5) README (polished / 11 sections) + fuzz-script migration + ADR landings for D-01..D-25.*
