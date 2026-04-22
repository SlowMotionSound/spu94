---
phase: 06-python-binding-cli
plan: 04
subsystem: packaging
tags:
  - packaging
  - scikit-build-core
  - cibuildwheel
  - pyproject
  - wheel
  - manylinux

requires:
  - Plan 06-01 (ctypes binding foundation — `_binding.py`, Register/Preset
    IntEnums, `presets` accessor). Plan 4 extends `_binding.py` with a
    sys.path fallback so the same resolver works in both the wheel-install
    layout and the scikit-build-core editable-install layout.
  - Plan 06-02 (Python API + `cli.py` shim). Plan 4 extends `cli.py` the
    same way so the `[project.scripts]` entry finds the compiled binary
    whether it was dropped next to `__init__.py` (wheel) or into
    `site-packages/spu94/` (editable install).
  - Plan 06-03 (compiled `spu94` CLI binary + $ORIGIN RPATH + SKBUILD-
    gated `install(TARGETS spu94_cli RUNTIME ...)` rule in
    `src/cli/CMakeLists.txt`). Plan 4 adds the matching
    `install(TARGETS spu94_shared LIBRARY ...)` rule for libspu94.so.
  - `scikit-build-core>=0.10` available to pip (installed into the test
    venv on demand; not a dev-host prerequisite).

provides:
  - pyproject.toml — full build backend (scikit-build-core), project
    metadata, `[project.scripts] spu94 = "spu94.cli:main"`, cibuildwheel
    matrix pinned to `cp310-manylinux_x86_64` with manylinux_2_28 image
    and `wheel.py-api = "py3"` (produces py3-none-* tag for a single
    wheel that satisfies Python 3.10..3.14+).
  - src/spu94/CMakeLists.txt install rule for libspu94.so, guarded by
    `if(DEFINED SKBUILD_PROJECT_NAME)` so plain `cmake -B build` is
    unaffected.
  - scripts/ci/verify-wheel-tag.sh — permanent CI gate that parses
    .dist-info/WHEEL and asserts the Tag line begins with py3-none-;
    strict mode (SPU94_WHEEL_STRICT=1) requires the full
    manylinux_2_28_x86_64 suffix.
  - tests/packaging/ — ctest-labelled `packaging` suite exercising (a)
    pyproject.toml + SKBUILD rule presence, (b) end-to-end editable
    install + import + self_test + CLI, (c) wheel-tag auditor (good/bad
    tag cases + strict mode), (d) end-to-end `python -m build --wheel`
    including wheel-contents shape + $ORIGIN RPATH check.
  - `_binding.py` + `cli.py` library-resolution extension for the
    scikit-build-core editable-install layout (sys.path walk for
    `spu94/libspu94.so` / `spu94/spu94`).
  - .gitignore additions: `dist/`, `*.egg-info/`, `.pytest_cache/`,
    `_skbuild/`, `wheelhouse/`.

affects:
  - Plan 06-05 (README) can now quote `pip install spu94`,
    `pip install -e .`, and `spu94 --preset hall in.wav out.wav` as
    landed paths with the exact wheel filename
    (`spu94-0.1.0-py3-none-linux_x86_64.whl`) and the exact
    `py3-none-*` Tag shape.
  - Phase 7 verification harnesses consume the same wheel in their
    CI runs; the wheel-tag gate catches any future drift that would
    change wheel tag or package layout.
  - Milestone 1's license decision point (end of M1) inherits the
    `license = { file = "LICENSE" }` pointer already wired in
    pyproject.toml; flipping MIT ↔ Apache-2.0 is a LICENSE-file edit,
    not a pyproject change.

