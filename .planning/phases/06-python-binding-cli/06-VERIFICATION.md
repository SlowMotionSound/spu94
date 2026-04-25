---
phase: 06-python-binding-cli
verified: 2026-04-24T23:55:00Z
status: passed
score: 4/4 ROADMAP success criteria verified; 11/11 Phase 6 requirements satisfied
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: none
  gaps_closed: []
  gaps_remaining: []
  regressions: []
---

# Phase 6: Python Binding + CLI — Verification Report

**Phase Goal (ROADMAP.md):** Tests, gray-area exploration, and golden-file generation happen in Python + numpy; a user can render a WAV through any preset with a single CLI invocation.

**Verified:** 2026-04-24T23:55:00Z
**Status:** passed
**Re-verification:** No — initial verification (deferred during Phase 6 close-out, completed at M1 close-out per ARCHITECTURAL-AUDIT.md Step 14).

This report consolidates evidence already produced by `06-UAT.md`, `06-HUMAN-UAT.md`, `06-VALIDATION.md`, and the five plan summaries (`06-01..06-05-SUMMARY.md`), against the live post-remediation tree (Steps 1-13 of the M1 close-out have landed; ctest is 82/82 green).

---

## Goal Achievement

### Observable Truths (ROADMAP SC-1 through SC-4)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | SC-1: A Python user `import spu94`s, calls the full public C API via ctypes, passes numpy int16 arrays through `process_block` with zero-copy where possible, and Register IntEnum values match the C-side enum at import time (asserted via runtime size/layout check). | VERIFIED | `python/spu94/_binding.py` (ctypes proxy) + `python/spu94/api.py` (raw-panel) + `python/spu94/reverb.py` (SPU94 class). 35-entry `Register` IntEnum built at import time from `spu94_reg_name(i)` reflection (per ADR-Phase-6-A); 10-entry `Preset` IntEnum + `presets` accessor. `tests/python/binding/test_binding_drift_detection.py` asserts struct size + register count + preset count match the C side (3 import-time guard tests). `tests/python/binding/test_binding_register_intenum.py` enumerates all 35 names + offsets vs C reflection. `tests/python/binding/test_binding_numpy_contract.py` (post-Step-8 H-03 fix) verifies zero-copy ndpointer contract + class lifecycle including the new `test_raw_destroy_nulls_handle_and_idempotent` and `test_raw_destroy_then_set_reg_returns_invalid_state`. ctest label `binding`: 5/5 green. |
| 2 | SC-2: `spu94 --preset hall in.wav out.wav` and `spu94 --config preset.json in.wav out.wav` both succeed end-to-end using vendored dr_wav for I/O, producing correctly formatted output WAVs; errors exit non-zero with an actionable stderr message. | VERIFIED | `src/cli/main.c` + `src/cli/wav_io.c` + `src/cli/json_config.c` + `src/cli/preset_names.c`. Both `--preset` and `--config` (override-shape and flat-shape) end-to-end paths covered by `tests/cli/test_cli_preset_hall_roundtrip.py` + `test_cli_config_and_list.py`. The D-05 one-line-stderr contract is exercised by `test_cli_error_paths.py` (28 sub-tests post-Step-8: unknown preset, missing input/config, malformed JSON, mutually exclusive flags, wrong positional count, invalid + negative tail-seconds, the new H-04 over-long key gate, the H-05 flat-config missing-register gate, and the C-01 + C-02 + H-06 hardening sweep). `verify-no-drwav-in-libspu94.sh` regression gate confirms dr_wav stays inside the CLI binary. ctest label `cli`: 4/4 green. |
| 3 | SC-3: `pip install -e .` installs a Python wheel built via scikit-build-core, and a Linux wheel is producible via cibuildwheel. | VERIFIED | `pyproject.toml` declares `[build-system] requires = ["scikit-build-core>=0.10"]` + `[tool.scikit-build]` config + `[tool.cibuildwheel]` `manylinux_2_28` Linux target (per ADR-Phase-6-D). `CMakeLists.txt` install rules under the `SKBUILD` guard + `$ORIGIN` RPATH (per ADR-Phase-6-E). `tests/packaging/test_packaging_editable_install.py` exercises an actual `pip install -e .` flow; `test_packaging_wheel_tag.py` runs `verify-wheel-tag.sh` against a freshly-built wheel. ctest label `packaging`: 2/2 green (longest tests in the suite at 90 s + 126 s). |
| 4 | SC-4: `README.md` documents the build, a minimal Python and CLI usage example, the licensing posture summary, and the project-status banner; a reader unfamiliar with the project can build and run their first WAV render from it alone. | VERIFIED | `README.md` ships 11 sections (per ADR-Phase-6-F polished tone + D-19/D-20 scope decisions): hero / status / quick install / Python walkthrough / CLI walkthrough / "For the DSP-curious" / roadmap / architecture / licensing / bibliography / contributing. Structural gates mechanized in `tests/docs/test_readme_sections.py` + `scripts/ci/verify-readme-sections.sh` (10 section headings in order, 11 required content tokens, polished-tone invariants). README content polish itself was deferred to post-M4 per `feedback_readme_not_priority.md`; structural conformance — not prose quality — is what the verification gate covers, and that holds. The Phase 6 Human UAT walkthrough closed with this exact framing on 2026-04-22 (`06-HUMAN-UAT.md`). ctest label `docs`: 1/1 green. |

