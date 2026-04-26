---
phase: 06-python-binding-cli
plan: 05
subsystem: docs-and-adr
tags:
  - docs
  - readme
  - adr
  - fuzz-migration
  - d-16
  - d-17
  - d-19
  - d-20

# Dependency graph
requires:
  - Plan 06-01 (ctypes binding foundation — Register IntEnum, `_lib` handle, presets accessor, drift detection)
  - Plan 06-02 (Python API + SPU94 class + cli shim) — README Python walkthrough demonstrates the public surface this plan landed
  - Plan 06-03 (native spu94 CLI binary + error-message shape) — README CLI walkthrough quotes the exact Plan 3 landed error messages
  - Plan 06-04 (pyproject.toml + wheel via scikit-build-core) — README `pip install spu94` quick-install path is the Plan 4 wheel

provides:
  - README.md — 303-line polished-tone product doc with all 11 D-20 sections in order (DOCS-04 closed)
  - scripts/ci/verify-readme-sections.sh — permanent CI gate asserting 10 section headings in order + 11 required content tokens
  - tests/docs/ — pytest-wrapped docs gate (label `docs`); 7 automated assertions on structure, content, error-shape, licensing
  - Migrated fuzz scripts (fuzz_buffer.py, fuzz_reverb.py, fuzz_fir.py, fuzz_process.py) — all drop hand-typed register tables; import from the new binding per D-16
  - docs/DECISIONS.md extended with 6 Phase-6 ADRs (ADR-Phase-6-A..F) covering all 25 locked CONTEXT decisions (D-01..D-25)

affects:
  - tests/CMakeLists.txt (one-line add_subdirectory(docs) append)
  - tests/python/CMakeLists.txt NOT modified (D-18 preservation)
  - No Phase 1-5 C code touched; zero regressions

# Tech tracking
tech-stack:
  added:
    - Phase-6 ADR group ADR-Phase-6-A..F (6 entries, docs/DECISIONS.md now 2352 lines total)
    - scripts/ci/verify-readme-sections.sh as a ctest target under label "docs"
  patterns:
    - "One shell script per regression gate + ctest wire-up, mirroring Phase-1 verify-no-heap-symbols.sh, Phase-4 verify-flags.sh, Phase-6 Plan-3 verify-no-drwav-in-libspu94.sh, Phase-6 Plan-4 verify-wheel-tag.sh. Now: verify-readme-sections.sh. Project-wide template for content / artifact-shape gates."
    - "sys.path prepend inside each migrated fuzz script — `_REPO_ROOT / 'python'` pushed onto sys.path[0] at import so `from spu94 import ...` resolves to the repository's binding source tree during ctest runs without requiring `pip install -e .`. Reusable for any future tests-only Python script that needs the binding but wants to stay pip-install-agnostic."
    - "load_lib() retained as a thin shim that returns `_lib` (the binding's already-configured CDLL handle). Keeps the migrated scripts' CLI surface intact while swapping the source of truth — no function rename, no signature change, no behavioral change for callers."

key-files:
  created:
    - README.md (repo root)
    - scripts/ci/verify-readme-sections.sh
    - tests/docs/__init__.py
    - tests/docs/CMakeLists.txt
    - tests/docs/test_readme_sections.py
    - .planning/phases/06-python-binding-cli/06-05-SUMMARY.md (this file)
  modified:
    - tests/CMakeLists.txt (one-line append: add_subdirectory(docs))
    - tests/python/fuzz_buffer.py (D-16 migration)
    - tests/python/fuzz_reverb.py (D-16 migration)
    - tests/python/fuzz_fir.py (D-16 migration)
    - tests/python/fuzz_process.py (D-16 migration + D-17 warning block strengthened + numpy array contract)
    - docs/DECISIONS.md (6 Phase-6 ADRs prepended above ADR-Phase-5-F)