tech-stack:
  added:
    - scikit-build-core 0.12.2 (runtime of the build backend; pinned
      floor `>=0.10` in pyproject.toml's `[build-system].requires`).
    - cibuildwheel config (no new runtime; this is CI-side only).
    - ninja >= 1.5 as an implicit dev build-backend dep for the
      wheel-build smoke test (scikit-build-core picks ninja when it's
      available on the PATH).
  patterns:
    - SKBUILD_PROJECT_NAME-guarded `install()` rules in every CMake
      subdir — dev `cmake -B build` is a no-op for install rules;
      scikit-build-core-driven builds get the full wheel layout.
    - `wheel.py-api = "py3"` to produce one wheel per platform, not
      per Python minor — valid because SPU-94 is pure ctypes (no
      Python C API surface).
    - sys.path walk fallback for locating compiled artifacts in
      scikit-build-core editable installs — the source tree is on
      sys.path via .pth, but the compiled .so lands in
      site-packages/spu94/.
    - `.dist-info/WHEEL` Tag-line audit via `unzip -p ... | grep`;
      minimal-tooling CI gate that works in any manylinux container
      without bringing auditwheel or pip-tools into the critical path.

key-files:
  created:
    - pyproject.toml
    - scripts/ci/verify-wheel-tag.sh
    - tests/packaging/__init__.py
    - tests/packaging/CMakeLists.txt
    - tests/packaging/test_packaging_editable_install.py
    - tests/packaging/test_packaging_wheel_tag.py
  modified:
    - src/spu94/CMakeLists.txt (appended SKBUILD install rule)
    - tests/CMakeLists.txt (add_subdirectory(packaging))
    - python/spu94/_binding.py (Rule-1 lib-resolution extension)
    - python/spu94/cli.py (Rule-1 binary-resolution extension)
    - .gitignore (Python packaging artifacts)

decisions:
  - D-21 (manylinux_2_28): pinned via
    `manylinux-x86_64-image = "manylinux_2_28"` in `[tool.cibuildwheel]`.
  - D-22 (Python 3.10+): pinned via
    `requires-python = ">=3.10"` in `[project]`.
  - D-23 (one wheel per platform, pure ctypes): pinned via
    `wheel.py-api = "py3"` in `[tool.scikit-build]`. Produces
    `spu94-0.1.0-py3-none-linux_x86_64.whl` locally and
    `spu94-0.1.0-py3-none-manylinux_2_28_x86_64.whl` in the
    cibuildwheel container.
  - D-24 (wheel layout — libspu94.so + spu94 binary inside spu94/
    package dir): pinned via SKBUILD-guarded `install(TARGETS ...)`
    rules + `wheel.packages = ["python/spu94"]`.
  - D-25 (pyproject.toml holds all config): single-source-of-truth
    for build backend + project metadata + scripts + cibuildwheel
    matrix; no setup.py, no setup.cfg, no separate config file.
  - Auto-fix Rule 1: `_binding.py` and `cli.py` now walk sys.path
    for the installed .so / binary in addition to
    `Path(__file__).parent`. Without this, scikit-build-core's
    editable-install would 100% fail because it routes
    `spu94._binding` to `python/spu94/_binding.py` (source tree)
    but routes the compiled `libspu94.so` to `site-packages/spu94/`.
    The plan's stated `pip install -e .` contract requires this
    resolver extension. Scope: two small `_resolve_*` functions;
    no API change; no test breakage.
  - Auto-fix Rule 3: added `ninja>=1.5` to the wheel-build test's
    dependency-install list. scikit-build-core picks ninja by
    default on Linux; without it the wheel-build step in a fresh
    `--no-isolation` venv fails with `Missing dependencies: ninja>=1.5`.

metrics:
  duration: ~40 minutes (init + Task 1 + Task 2 + verification + SUMMARY)
  completed: 2026-04-21T22:31Z
---

# Phase 6 Plan 4: Python Packaging (scikit-build-core + cibuildwheel) Summary

Phase 6 Plan 4 turns the project into a pip-installable Python package:
`pyproject.toml` at the repo root drives scikit-build-core which drives
CMake, `pip install -e .` and `pip install dist/*.whl` both produce a
working `import spu94` + `spu94 --preset ... in.wav out.wav` experience,
and `scripts/ci/verify-wheel-tag.sh` is a permanent regression gate
against the D-23 `py3-none-*` wheel-tag contract. PYBIND-06 is green.

## Tasks Executed

| # | Name | Commit | Files |
|---|------|--------|-------|
| 1 | pyproject.toml + SKBUILD install rules + editable-install smoke test | `26c1d0d` | 9 (3 created, 5 modified + 1 lib-resolution fix) |
| 2 | verify-wheel-tag.sh + wheel-build smoke test | `f3f42b2` | 2 (both created) |

Both commits carry the `06-04` scope and used `--no-verify` per the
parallel-execution protocol.

## The Wheel SPU-94 Produces

**Local dev build** (`python -m build --wheel --no-isolation`):

| Property | Value |
|---|---|
| Filename | `spu94-0.1.0-py3-none-linux_x86_64.whl` |
| Size | 78 856 bytes (77 KB) |
| Tag (from .dist-info/WHEEL) | `py3-none-linux_x86_64` |
| Generator | `scikit-build-core 0.12.2` |

**Contents (`zipfile.ZipFile.namelist()` sorted):**

```
spu94-0.1.0.dist-info/METADATA
spu94-0.1.0.dist-info/RECORD
spu94-0.1.0.dist-info/WHEEL
spu94-0.1.0.dist-info/entry_points.txt
spu94-0.1.0.dist-info/licenses/LICENSE
spu94/.gitkeep
spu94/__init__.py
spu94/_binding.py
spu94/api.py
spu94/cli.py
spu94/libspu94.so
spu94/presets.py
spu94/reverb.py
spu94/spu94
```

- All 6 Python files from Plans 1 + 2 are present.
- `libspu94.so` (35 128 bytes) is next to `__init__.py`.
- `spu94` CLI binary (117 024 bytes, stripped) is next to the library;
  `readelf -d` on the binary reports `RUNPATH: [$ORIGIN]` — the binary
  will find its own `libspu94.so` at runtime with no `LD_LIBRARY_PATH`
  dance (T-06-25 satisfied).
- `.dist-info/entry_points.txt` contains `spu94 = spu94.cli:main`, so
  `pip install` registers the CLI shim on PATH.

The `spu94/.gitkeep` hitch-riding into the wheel is a zero-byte leftover
from pre-Plan-1 scaffolding under `python/spu94/`. Cosmetic, non-blocking.
Removal is out of scope for Plan 4 (the file was already present before
this plan touched the tree).

**cibuildwheel build** (manylinux_2_28 container, not exercised here —
requires Docker): the plan's `[tool.cibuildwheel]` section pins the
build identifier `cp310-manylinux_x86_64` with the `manylinux_2_28`
image. The container's auditwheel-repair step will rewrite the tag to
`py3-none-manylinux_2_28_x86_64`; `verify-wheel-tag.sh SPU94_WHEEL_STRICT=1`
is the strict-mode gate that enforces this. CI execution deferred to the
first CI run after Plan 5 lands the GitHub Actions workflow (or whatever
CI surface the project picks).

## End-to-End Proof

```
$ python3 -m venv /tmp/wheelvenv
$ /tmp/wheelvenv/bin/pip install --no-cache-dir \
      dist/spu94-0.1.0-py3-none-linux_x86_64.whl
...
Successfully installed numpy-2.4.4 spu94-0.1.0

$ /tmp/wheelvenv/bin/spu94 --list-presets
off
room
studio_a
studio_b
studio_c
hall
half_echo
space_echo
echo
delay

$ /tmp/wheelvenv/bin/python -c "import spu94; spu94.self_test(); print('self_test OK')"
self_test OK
```

Zero `SPU94_LIB` in the environment. Zero `LD_LIBRARY_PATH`. Zero extra
setup. This is PYBIND-06's must-have.

**Editable install** (`pip install -e .`) works the same:

```
$ /tmp/testvenv1/bin/pip install --no-cache-dir -e .
...
Successfully installed spu94-0.1.0

$ /tmp/testvenv1/bin/python -c "import spu94; print(spu94.SPU94_REG__COUNT)"
35

$ /tmp/testvenv1/bin/spu94 --list-presets
off
room
studio_a
...
```

The sys.path-walk fallback in `_binding.py` is what makes this work in
editable mode (see Deviations below).

## Wheel-Tag Gate Behavior

`scripts/ci/verify-wheel-tag.sh` has two modes:

| Mode | Trigger | Accepts | Rejects |
|---|---|---|---|
| Relaxed (default) | no env var | `py3-none-linux_x86_64` or `py3-none-manylinux_2_28_x86_64` | anything not starting `py3-none-` |
| Strict | `SPU94_WHEEL_STRICT=1` | `py3-none-manylinux_2_28_x86_64` only | everything else including relaxed-dev wheels |

Graceful error on missing wheel:

```
$ bash scripts/ci/verify-wheel-tag.sh /nonexistent.whl
FAIL: wheel not found: /nonexistent.whl
$ echo $?
1
```

Tested cases (all in `test_packaging_wheel_tag.py`):
- `cp310-cp310-linux_x86_64` (D-23 drift — Pitfall 1) → FAIL
- `py3-none-linux_x86_64` + relaxed → PASS
- `py3-none-linux_x86_64` + strict → FAIL (not manylinux_2_28)
- `py3-none-manylinux_2_28_x86_64` + strict → PASS
- the real dev-built wheel → PASS (relaxed)

## Test Coverage

**Packaging suite (`ctest -L packaging`):** 2 tests = 9 sub-tests total;
9/9 green.

| Test | Sub-tests | Coverage |
|---|---|---|
| `test_packaging_editable_install` | 4 | pyproject.toml field presence; SKBUILD guards in both CMakeLists; end-to-end `pip install -e .` in fresh venv → `import spu94` → `SPU94_REG__COUNT == 35` → `spu94.self_test()` → `spu94 --list-presets` reports 10 names |
| `test_packaging_wheel_tag` | 5 | script exists + executable; bad-tag rejected; good-tag accepted relaxed; strict mode gate; `python -m build --wheel` end-to-end + wheel contents + $ORIGIN RPATH |

**Full non-fuzz suite (`ctest -LE "fuzz"`):** 61/61 green, ~110 s wall
(rt_safety dominates at 101 s for 4 tests). Binding / CLI / FIR / preset /
process / packaging labels all green.

## Final Line Counts

| File | Lines |
|---|---|
| pyproject.toml | 82 |
| src/spu94/CMakeLists.txt (diff +8) | 48 (was 40) |
| scripts/ci/verify-wheel-tag.sh | 58 |
| tests/packaging/CMakeLists.txt | 25 |
| tests/packaging/test_packaging_editable_install.py | 112 |
| tests/packaging/test_packaging_wheel_tag.py | 163 |
| python/spu94/_binding.py (diff +43) | 230 (was 186) |
| python/spu94/cli.py (diff +24) | 80 (was 56) |
| .gitignore (diff +7) | 23 (was 15) |

All files well above their `min_lines` thresholds from the plan
(`pyproject.toml` 82 ≥ 60; `verify-wheel-tag.sh` 58 ≥ 30;
`test_packaging_editable_install.py` 112 ≥ 40;
`test_packaging_wheel_tag.py` 163 ≥ 50).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 — Bug] scikit-build-core editable install puts libspu94.so outside the Python source tree**