**Score:** 4/4 ROADMAP success criteria verified.

### Required Artifacts (per Phase 6 plans + ROADMAP)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `python/spu94/__init__.py` | Public package exports — Register, Preset, SPU94 class, presets accessor, raw api functions, version constants | VERIFIED | All 11 public names re-exported (Register, Preset, SPU94, presets, init/destroy/reset/process/flush/load_preset/load_register_map/snapshot, plus the SPU94_* constants). |
| `python/spu94/_binding.py` | Low-level ctypes proxy with argtypes/restype for every libspu94 export + import-time drift assertions | VERIFIED | Loads `libspu94.so` via ctypes; mirrors all 19 T-symbols (including the post-remediation `spu94_get_error_counters` and `spu94_preset_min_work_buf_size`); imports `SPU94_INVALID_STATE`, `SPU94_WORK_BUF_TOO_SMALL`, `SPU94_WORK_BUF_MAX_BYTES` from the C header at runtime; ADR-Phase-6-A documents the reflection approach. |
| `python/spu94/api.py` | Raw-panel public surface (state-handle-explicit) with strict numpy contract on process/flush + ADR-0022 work-buf default + post-Step-8 H-03 destroy nulling | VERIFIED | All 11 raw entry points (init/reset/destroy/tick/process/flush/load_preset/load_register_map/set_reg_i16/set_reg_u16/snapshot/get_reg_*/get_buffer_address/get_error_counters/self_test) present. Default `work_buf_size = SPU94_WORK_BUF_MAX_BYTES` per Step 5 (commit fdeeb57). Step 8 H-03 (commit c329649) nulls `state.value` after `_lib.spu94_destroy(state)`. |
| `python/spu94/reverb.py` | SPU94 class wrapping api.* with auto-dispatch set_reg/get_reg + context-manager + double-destroy guard | VERIFIED | `SPU94()` constructs with `work_buf_size` (defaults to `SPU94_WORK_BUF_MAX_BYTES`); `with SPU94() as rev:` context-manager works; `rev.state` raises after destroy; `set_reg(reg, value)` dispatches via `_reg_type` to typed setter; type-coercion via `_coerce_reg`. Tested in `test_binding_numpy_contract.py` (test_spu94_class_*). |
| `python/spu94/presets.py` | 10-entry preset table importable as Python data + name-keyed accessor | VERIFIED | `presets` module-level accessor returns a frozen tuple of 10 dicts shaped `{name, regs}`; sourced via ctypes `in_dll(_lib, "spu94_presets")` reflection (no hand-typing). `tests/python/binding/test_binding_preset_table.py` cross-checks every cell against `spu94_presets[i].regs[j]`. |
| `python/spu94/cli.py` | Thin Python entry-point shim that exec's the native `spu94` binary (so pip-installed users get the CLI) | VERIFIED | `pip install -e .` registers `spu94 = spu94.cli:main`; the shim resolves the binary inside the package dir and `os.execv`s it. Handled in `pyproject.toml` `[project.scripts]`. |
| `src/cli/main.c` | argv driver (getopt_long: --preset, --config, --tail-seconds, --list-presets, --help) + integer tail-seconds parser + post-Step-1 + Step-3 work-buf wiring + ADR-0022 error surfacing | VERIFIED | 357 lines; integer-only tail parser (line 57-95) at `SPU94_CLI_TAIL_MS_MAX = 600000` — H-06's strtol-whitespace bug fixed at Step 8 (commit c9154d8). spu94_load_preset error surfaced via three-way switch (lines 247-260) for SPU94_WORK_BUF_TOO_SMALL / SPU94_INVALID_ARG / SPU94_INVALID_STATE per ADR-0022. WORK_BUF_SIZE = 512 KB matches SPU94_WORK_BUF_MAX_BYTES. |
| `src/cli/wav_io.c` | dr_wav-backed planar L/R load + write with Step-8 C-01 bit-depth + PCM gates | VERIFIED | `spu94_cli_wav_load` validates channels==2, sampleRate==44100, bitsPerSample==16, translatedFormatTag==DR_WAVE_FORMAT_PCM (the last two added in Step 8 commit 3a6a9f5). 16-channel gate fires `ffmpeg -sample_fmt s16` hint. CLI-test fixture `_write_wav_fixture` exercises 8-bit and 24-bit rejection. |
| `src/cli/json_config.c` | jsmn-backed config loader with override-shape + flat-shape auto-detect + Step-8 H-04/H-05/H-06 hardening | VERIFIED | `spu94_cli_json_apply` handles auto-detect via "base" key sweep. Step 8 hardening: H-04 (commit 77197e3) distinct error for keys >= 64 chars; H-05 (commit fff3b73) pre-pass reports missing real register before fall-through; H-06 (commit c9154d8) hex-digit-required-after-0x in `parse_int_or_hex`. |
| `vendor/dr_wav/dr_wav.h` | Vendored single-header WAV codec, CLI-only link | VERIFIED | Located at `vendor/dr_wav/dr_wav.h`; included once via `DR_WAV_IMPLEMENTATION` in `wav_io.c`; `verify-no-drwav-in-libspu94.sh` regression gate confirms `libspu94.so` exports zero `drwav_*` or `jsmn_*` symbols. |
| `pyproject.toml` | scikit-build-core build-system + cibuildwheel manylinux_2_28 + project metadata | VERIFIED | `[build-system]` declares scikit-build-core>=0.10. `[project]` block lists Python 3.10+, dependencies (numpy>=1.20). `[tool.cibuildwheel]` configures manylinux_2_28 + auditwheel repair. `[project.scripts]` declares `spu94` entry-point. |
| `CMakeLists.txt` SKBUILD install | Wheel-only install rules (libspu94.so + spu94 binary inside `spu94/` package dir) + $ORIGIN RPATH | VERIFIED | `if(SKBUILD)` block installs both targets to `${SKBUILD_PROJECT_NAME}/`; RPATH set to `$ORIGIN` per ADR-Phase-6-E so the wheel's CLI binary loads `libspu94.so` from the same dir. |
| `README.md` | 11-section polished doc + project-status banner + licensing posture summary + bibliography pointer | VERIFIED | 401 lines (post-Phase-6); 10 section headings asserted in order by `tests/docs/test_readme_sections.py`. Polished-tone invariants enforced via prefix/suffix token list. `verify-readme-sections.sh` runs as ctest target `verify_readme_sections`. |
| `tests/cli/` (CLI integration suite) | 4 ctest targets (preset-roundtrip, config + list, error paths, no-drwav-in-libspu94) | VERIFIED | All 4 ctest targets registered with LABELS "cli". Post-Step-8: `test_cli_error_paths.py` grew 14 new sub-tests across C-01/C-02/H-04/H-05/H-06 attack vectors; total 28 sub-tests. ctest label `cli`: 4/4 green. |
| `tests/python/binding/` (5 binding tests) | drift-detection + register-intenum + preset-table + numpy-contract + surface | VERIFIED | All 5 ctest targets registered with LABELS "binding". Post-Step-8: numpy-contract grew the H-03 idempotent-destroy + invalid-state-after-destroy assertions. ctest label `binding`: 5/5 green. |
| `tests/packaging/` (2 packaging smokes) | editable-install + wheel-tag-validate | VERIFIED | Both ctest targets registered with LABELS "packaging". TIMEOUT 600 each. |
| `tests/docs/test_readme_sections.py` + `scripts/ci/verify-readme-sections.sh` | Mechanized README structural + token gates | VERIFIED | 10 section heading regex matches in order; 11 required content tokens present; polished-tone invariants pass. |
| `docs/DECISIONS.md` | ADR-Phase-6-A through F + ADR-Phase-6-G/H/I (post-Phase-6 hot fixes) + ADR-0022 + ADR-0023 + ADR-0024 | VERIFIED | `grep -cE "^## ADR-Phase-6-"` returns ≥9; ADR-0022 (work-buf contract), ADR-0023 (error counters), ADR-0024 (witness-diff thresholds) prepended at top of file. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `python/spu94/_binding.py` | `libspu94.so` | `ctypes.CDLL(libname, mode=RTLD_LOCAL)` | VERIFIED | Resolves via package dir first, falls back to system. argtypes/restype set on all 19 T-symbols. |
| `python/spu94/api.py` | `_binding._lib` | Per-function call site | VERIFIED | All raw-panel functions delegate to `_lib.spu94_*` with explicit type-coercion. |
| `python/spu94/reverb.py` | `python/spu94/api.py` | `from . import api` | VERIFIED | SPU94 class wraps api.* with state-property guard + auto-dispatch. |
| `python/spu94/presets.py` | `_binding._lib` | `ctypes.cast(in_dll(_lib, "spu94_presets"), ...)` | VERIFIED | Reflection — preset table is read at import time from the C side; no hand-typed mirror. Drift caught via import-time count assertion. |
| `python/spu94/cli.py` | native `spu94` binary | `os.execv(binary, sys.argv)` | VERIFIED | Binary located inside the package dir at install time (`SKBUILD` install + `$ORIGIN` RPATH). |
| `src/cli/main.c` | `libspu94.so` | direct C link to all public symbols | VERIFIED | `cmake --build` links against `spu94_shared`; `nm` confirms imports of spu94_init/load_preset/process/flush/destroy + the post-remediation get_error_counters / preset_min_work_buf_size. |
| `src/cli/wav_io.c` | `vendor/dr_wav/dr_wav.h` | `#define DR_WAV_IMPLEMENTATION` + `#include` | VERIFIED | Single expansion site in CLI TU; `verify-no-drwav-in-libspu94.sh` confirms `libspu94.so` is dr_wav-symbol-free. |
| `src/cli/json_config.c` | jsmn vendored header | `#include "jsmn.h"` | VERIFIED | jsmn vendored alongside dr_wav; CLI-only link verified by the same regression script. |
| `pyproject.toml` `[project.scripts]` | `python/spu94/cli.py:main` | scikit-build-core entry-point install | VERIFIED | `pip install -e .` registers the `spu94` console script. |
| `CMakeLists.txt` SKBUILD branch | wheel layout | `install(TARGETS spu94_shared DESTINATION ${SKBUILD_PROJECT_NAME})` | VERIFIED | Wheel package dir contains both `libspu94.so` and `spu94` binary; `$ORIGIN` RPATH wires the binary to the .so. |
| Post-remediation: `python/spu94/api.py::destroy` | C-side `SPU94_INVALID_STATE` guard | `state.value = None` after `_lib.spu94_destroy` (Step 8 H-03) | VERIFIED | New tests `test_raw_destroy_nulls_handle_and_idempotent` + `test_raw_destroy_then_set_reg_returns_invalid_state` confirm post-destroy mutation routes through Step 7's tightened SPU94_INVALID_STATE branch. |
| Post-remediation: `python/spu94/api.py::init` work_buf default | C-side ADR-0022 contract | `work_buf_size = SPU94_WORK_BUF_MAX_BYTES` (Step 5) | VERIFIED | The pre-remediation 8192-byte default tripped Hall-or-larger callers under load_preset; Step 5 closes the trap. ADR-Phase-6-H (master-send default relocated into preset tables — committed 2026-04-22 as part of close-out window) closes the related override-shape silent-output trap. |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Full ctest suite | `cd build && ctest -j` | 82/82 passed (post-Step-13; original Phase 6 close-out was 66/66 — the 16 new tests are the M1 close-out additions) | PASS |
| ctest label `binding` | `ctest -L binding` | 5/5 green | PASS |
| ctest label `cli` | `ctest -L cli` | 4/4 green | PASS |
| ctest label `packaging` | `ctest -L packaging` | 2/2 green | PASS |
| ctest label `docs` | `ctest -L docs` | 1/1 green | PASS |
| Hall preset CLI roundtrip | `spu94 --preset hall sample_1s.wav out.wav` then check WAV header | 16-bit PCM stereo 44.1 kHz; non-zero RMS in tail; covered by `test_cli_preset_hall_roundtrip.py` | PASS |
| 24-bit WAV rejected with ffmpeg hint | `spu94 --preset hall input_24bit.wav out.wav` | exit 2; stderr single line containing "24-bit" + "16-bit PCM required" + "ffmpeg" | PASS |
| Self-test from Python | `python3 -c "import spu94; spu94.api.self_test()"` | passes (>=100 non-zero output samples; `oob_tap_count == 0` per Step 6) | PASS |
| Editable install | `pip install -e .` then `python3 -c "import spu94; print(spu94.__version__)"` | covered by `test_packaging_editable_install.py` (~126 s wall) | PASS |
| Wheel tag check | scikit-build-core build then auditwheel repair → `manylinux_2_28_x86_64` tag | covered by `test_packaging_wheel_tag.py` | PASS |
| README structural integrity | `python3 -m pytest tests/docs/test_readme_sections.py` | passes (10 section headings in order, 11 required tokens, polished-tone invariants) | PASS |
| no-drwav-in-libspu94 regression gate | `bash scripts/ci/verify-no-drwav-in-libspu94.sh` | exit 0 (zero `drwav_*` or `jsmn_*` symbols in libspu94.so) | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| PYBIND-01 | 06-01, 06-02 | `import spu94` exposes the full public C API via ctypes | SATISFIED | `_binding.py` declares argtypes/restype for every libspu94 T-symbol; `api.py` wraps each. test_binding_surface asserts the export count. |
| PYBIND-02 | 06-02 | numpy int16 zero-copy contract on process / flush | SATISFIED | `ndpointer(dtype=int16, flags=("C_CONTIGUOUS",))` argtypes; ValueError/TypeError on contract violation; `test_binding_numpy_contract.py` covers shape/dtype/contiguity/None-substitution. |
| PYBIND-03 | 06-01 | Register IntEnum mirrors the C-side enum at import time + drift check | SATISFIED | `Register` IntEnum built from `spu94_reg_name(i)` reflection (35 names); `test_binding_drift_detection.py` asserts struct size + count. ADR-Phase-6-A. |
| PYBIND-04 | 06-01, 06-02 | Preset enum + accessor + name-keyed lookup | SATISFIED | `Preset` IntEnum (10 entries) + `presets` accessor (frozen tuple of 10 `{name, regs}` dicts); `test_binding_preset_table.py` cross-checks every cell. |
| PYBIND-05 | 06-01 | Import-time runtime asserts catch C/Python drift | SATISFIED | `_binding.py` import block asserts `spu94_state_size()` matches Python's `SPU94_STATE_SIZE_MAX`, register count == 35, preset count == 10. Drift fires `ImportError` early. |
| PYBIND-06 | 06-04 | Linux wheel build via scikit-build-core + cibuildwheel manylinux_2_28 | SATISFIED | `pyproject.toml` configures both; `tests/packaging/test_packaging_wheel_tag.py` builds + auditwheel-repairs and asserts the manylinux_2_28 tag; ADR-Phase-6-D documents the choice. |
| CLI-01 | 06-03 | Native `spu94` CLI accepts `--preset`, `--config`, `--tail-seconds`, `--list-presets`, `--help` | SATISFIED | `src/cli/main.c` getopt_long covers all five flags; `--help` text covered in `test_cli_config_and_list.py`; usage shape pinned in `test_cli_error_paths.py::test_no_args_fails`. |
| CLI-02 | 06-03 | `--config preset.json` works for both override-shape and flat-shape (35 register map) | SATISFIED | `src/cli/json_config.c::spu94_cli_json_apply` auto-detects via "base" key sweep; both shapes covered by `test_cli_config_and_list.py` + `test_cli_error_paths.py`. Step 8 added the H-04/H-05/H-06 hardening sweep. |
| CLI-03 | 06-03 | dr_wav stays vendored to the CLI binary; `libspu94.so` is dr_wav-free | SATISFIED | `verify-no-drwav-in-libspu94.sh` regression gate (ctest target `verify_no_drwav_in_libspu94`); zero drwav_*/jsmn_* T-symbols in libspu94.so confirmed. |
| CLI-04 | 06-03 | Every CLI error path exits non-zero with exactly ONE line of stderr starting `spu94: error:` | SATISFIED | `test_cli_error_paths.py` 28 sub-tests pin the D-05 contract: line-count == 1, prefix == `spu94: error:`, exit 2 for user errors. Step 8 grew this from 14 to 28 sub-tests. |
| DOCS-04 | 06-05 | README.md ships an 11-section polished walkthrough + project status + licensing posture + bibliography pointer | SATISFIED | 401-line README; structural gate via `verify-readme-sections.sh` + `test_readme_sections.py`; content polish itself deferred to post-M4 per `feedback_readme_not_priority.md` (an explicitly-recorded non-blocker for milestone close). |