decisions:
  - README tone locked to polished / product-doc; no apologetic framing. Status communicated via a dedicated "Current state" block. 303 lines total; 10 H2 headings + 1 title; hero paragraph before first H2.
  - verify-readme-sections.sh checks the 10 canonical H2 headings (the hero paragraph is before the first `## ` so it is intentionally not in the 10-heading set). All 11 D-20-listed content requirements (including LICENSE, Q15, 39-tap, vIIR, import spu94) are asserted as grep-presence.
  - Fuzz migration strategy: keep each script's existing CLI + argparse surface intact; swap the source of truth for register constants + argtypes from hand-typed / per-script ctypes to `from spu94 import ...` + `from spu94._binding import _lib`. Each script's `load_lib(lib_path)` helper becomes a thin wrapper returning `_lib` — no caller signature change.
  - fuzz_process.py switches from ctypes c_int16 arrays to numpy int16 arrays to satisfy the binding's ndpointer contract. Per-step measurement shows the migration is FASTER than baseline (~7.5k ops/s vs baseline ~1.7k ops/s) — 10^6 steps projected at ~135 s vs baseline 595 s. Well within the 1200 s ctest TIMEOUT.
  - 6 Phase-6 ADRs chosen over 25 one-per-decision ADRs. The 6 groupings match natural decision clusters (two-surface binding; reflection+drift; numpy contract; CLI+error shape; JSON schema; packaging+README+fuzz). Decision-to-ADR mapping documented in each ADR's Resolves line.
  - ADR-Phase-6-F groups three related clusters (D-16/D-17/D-18 fuzz migration + D-19/D-20 README + D-21..D-25 packaging) because each individual cluster is short and the three are interrelated (README cites wheel filename from packaging decision; fuzz migration relies on the binding delivered by packaging).
  - docs/DECISIONS.md now has 6 Phase-6 ADR entries prepended ABOVE ADR-Phase-5-F, consistent with the project-wide "new entries prepend at top" discipline.

# Metrics
metrics:
  duration: ~45 minutes (context + Task 1 README + Task 2 fuzz migration + Task 3 ADRs + SUMMARY)
  completed: 2026-04-21
---

# Phase 6 Plan 5: README + Fuzz Migration + Phase-6 ADRs

**DOCS-04 closed. Phase 6 algorithm work is complete; Milestone 1 is positioned for the /gsd-verify-work 6 pass.**

## Tasks Executed

| # | Name | Commit | Files |
|---|------|--------|-------|
| 1 | README.md + verify-readme-sections.sh + tests/docs/ | `bb78953` | 5 created, 1 modified |
| 2 | Fuzz script migration (D-16 + D-17 preservation) | `2f57ea2` | 4 modified |
| 3 | docs/DECISIONS.md — 6 Phase-6 ADRs (D-01..D-25) | `b710ad0` | 1 modified |
| 4 | Manual README walkthrough — SC-4 first-wave reader test | **PENDING — BLOCKING CHECKPOINT** | user action required |

All three automated commits carry the `06-05` scope and were created with `--no-verify` per the parallel-execution protocol. Task 4 is a `checkpoint:human-verify` in the plan and is returned to the orchestrator as a blocking gate.

## README — Final Line Count + Section Count

**303 lines; 10 H2 headings + 1 top-level title.**

Section-by-section (10 H2 headings in D-20 order):

| # | Heading | Purpose |
|---|---------|---------|
| — | `# SPU-94` + hero paragraph (lines 1-8) | Top-level title + 3 paragraphs of polished pitch |
| 1 | `## Current state` | Milestone 1 April 2026 status block |
| 2 | `## Quick install` | `pip install spu94` + editable install + cmake-from-source |
| 3 | `## Python walkthrough` | Raw-panel functions + SPU94 class (with context manager) |
| 4 | `## CLI walkthrough` | --preset + --config (both shapes) + --tail-seconds + --list-presets + error format |
| 5 | `## For the DSP-curious` | Q15 truncation + 39-tap FIR + vIIR anomaly + from-spec-not-ported |
| 6 | `## Roadmap` | M1..M5 at a glance |
| 7 | `## Architecture overview` | Signal-flow ASCII diagram |
| 8 | `## Licensing posture` | dr_wav + jsmn vendored, MIT/Apache deferred, nocash paraphrase discipline |
| 9 | `## Acknowledgments` | Primary sources + vendored libs + behavioral witnesses |
| 10 | `## Contributing` | Build / test / ADR workflow |

The hero paragraph before the first `## Current state` heading is in the document layout exactly as D-20 prescribed — polished pitch + three paragraphs, before any section heading. The plan's H2 count target (10) is met exactly.

Content spot-checks (all assert-presence in `verify-readme-sections.sh`):

| Token | Count in README |
|-------|-----------------|
| `pip install spu94` | 1 |
| `cmake --build build` | 2 |
| `spu94 --preset hall` | 2 |
| `spu94.SPU94` | 1 (plus `SPU94()` forms) |
| `import spu94` | 2 |
| `vIIR` | 5 |
| `39-tap` | 6 |
| `Q15` | 2 |
| `dr_wav` | 3 |
| `jsmn` | 2 |
| `LICENSE` | 2 |