- **Found during:** Task 1 verification (first `ctest -R test_packaging_editable_install` run with the venv-install path enabled).
- **Issue:** The plan's acceptance criterion says `pip install -e .` must make `import spu94` work without `SPU94_LIB`. But scikit-build-core's editable install puts the Python sources on sys.path via a `.pth` file (so `import spu94` loads `python/spu94/__init__.py`) AND installs the compiled artifacts into `site-packages/spu94/libspu94.so`. The Plan-1 `_resolve_lib_path()` only checked `SPU94_LIB` env + `Path(__file__).parent/libspu94.so`, so in editable mode it tried `python/spu94/libspu94.so` which doesn't exist, and crashed with `OSError: cannot open shared object file`.
- **Fix:** Extended `_resolve_lib_path()` in `python/spu94/_binding.py` to also walk every `sys.path` entry looking for `{entry}/spu94/libspu94.so`. The wheel-install layout (library next to `__init__.py`) stays the first fallback, so regular `pip install` is unaffected; the sys.path walk only kicks in when the first fallback misses.
- **Files modified:** python/spu94/_binding.py
- **Mirror fix:** Same extension applied to `cli.py`'s binary-resolution logic via a new `_resolve_binary()` helper — so the pip-installed `spu94` command works in editable installs too.
- **Verification:** `/tmp/testvenv1/bin/python -c "import spu94; print(spu94.SPU94_REG__COUNT)"` prints `35` after `pip install -e .` with no `SPU94_LIB` set. `spu94 --list-presets` from the editable-install venv prints all 10 canonical names. Plan 1 + Plan 2's existing binding tests (5 tests, 38 sub-tests) all still pass — no regression, the sys.path walk only takes effect when the wheel-layout fallback misses.
- **Committed in:** 26c1d0d (Task 1).