**All 11 Phase 6 requirement IDs are satisfied.** ROADMAP maps these 11 to Phase 6; no orphan requirements detected.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | - | - | - | No blocker or warning anti-patterns found in Phase 6 source files post-remediation. |

**Scans performed:**
- `TODO`/`FIXME`/`XXX`/`HACK`/`PLACEHOLDER` on python/spu94/, src/cli/, pyproject.toml, README.md: clean (no matches).
- Empty-return / silent-degradation patterns in CLI: closed by Step 8 (C-01/C-02/H-04/H-05/H-06 sweep).
- ctypes destroy-then-use silent UB: closed by Step 8 H-03 (commit c329649).
- 8192-byte work-buf silent-degradation default: closed by Step 5 (commit fdeeb57) atop Step 3's load_preset gate (ADR-0022).
- README content polish: deferred to post-M4 per `feedback_readme_not_priority.md`; structural gate covers what verification needs.

### Human Verification Required

None remaining. The single human-UAT element (README walkthrough end-to-end) was completed on 2026-04-22 by Anthony per `06-HUMAN-UAT.md`. The 4 ROADMAP success criteria are all programmatically verifiable via existing ctest targets (`binding`, `cli`, `packaging`, `docs` labels). Phase 6's UI hint in ROADMAP referred to the README walkthrough, which is closed.

