---
phase: 6
slug: python-binding-cli
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-21
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from `06-RESEARCH.md` § Validation Architecture.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework (C)** | Unity 2.5.2 (vendored at `tests/unit/unity/`, already wired through Phase 5) |
| **Framework (Python)** | pytest (new for Phase 6) + existing random-walk fuzz harnesses |
| **Framework (CLI)** | pytest + subprocess against the built `spu94` binary |
| **Framework (wheel)** | cibuildwheel `test-command` inside manylinux_2_28 container |
| **Config file** | `tests/CMakeLists.txt` + new `tests/python/binding/CMakeLists.txt` + new `tests/cli/CMakeLists.txt` + `pyproject.toml` `[tool.cibuildwheel]` |
| **Quick run command** | `ctest --test-dir build --output-on-failure -L "binding\|cli"` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | Quick: <10 s · Full: <15 min (includes the existing `fuzz_process` 10-minute target) |

---

## Sampling Rate

- **After every task commit:** `ctest --test-dir build -L "binding\|cli"` (expected <10 s — pytest + subprocess, no fuzz)
- **After every plan wave:** `ctest --test-dir build --output-on-failure` (full suite; regression-checks Phases 1–5)
- **Before `/gsd-verify-work`:** Full suite green + `bash scripts/ci/verify-wheel-tag.sh` + `bash scripts/ci/verify-no-drwav-in-libspu94.sh` + manual README first-wave walkthrough
- **Max feedback latency:** 10 s (quick loop); 15 min (full suite)

---

## Per-Task Verification Map

> Task IDs filled by the planner. Requirement → test-type mapping is pre-locked below (from RESEARCH.md § Validation Architecture). After `/gsd-plan-phase` lands plans, populate `Task ID` column from plan frontmatter and commit the update.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | PYBIND-01 | — | Full C API surface reachable via ctypes with correct argtypes/restype | unit (pytest) | `ctest -R test_binding_surface` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | PYBIND-02 | V5 Input Validation (numpy) | Rejects non-int16, non-contiguous, mismatched-length arrays with actionable messages; zero-copy on valid | unit (pytest) | `ctest -R test_binding_numpy_contract` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | PYBIND-03 | — | `spu94.Register` IntEnum has 35 members; names + values match live C library | unit (pytest) | `ctest -R test_binding_register_intenum` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | PYBIND-04 | — | `spu94.presets[name]` and `spu94.presets[Preset.ID]` both resolve to `spu94_presets[].regs` | unit (pytest) | `ctest -R test_binding_preset_table` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | PYBIND-05 | — | Import-time drift assertions raise `RuntimeError` on library-mismatch; pass when consistent | unit (pytest) | `ctest -R test_binding_drift_detection` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | PYBIND-06 | — | `pip install -e .` + `python -c "import spu94; spu94.self_test()"` succeed; wheel tag is `py3-none-manylinux_2_28_x86_64` | integration (pytest + build tooling) | `bash scripts/ci/verify-wheel-tag.sh` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | CLI-01 | V5 Input Validation (WAV) | `spu94 --preset hall in.wav out.wav` produces a valid WAV; sample count / rate / channels preserved | integration (pytest + subprocess) | `ctest -R test_cli_preset_hall_roundtrip` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | CLI-02 | V5 Input Validation (JSON) | Flat-map + override-shape `--config` both work; `--list-presets` prints 10 names; `--help` exits 0 | integration (pytest + subprocess) | `ctest -R test_cli_config_and_list` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | CLI-03 | — | `nm -D libspu94.so` contains zero `drwav_*` symbols (library purity audit) | linker-symbol audit (bash) | `bash scripts/ci/verify-no-drwav-in-libspu94.sh` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | CLI-04 | V5 Input Validation (argv + WAV + JSON) | All error paths exit non-zero with a one-line actionable stderr message (no tracebacks) | integration (pytest + subprocess) | `ctest -R test_cli_error_paths` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | DOCS-04 | — | README present; 11 sections in D-20 order; `pip install`, `cmake --build`, Python + CLI walkthroughs, status banner, licensing summary all present | doc presence + manual walkthrough | `bash scripts/ci/verify-readme-sections.sh` + manual fresh-clone render | ❌ Wave 0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

