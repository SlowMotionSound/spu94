---
status: complete
phase: 06-python-binding-cli
source:
  - 06-01-SUMMARY.md
  - 06-02-SUMMARY.md
  - 06-03-SUMMARY.md
  - 06-04-SUMMARY.md
  - 06-05-SUMMARY.md
started: 2026-04-22T20:25:00Z
updated: 2026-04-22T20:55:00Z
---

## Current Test

[testing complete — all tests passing after ADR-Phase-6-H fix]

## Tests

### 1. Cold-start smoke (clean build + full ctest)
expected: Fresh `cmake -S . -B build` from an empty build dir configures, `cmake --build` compiles everything, `ctest` runs green end-to-end across binding, CLI, packaging, rt-safety, docs, fuzz.
result: pass
evidence: |
  - `rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` configured cleanly.
  - `cmake --build build -j` built 100% of targets including `spu94_cli`.
  - `ctest --test-dir build --output-on-failure` → 66/66 tests passed (354.33s total, full suite incl. fuzz).
  - Labels: binding 5/5, cli 4/4, docs 1/1, fir 12/12, fuzz 2/2, packaging 2/2, preset 4/4, process 8/8, rt_safety 4/4.

### 2. CLI `--preset hall` renders audibly clean reverb
expected: `spu94 --preset hall input.wav output.wav` produces a wav file with non-zero audio, no startup burst, musical tail character.
result: pass
evidence: |
  - User confirmed via Audacity in previous session (commit `fe3673e`) on a real piano file.
  - Independent verification on `/tmp/spu94-test-input.wav`: peak=2679, 175290/176400 non-zero samples.
  - Behavior identical before and after ADR-Phase-6-H (DSP numerics unchanged; the amendment moved the master-send default from CLI layer to preset table — same register values hit the engine, same output).

### 3. Different presets produce audibly different output
expected: Same input through `hall`, `room`, `echo` produces three files with distinct reverb character.
result: pass
evidence: |
  - hall peak=2679, room peak=2318, echo peak=4031.
  - Cross-preset divergence: hall/room ≈175245 differing samples; hall/echo ≈175298; room/echo ≈175297.

### 4. `--config` flat register-map shape parses and renders
expected: `spu94 --config sample_flat_registermap.json input.wav output.wav` exits 0, writes a valid wav.
result: pass
evidence: |
  - Exit 0, valid 44.1k stereo wav.
  - Output silent because the fixture intentionally sets `vLOUT=0, vROUT=0`. By fixture design — the flat shape tests the parser across all 35 cells, not audibility. A user authoring a flat map with non-zero master send would get audio.

### 5. `--config` override shape produces audible output
expected: `spu94 --config '{"base": "hall", "overrides": {...}}' input.wav output.wav` renders audibly, matching `--preset hall` semantics for cells not overridden.
result: pass (after ADR-Phase-6-H fix)
evidence: |
  - Pre-fix: peak=0 (silent) — the original gap.
  - Post-fix: peak=8153, 175315 samples differ from plain `--preset hall`. Override's vIIR + mLCOMB1 changes take effect; master send inherited from hall's factory table (now 0x7FFF) makes the output audible.
  - Root cause closed: factory preset tables for 9 non-Off presets now carry `vLOUT`=`vROUT`=0x7FFF (previously 0x0000). All consumers of `spu94_load_preset` — `--preset`, `--config` override, `--config` flat (if non-zero), Python API, future M4 plugin, Phase 7 witness harnesses — inherit audible-by-default for non-Off presets.
  - `--preset off` still produces correct silence (peak=0). Off's table keeps `vLOUT`=`vROUT`=0.

## Summary

total: 5
passed: 5
issues: 0
pending: 0
skipped: 0

## Gaps

[none — Gap 1 closed by ADR-Phase-6-H, verified via live CLI repro]

## Fix Log

- **Gap 1 (Test 5) closed by ADR-Phase-6-H** — moved the non-Off master-send default from the CLI layer into the factory preset tables. Single commit covers:
  - `src/spu94/spu94_presets.c` — 9 non-Off presets get `vLOUT`=`vROUT`=0x7FFF; comment block updated.
  - `src/cli/main.c` — post-load `spu94_set_vLOUT/vROUT` block removed (now dead code).
  - `tests/python/fuzz_process.py` — HI-04 workaround removed (redundant after table change).
  - `tests/unit/process/test_process_block_size.c` — `fresh_state()` helper simplified (explicit master-send writes removed).
  - `docs/DECISIONS.md` — ADR-Phase-6-H prepended, amends ADR-Phase-6-G's CLI-layer-default bullet; preserves all other -G decisions.
  - Rebuild + full 66/66 ctest pass; live `--config override` repro now audible.