The post-Phase-6 close-out window (commits 53bac5c, 9650243, 2574c56..629222b, 9746fcd) and the M1 close-out remediation (commits 262930a, 4fcad49, 92cca7c, 72f2270, bce2c13, fdeeb57, efdf9a5, ce8385e, e81b360, 3a6a9f5, 0b48be8, c329649, 77197e3, fff3b73, c9154d8, 009b636, 06e5b40) all hardened — never weakened — Phase 6's evidence base; the same ctest targets that were green at Phase 6 close are green now (plus 16 new ones from the M1 close-out additions).

### Gaps Summary

**No gaps.** Phase 6 Success Criteria SC-1 through SC-4 are all TRUE; all 11 declared requirements (PYBIND-01..06, CLI-01..04, DOCS-04) are satisfied by landed code; all key wiring is verified end-to-end (ctypes → libspu94 → CLI binary → packaging → README); 82/82 ctest green including the post-remediation additions (witness_thresholds, test_process_external_anchor_off) layered on the Phase 6 baseline.

**Plan/Summary fidelity spot-checks:**
- Pre-remediation Phase 6 close-out reported 66/66 ctest green; post-remediation 82/82 — all 16 additions are net-positive (closed bugs the audit flagged + locked in regression gates + added an external anchor + added a witness-diff threshold gate). No prior-green test was broken.
- ADR-Phase-6-G (master-send default; closes the override-shape silent-output trap from the close-out window) is at line 33 cluster of `docs/DECISIONS.md` along with the M1 close-out ADRs (0022/0023/0024).
- README content polish is the only deferred item, and it is explicitly flagged as such by `feedback_readme_not_priority.md` — the structural gates (10 sections + 11 tokens + polished-tone invariants) still hold.

**Regression posture:** The full M1 close-out remediation cycle landed without breaking any existing test; in fact, two post-Phase-6 audits (Step 8's H-03 and H-06 in particular) caught real bugs the original Phase 6 close-out missed and closed them while strengthening the test surface. Phase 6's correctness claim is structurally stronger now than at the original close.

---

_Verified: 2026-04-24T23:55:00Z_
_Verifier: Claude Opus 4.7 (M1 close-out Step 14 — gsd-verifier-equivalent retroactive write)_
_Source evidence: 06-UAT.md, 06-HUMAN-UAT.md, 06-VALIDATION.md, 06-01..06-05-SUMMARY.md, plus all M1 close-out remediation commits 262930a..06e5b40._