New test infrastructure — the binding layer and the CLI layer don't exist yet. Wave 0 installs:

- [ ] `tests/python/binding/` — `conftest.py` (`spu94_lib_path`, `sample_wav_file` fixtures), `test_binding_surface.py`, `test_binding_numpy_contract.py`, `test_binding_register_intenum.py`, `test_binding_preset_table.py`, `test_binding_drift_detection.py`
- [ ] `tests/cli/` — `conftest.py` (`spu94_cli_path`, `sample_wav_file` fixtures), `test_cli_preset_hall_roundtrip.py`, `test_cli_config_and_list.py`, `test_cli_error_paths.py`
- [ ] `tests/fixtures/` — deterministic 1-second 44.1 kHz stereo int16 WAV generated at build time; sample `override_hall.json` + `flat_registermap.json`
- [ ] `tests/python/binding/CMakeLists.txt` + `tests/cli/CMakeLists.txt` — `add_test()` wiring with `SPU94_LIB=$<TARGET_FILE:spu94_shared>` env-var pattern (Phase 2 Pitfall 7 mitigation)
- [ ] `scripts/ci/verify-wheel-tag.sh` — parses wheel `.dist-info/WHEEL` file, asserts `Tag: py3-none-manylinux_2_28_x86_64`
- [ ] `scripts/ci/verify-no-drwav-in-libspu94.sh` — `nm -D build/src/spu94/libspu94.so` grep for `drwav_*` → must be empty
- [ ] `scripts/ci/verify-readme-sections.sh` — greps README for all 11 D-20 section headings in order
- [ ] `pytest` added to dev requirements (Phases 2–5 used raw Python scripts; Phase 6 standardizes on pytest for the binding + CLI layer)
- [ ] New ctest labels registered: `binding`, `cli`, `wheel`

*Existing labels stay: `fuzz`, `fir`, `process`, `preset`, `rt_safety` (Phases 2–5 tests keep running under the full suite).*

*Fuzz-script migration (D-16) is NOT Wave 0 — it's structural work for a later wave (edits existing files, doesn't need new infrastructure).*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| README first-wave reader walkthrough | DOCS-04 | SC-4 explicitly requires "a reader unfamiliar with the project can build and run their first WAV render from README alone." Automated grep can't assert *comprehensibility*. | Fresh-clone the repo on a machine that's never seen it; follow only the README; produce a rendered WAV from the `--preset hall` example. Pass iff no external docs / source-code reading needed. |
| CLI error-message *tone* | CLI-04 | Automated tests assert "exits 2 with one-line stderr"; they do not assert the stderr reads like polished product copy vs apology. User is a recording engineer, not a coder — error-message tone matters. | Spot-check ~5 error paths (unknown preset, malformed JSON, bad WAV header, missing input file, zero-arg invocation). Each stderr line should read like engineered gear output ("spu94: error: unknown preset 'hll' — valid: off, room, ..."), not a developer traceback. |
| README DSP-curious section *accuracy* | DOCS-04 | Paraphrase-not-transcribe posture means the prose is freshly written; mechanical grep can't catch subtle factual drift from the specs/ADRs. | Cross-check each technical claim in the "For the DSP-curious" section against `docs/DECISIONS.md` ADR-0001..ADR-Phase-5-F. Any claim that's not traceable to an ADR or a cited source is either cited inline or removed. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (binding + CLI + CI scripts + fixtures)
- [ ] No watch-mode flags
- [ ] Feedback latency <10 s quick loop, <15 min full suite
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending (awaits planner output + first Wave 0 green run)