**2. [Rule 3 — Blocking] Missing `ninja>=1.5` for wheel-build smoke test**

- **Found during:** Task 2 first run of `test_python_m_build_produces_wheel` — `python -m build` reported `Missing dependencies: ninja>=1.5`.
- **Issue:** scikit-build-core's default CMake generator on Linux is ninja; it expects ninja on the PATH at wheel-build time. The test's `pip install build scikit-build-core numpy cmake` didn't include ninja, and in a fresh venv the system ninja isn't visible.
- **Fix:** Added `"ninja>=1.5"` to the pip-install list in the test.
- **Files modified:** tests/packaging/test_packaging_wheel_tag.py
- **Verification:** Re-running the same ctest target passes end-to-end in ~14 s including venv bootstrap + pip install + wheel build + wheel audit.
- **Committed in:** f3f42b2 (Task 2).

**Total deviations:** 2 auto-fixed (1 Rule 1 lib-resolution bug, 1 Rule 3
blocking dep). **Impact on plan:** both fixes necessary for the plan's
stated acceptance criteria to pass. No scope creep. No architectural
changes. No new dependencies added to the shipped package — ninja is a
dev-only / CI-only dep, not in `[project.dependencies]`.

## Issues Encountered

None beyond the two auto-fixed deviations above. The plan's templates
for pyproject.toml, verify-wheel-tag.sh, and both tests compiled and ran
cleanly after those fixes.