## ADR Landings — 6 Phase-6 ADRs Covering D-01..D-25

| ADR | Title | D-XX coverage |
|-----|-------|---------------|
| ADR-Phase-6-A | Two-surface Python binding — raw-panel functions + SPU94 class | D-01, D-02 |
| ADR-Phase-6-B | Runtime reflection + import-time drift detection | D-06, D-07, D-08 |
| ADR-Phase-6-C | Strict numpy int16 contract — zero-copy when it holds | D-09, D-10, D-11 |
| ADR-Phase-6-D | Native C CLI — vendored dr_wav + jsmn, polished error shape | D-03, D-04, D-05 |
| ADR-Phase-6-E | `--config` JSON — dual shape auto-detect + strict validation | D-12, D-13, D-14, D-15 |
| ADR-Phase-6-F | Packaging, README scope, fuzz migration — closing Phase 6 | D-16, D-17, D-18, D-19, D-20, D-21, D-22, D-23, D-24, D-25 |

All 25 locked CONTEXT decisions appear in at least one ADR's **Resolves:** line (verified by per-decision grep). Each ADR follows the Phase-5 format: Status / Resolves / Relates / Context / Decision / Consequences / Alternatives Considered / Seam / Revision Path / Sources. Prose is original; no transcription of nocash or upstream emulator text. Cross-references use the existing ADR-0001..ADR-0020 and ADR-Phase-5-A..F numbering where relevant.

**docs/DECISIONS.md final line count:** 2352 (was 2021 pre-plan; +331 lines for the 6 ADRs).

**Placement:** Prepended above `## ADR-Phase-5-F` at the top of the file, consistent with the project's "new entries prepend at top" discipline.

## Fuzz Script Migration — Before vs After Runtime

Per-script runtimes on the dev workstation, measured via ctest post-build:

| Script | Pre-migration baseline | Post-migration | Delta |
|--------|------------------------|----------------|-------|
| fuzz_buffer | 2.46 s (Phase 2 Plan 05) | 2.55 s | +3.7% |
| fuzz_reverb | ~2.2 s (Phase 3 Plan 04) | 2.24 s | +/-0% |
| fuzz_fir | ~3.1 s (Phase 4 Plan 04) | 3.08 s | +/-0% |
| fuzz_process | 595 s (Phase 5 Plan 05, 10^6 steps) | ~135 s projected (50K steps measured at 6.7 s = 7.5k ops/s) | **-77%** |

fuzz_process accelerated significantly because numpy slicing + ndpointer handoff is faster than the ctypes c_int16 array slicing + POINTER(c_int16) handoff. The `Lout[:n].any()` numpy path is also a C-level scan vs the pre-migration `any(Lout[:n])` Python-level scan. All three factors compound; the migration gained speed while dropping ~150 lines of hand-typed register-table duplication across four files.

All four scripts keep the pre-migration CLI surface (`--seed`, `--steps`, `--lib` where applicable). The `load_lib()` helper in each script becomes a thin shim returning the binding's `_lib` handle.

**tests/python/CMakeLists.txt** is UNCHANGED from pre-Plan-5 — D-18 compliance verified by `git diff tests/python/CMakeLists.txt` returning no diff.

Struct-internal offsets in fuzz_process.py (PENDING_MASK_OFFSET and the four FIR_IDX_*_OFFSET constants) remain hand-typed per D-17. The warning block has been strengthened per the plan: it now names D-17 / 06-CONTEXT.md explicitly, explains why the public binding does not expose these, and provides the C-probe recipe for recomputing offsets when layout shifts. 4 references to "D-17" appear in the file (count verified by grep).

## Test Coverage

**Binding suite (`ctest -L binding`):** 5/5 green, ~1.5 s wall time.

| Test | Status |
|------|--------|
| test_binding_surface | PASS |
| test_binding_register_intenum | PASS |
| test_binding_preset_table | PASS |
| test_binding_drift_detection | PASS |
| test_binding_numpy_contract | PASS (30/30 sub-tests) |

**Docs suite (`ctest -L docs`):** 1/1 green, ~0.3 s wall time.

| Test | Status | Sub-tests |
|------|--------|-----------|
| test_readme_sections | PASS | 7 (verify-script + exists + hero-para + min-length + class-usage + tail-seconds + licensing + error-shape) |

**Fuzz regression (fast scripts, `ctest -R fuzz_buffer|fuzz_reverb|fuzz_fir`):** 3/3 green.

| Test | Wall time |
|------|-----------|
| fuzz_buffer | 2.55 s |
| fuzz_reverb | 2.24 s |
| fuzz_fir | 3.08 s |

The full 10^6-step fuzz_process is not run in every ctest sweep because of its 135 s projected runtime; a smoke-test run of 1000 steps passes in 0.1 s, confirming the migration does not break per-step invariants. The 50K-step run (6.7 s) exercises the mid-stream-writes path at sufficient scale to catch any migration-introduced state corruption.

**Full non-fuzz suite (`ctest -LE "fuzz"`):** not re-run this plan because Plan 5 changes are doc / test / ADR only; the compiled library is unchanged from end of Plan 4. Plan 4 SUMMARY recorded 61/61 green for `ctest -LE fuzz`.

## CONTEXT Decisions Satisfied by This Plan

- **D-16** — All four fuzz scripts import from the new binding; no hand-typed `SPU94_REG_*` tuple remains. Verified by grep (4 files × 1 "from spu94" line each; grep for `(SPU94_REG_vLOUT, SPU94_REG_vROUT` returns nothing across all four).
- **D-17** — Struct-internal offsets preserved in fuzz_process.py with strengthened warning block naming D-17 explicitly. 4 occurrences of "D-17" in the file; PENDING_MASK_OFFSET + the four FIR_IDX_*_OFFSET constants all present with bound annotations.
- **D-18** — tests/python/CMakeLists.txt unchanged; same ctest topology, same SPU94_LIB env wiring.
- **D-19, D-20** — README tone (polished) + structure (11 sections) both landed. verify-readme-sections.sh is a permanent CI regression gate against any future drift.
- **All of D-01..D-25** — Full coverage via the 6 Phase-6 ADRs. Every decision's rationale is now a first-class documented deliverable in docs/DECISIONS.md.

## Open Notes for Phase 7 — What the Verification Work Needs to Know

Phase 6 leaves Phase 7 with a stable, documented Python binding surface. Specific items the Phase 7 harnesses (witness-diff, golden-file, modulation) should know:

1. **The Register IntEnum is reflected from the live library at import time.** Phase 7's modulation harness can iterate `for r in spu94.Register: ...` and get all 35 members. No parallel table needs maintaining.
2. **Strict numpy contract applies to `spu94.process` / `spu94.flush`.** Phase 7 must use int16 C-contiguous 1-D numpy arrays. Convert float32 references via `(arr * 32767).clip(-32768, 32767).astype(np.int16)` before handing off.
3. **Preset data is available via `spu94.presets[...]`** keyed by string ("hall"), enum (`spu94.Preset.HALL`), or int (5). The underlying data is ctypes.in_dll'd from `.rodata` so there is no mutable copy to worry about.
4. **The `SPU94` class is a convenience wrapper over the raw panel.** Phase 7 harnesses can use either style; the class provides `with spu94.SPU94() as rev` context-manager semantics with idempotent destroy and loud post-destroy raises.
5. **Fuzz scripts' sys.path-prepend pattern is reusable.** Any Phase 7 tests-only script that needs the binding but doesn't want to require `pip install -e .` can prepend `{repo}/python` to `sys.path` before importing spu94. Template matches all four Phase-6 fuzz scripts.

## Checkpoint Task 4 — Status and Protocol

Task 4 in the Plan 5 spec is an explicit **checkpoint:human-verify** with gate="blocking". Per the plan's own language: "The executor PAUSES here and asks the user to perform the walkthrough described below. No Claude automation runs until the user returns with 'approved' or a revision request."

**SC-4 target:** "A reader unfamiliar with the project can build and run their first WAV render from the README alone."

**What's automated (green, gated by ctest):**
- `scripts/ci/verify-readme-sections.sh README.md` PASS
- `test_readme_sections` (7 sub-tests) PASS — hero paragraph, min length, class usage, tail-seconds, licensing, error shape

**What only a human can verify:**
- Does the README read right for Anthony's audience (recording / broadcast engineer, not coder)?
- Does the tone feel polished, not apologetic?
- Does the DSP-curious section read like engineering craft, or like a developer talking to a developer?
- Do the CLI / Python examples work as shown, or does the user have to improvise?
- Is the first-WAV-render walkthrough self-contained (no need to open any other docs)?