## User Setup Required

None. The wheel builds on the dev workstation with the tooling it already
has (CMake 3.31.6, Python 3.13.7, numpy 2.4.4). Anthony's future flow:

```
pip install dist/spu94-0.1.0-py3-none-linux_x86_64.whl
spu94 --preset hall input.wav output.wav
```

— nothing else.

## CONTEXT Decisions Satisfied by This Plan

- **D-21** (manylinux_2_28): **fully satisfied.**
  `manylinux-x86_64-image = "manylinux_2_28"` in `[tool.cibuildwheel]`
  pins the container; `scripts/ci/verify-wheel-tag.sh` strict mode
  enforces the resulting tag.
- **D-22** (Python 3.10+): **fully satisfied.**
  `requires-python = ">=3.10"` in `[project]`; classifiers list
  3.10/3.11/3.12/3.13.
- **D-23** (one wheel per platform, py3-none tag): **fully satisfied.**
  `wheel.py-api = "py3"` + `build = ["cp310-manylinux_x86_64"]` +
  the verify-wheel-tag.sh gate pin this invariant three ways.
- **D-24** (wheel layout): **fully satisfied.**
  SKBUILD-guarded `install(TARGETS spu94_shared LIBRARY ...)` +
  `install(TARGETS spu94_cli RUNTIME ...)` + `wheel.packages =
  ["python/spu94"]` produce the expected spu94/ package dir with
  libspu94.so + spu94 binary + 6 Python files.
- **D-25** (pyproject.toml as single source of truth): **fully
  satisfied.** No setup.py, no setup.cfg, no separate config file.
  All build backend, project metadata, scripts, and CI matrix lives
  in one 82-line pyproject.toml.

## ADR Candidates (deferred to Plan 5)

Plan 5 (README + ADRs) will land formal ADR entries in
`docs/DECISIONS.md` for D-21..D-25 — the packaging decisions are
already fully coded in Plan 4; the ADRs record the "why" for posterity.

Candidates also worth recording:
- The sys.path-walk fallback pattern — it's a reusable approach for
  any ctypes binding that ships alongside scikit-build-core editable
  installs. Could fold into a "Phase 6 Library Resolution" ADR.
- The verify-wheel-tag.sh two-mode pattern (strict for CI, relaxed for
  dev) — useful template for any wheel-tag regression gate.

## Known Stubs

None. Every file in the plan's `files_modified` list ships its full
Plan 4 implementation. No TODO / FIXME / placeholder patterns in the
created or modified files.

## Threat Flags

None new. Plan 4's threat register (T-06-23..T-06-28) covers the
packaging boundary; all threats with `mitigate` disposition were
satisfied:

- **T-06-23** (wheel-tag drift): mitigated via
  `wheel.py-api = "py3"` + `verify-wheel-tag.sh` CI gate. The gate
  is a ctest target (`test_packaging_wheel_tag`) under the `packaging`
  label — no way to merge a wheel-shape regression without this test
  screaming.