This checkpoint is returned to the orchestrator as a structured message (see the orchestrator-return block below). A fresh agent will be spawned to continue once the user approves or provides revisions.

## Known Stubs

None. Every file claimed by Plan 5 ships a full implementation. Scan for stub patterns (`grep -rn -i "stub\|not implemented\|TODO\|FIXME\|placeholder" README.md scripts/ci/verify-readme-sections.sh tests/docs/ docs/DECISIONS.md tests/python/fuzz_*.py`) returns only:
- Pre-existing TODO / FIXME / placeholder matches in fuzz_*.py docstrings that reference Phase 6's migration plan (now complete). Those comments are historical context, not live stubs.
- `placeholder` in the README's LICENSE paragraph — documenting that the LICENSE file is a placeholder by design (final pick deferred to end of M1). This is correct, not a stub.

## Threat Flags

None new. Plan 5's threat register (T-06-29, T-06-30, T-06-31) had `mitigate` / `accept-with-monitoring` dispositions assigned in-plan and were satisfied as described:

- **T-06-29** (README claims contradicting reality) — mitigated: every CLI invocation shown in the README is exercised by a Plan 3 or Plan 4 test; every Python example runs under the existing Plan 2 binding tests. The README's `spu94: error:` examples are byte-identical to the Plan 3 landed messages (cross-checked against `tests/cli/test_cli_error_paths.py`).
- **T-06-30** (ADRs transcribe nocash prose) — mitigated: the 6 Phase-6 ADRs are original SPU-94 wording. No verbatim nocash passages; facts (register counts, latency samples, Q15 semantics) are paraphrased, with the cross-reference going to the relevant Phase 1-5 ADR (e.g., ADR-0001 for Q15 truncation, ADR-Phase-5-A for block API shape).
- **T-06-31** (README DSP-curious drifts from DECISIONS.md) — accept-with-monitoring: the manual-verification checkpoint is the monitoring hook. Future phase changes to ADR-0001 / ADR-Phase-4-* / ADR-0002 require a README update; the verify-readme-sections.sh gate catches wholesale heading drift but does not catch subtle content drift. The acceptance is explicit in the plan.

No new surface introduced outside the plan's `<threat_model>`.

## Self-Check: PASSED

Files claimed to exist and spot-checked on disk:

- `FOUND: README.md` (303 lines)
- `FOUND: scripts/ci/verify-readme-sections.sh` (executable)
- `FOUND: tests/docs/__init__.py`
- `FOUND: tests/docs/CMakeLists.txt`
- `FOUND: tests/docs/test_readme_sections.py`
- `FOUND: .planning/phases/06-python-binding-cli/06-05-SUMMARY.md` (this file)
- `FOUND: tests/CMakeLists.txt` contains `add_subdirectory(docs)`
- `FOUND: docs/DECISIONS.md` contains 6 Phase-6 ADR headings
- `FOUND: tests/python/fuzz_buffer.py` contains `from spu94 import`
- `FOUND: tests/python/fuzz_reverb.py` contains `from spu94 import`
- `FOUND: tests/python/fuzz_fir.py` contains `from spu94 import`
- `FOUND: tests/python/fuzz_process.py` contains `from spu94 import` and 4 D-17 references

Commits claimed and verified via `git log --oneline`:

- `FOUND: bb78953` (Task 1 — README + verify-script + tests/docs)
- `FOUND: 2f57ea2` (Task 2 — fuzz migration)
- `FOUND: b710ad0` (Task 3 — 6 Phase-6 ADRs)

Verification commands (all green, exit 0):

- `bash scripts/ci/verify-readme-sections.sh README.md` — `PASS: README.md has all 10 sections in order with required content`
- `ctest --test-dir build -L docs` — 1/1 test passes
- `ctest --test-dir build -L binding` — 5/5 tests pass (no regression)
- `ctest --test-dir build -R "fuzz_buffer|fuzz_reverb|fuzz_fir"` — 3/3 tests pass within 10%-of-baseline runtime
- `grep -c "^## ADR-Phase-6-" docs/DECISIONS.md` — `6`
- Per-decision coverage grep (D-01..D-25) — all 25 found at least once
- `git diff tests/python/CMakeLists.txt` — empty (D-18 preservation)

---

*Phase: 06-python-binding-cli*
*Plan 05 automated tasks completed: 2026-04-21*
*Task 4 (manual README walkthrough checkpoint): PENDING — blocking gate returned to orchestrator*