- **T-06-24** (dr_wav / jsmn leaking into libspu94.so via build-system
  drift): mitigated by Plan 3's `verify-no-drwav-in-libspu94.sh`
  (already a permanent ctest gate under the `cli` label; runs in every
  CI sweep). Plan 4 doesn't introduce a parallel wheel-extracted
  check this round — the Plan-3 gate already runs on the same
  libspu94.so that goes into the wheel (scikit-build-core links the
  same `spu94_shared` target), so a leak would fire at Plan 3's gate
  before it could reach the wheel.
- **T-06-25** ($ORIGIN RPATH missing): mitigated via Plan 3's
  `INSTALL_RPATH "$ORIGIN"` on `spu94_cli` plus Plan 4's
  `test_python_m_build_produces_wheel` audit which runs `readelf -d`
  on the wheel-extracted binary and asserts `$ORIGIN` appears.
- **T-06-26** (install rules polluting plain cmake): mitigated by
  every install rule being inside `if(DEFINED SKBUILD_PROJECT_NAME)`.
  Plain `cmake -B build && cmake --install build` is a no-op (default
  prefix is `/usr/local` but the install rules never fire because
  `SKBUILD_PROJECT_NAME` isn't set). Verified by the clean
  `cmake -B build --fresh && cmake --build build` flow still working.
- **T-06-27** (auditwheel disk usage): accepted per plan; wheel is
  77 KB, well within container capacity.
- **T-06-28** (wheel metadata leak): accepted per plan; all project
  info in pyproject.toml is user-controlled and intentional.

## Deferred Items

None for Plan 4. Adjacent items that remain for Plan 5 or later:
- GitHub Actions (or other CI surface) workflow YAML that invokes
  `cibuildwheel --only cp310-manylinux_x86_64 .` and runs
  `verify-wheel-tag.sh SPU94_WHEEL_STRICT=1 wheelhouse/*.whl`. Plan
  4 provides the script; Plan 5 or a subsequent plan wires the
  workflow.
- Cleanup of the stray `python/spu94/.gitkeep` that hitches into the
  wheel. Cosmetic; not blocking.

## Self-Check: PASSED

Files claimed to exist and spot-checked on disk:

- `FOUND: pyproject.toml` (82 lines)
- `FOUND: scripts/ci/verify-wheel-tag.sh` (58 lines, +x)
- `FOUND: tests/packaging/__init__.py` (empty)
- `FOUND: tests/packaging/CMakeLists.txt` (25 lines)
- `FOUND: tests/packaging/test_packaging_editable_install.py` (112 lines)
- `FOUND: tests/packaging/test_packaging_wheel_tag.py` (163 lines)
- `FOUND: src/spu94/CMakeLists.txt` with `install(TARGETS spu94_shared`
  block under `if(DEFINED SKBUILD_PROJECT_NAME)`
- `FOUND: tests/CMakeLists.txt` contains `add_subdirectory(packaging)`
- `FOUND: python/spu94/_binding.py` includes `_resolve_lib_path`
  with sys.path walk
- `FOUND: python/spu94/cli.py` includes `_resolve_binary` helper
- `FOUND: .gitignore` contains `dist/`, `_skbuild/`, `wheelhouse/`

Commits claimed and verified via `git log --oneline`:

- `FOUND: 26c1d0d` (Task 1 — pyproject.toml + SKBUILD + editable test)
- `FOUND: f3f42b2` (Task 2 — verify-wheel-tag.sh + wheel-build test)

Verification commands (all green, exit 0):

- `cmake -B build --fresh && cmake --build build` — succeeds, install
  rules dormant (T-06-26 check).
- `ctest --test-dir build -L packaging` — 2/2 tests pass (9 sub-tests).
- `ctest --test-dir build -LE "fuzz"` — 61/61 green, ~110 s wall.
- `bash scripts/ci/verify-wheel-tag.sh dist/spu94-*.whl` — PASS.
- `python3 -m venv /tmp/wheelvenv && /tmp/wheelvenv/bin/pip install
  dist/spu94-*.whl && /tmp/wheelvenv/bin/spu94 --list-presets` — 10
  canonical names; `self_test()` green.
