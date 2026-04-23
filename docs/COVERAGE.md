# SPU-94 — Spec Conformance Coverage

Pinned spec reference: https://web.archive.org/web/20260114082525/https://psx-spx.consoledev.net/soundprocessingunitspu/

This file is CI-enforced by `scripts/ci/check_coverage.py` (D-02). Every row's
`test:` field names a test that must exist and pass. Adding a new row without
a corresponding test fails the build. Regeneration or substantive edits land
via an ADR in `docs/DECISIONS.md` (D-01).

**Test-reference format.** Each `test:` cell is backticked and contains
`<source-path>::<ctest-name>` — the first half is the file on disk the
validator `stat`s, the second half is the ctest registration name the
validator invokes via `ctest -R "^<name>$"` (for `.c` paths) or `pytest`
(for `.py` paths). Empty `test:` cells are permitted only inside the
`## Known Gaps` section at the bottom of this file.

## Per-Register Coverage

Rows appear in `spu94_reg_t` enum order (`include/spu94/spu94_registers.h`).
All 35 registers are exercised end-to-end by `fuzz_process` (ctest #50) —
10⁶ random-walk steps of interleaved `spu94_process` + register writes +
`spu94_load_preset` with an independent Python reference model. The per-row
citation below points at the narrower ctest that touches the register
directly; `fuzz_process` is the wide-net backstop for all 35.

| Register | Type | test: | Notes |
|----------|------|-------|-------|
| `vLOUT`  | I16 | `tests/unit/registers/test_register_roundtrip.c::register_roundtrip` | Output L gain |
| `vROUT`  | I16 | `tests/unit/registers/test_register_roundtrip.c::register_roundtrip` | Output R gain |
| `mBASE`  | U16 | `tests/unit/buffer/test_buffer_mbase.c::buffer_mbase` | ADR-0006 snap-on-write |
| `dAPF1`  | U16 | `tests/unit/reverb/test_reverb_apf1.c::reverb_apf1` | APF1 delay |
| `dAPF2`  | U16 | `tests/unit/reverb/test_reverb_apf2.c::reverb_apf2` | APF2 delay |
| `vIIR`   | I16 | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | ADR-0002 0x8000 anomaly |
| `vCOMB1` | I16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | Comb tap 1 gain |
| `vCOMB2` | I16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | Comb tap 2 gain |
| `vCOMB3` | I16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | Comb tap 3 gain |
| `vCOMB4` | I16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | Comb tap 4 gain |
| `vWALL`  | I16 | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | Wall reflectance coefficient |
| `vAPF1`  | I16 | `tests/unit/reverb/test_reverb_apf1.c::reverb_apf1` | APF1 coefficient |
| `vAPF2`  | I16 | `tests/unit/reverb/test_reverb_apf2.c::reverb_apf2` | APF2 coefficient |
| `mLSAME` | U16 | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | L SAME store addr |
| `mRSAME` | U16 | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | R SAME store addr |
| `mLCOMB1`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | L comb tap 1 addr |
| `mRCOMB1`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | R comb tap 1 addr |
| `mLCOMB2`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | L comb tap 2 addr |
| `mRCOMB2`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | R comb tap 2 addr |
| `dLSAME` | U16 | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | L SAME feedback addr |
| `dRSAME` | U16 | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | R SAME feedback addr |
| `mLDIFF` | U16 | `tests/unit/reverb/test_reverb_diff_iir.c::reverb_diff_iir` | L DIFF store addr |
| `mRDIFF` | U16 | `tests/unit/reverb/test_reverb_diff_iir.c::reverb_diff_iir` | R DIFF store addr |
| `mLCOMB3`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | L comb tap 3 addr |
| `mRCOMB3`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | R comb tap 3 addr |
| `mLCOMB4`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | L comb tap 4 addr |
| `mRCOMB4`| U16 | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | R comb tap 4 addr |
| `dLDIFF` | U16 | `tests/unit/reverb/test_reverb_diff_iir.c::reverb_diff_iir` | L DIFF feedback addr (cross-side) |
| `dRDIFF` | U16 | `tests/unit/reverb/test_reverb_diff_iir.c::reverb_diff_iir` | R DIFF feedback addr (cross-side) |
| `mLAPF1` | U16 | `tests/unit/reverb/test_reverb_apf1.c::reverb_apf1` | L APF1 addr |
| `mRAPF1` | U16 | `tests/unit/reverb/test_reverb_apf1.c::reverb_apf1` | R APF1 addr |
| `mLAPF2` | U16 | `tests/unit/reverb/test_reverb_apf2.c::reverb_apf2` | L APF2 addr |
| `mRAPF2` | U16 | `tests/unit/reverb/test_reverb_apf2.c::reverb_apf2` | R APF2 addr |
| `vLIN`   | I16 | `tests/unit/reverb/test_reverb_input_scale.c::reverb_input_scale` | L input gain |
| `vRIN`   | I16 | `tests/unit/reverb/test_reverb_input_scale.c::reverb_input_scale` | R input gain |

## Per-Behavior Coverage

Rows enumerate the documented algorithmic behaviors across Phases 1–5. Each
points at the narrowest existing ctest that exercises the behavior in
isolation; wider integration coverage (preset × input matrix) is added in
Plan 07-03 (golden files) and Plan 07-05 (modulation harness).

| Behavior | Spec reference | test: | Notes |
|----------|----------------|-------|-------|
| SAME IIR stage | `#reverb-processing` | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | |
| DIFF IIR stage | `#reverb-processing` | `tests/unit/reverb/test_reverb_diff_iir.c::reverb_diff_iir` | Cross-side pairing covered |
| 4-tap comb (cascading saturation) | `#reverb-processing` | `tests/unit/reverb/test_reverb_comb.c::reverb_comb` | D-07 int32 intermediate |
| APF1 stage | `#reverb-processing` | `tests/unit/reverb/test_reverb_apf1.c::reverb_apf1` | |
| APF2 stage | `#reverb-processing` | `tests/unit/reverb/test_reverb_apf2.c::reverb_apf2` | |
| Input scale | `#reverb-processing` | `tests/unit/reverb/test_reverb_input_scale.c::reverb_input_scale` | |
| Output scale | `#reverb-processing` | `tests/unit/reverb/test_reverb_output_scale.c::reverb_output_scale` | |
| Hard clip on mix bus | `#reverb-processing` | `tests/unit/reverb/test_reverb_hard_clip.c::reverb_hard_clip` | ADR-0007 |
| vIIR=0x8000 negation anomaly | `#reverb-processing` | `tests/unit/reverb/test_reverb_same_iir.c::reverb_same_iir` | ADR-0002 (also covered in DIFF IIR) |
| Reverb edges / boundary | `#reverb-processing` | `tests/unit/reverb/test_reverb_edges.c::reverb_edges` | |
| Reverb body composition | `#reverb-processing` | `tests/unit/reverb/test_reverb_body.c::reverb_body` | All stages composed end-to-end |
| BufferAddress wrap formula | `#reverb-buffer` | `tests/unit/buffer/test_buffer_wrap.c::buffer_wrap` | ADR-0006 |
| mBASE snap-on-write | `#reverb-buffer` | `tests/unit/buffer/test_buffer_mbase.c::buffer_mbase` | ADR-0006 |
| 39-tap FIR decimate | `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_decimate.c::fir_decimate` | |
| 39-tap FIR interpolate | `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_interpolate.c::fir_interpolate` | |
| 39-tap FIR bit identity | `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_bit_identity.c::fir_bit_identity` | |
| 39-tap FIR coefficient table | `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_coef_table.c::fir_coef_table` | ADR-Phase-4-C |
| FIR chain latency (58 samples) | `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_chain_latency.c::fir_chain_latency` | ADR-Phase-4-H |
| FIR round-trip transparency | `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_round_trip_transparency.c::fir_round_trip_transparency` | |
| Split write-timing policy | `#spureverbregisters` | `tests/unit/registers/test_register_policy.c::register_policy` | ADR-0005 |
| Q15 truncation-not-rounding | `#spu-fixed-point` | `tests/unit/q15/test_q15.c::q15_unit` | ADR-0001 |
| Register I/O type preservation | `#spureverbregisters` | `tests/unit/registers/test_register_types.c::register_types` | |
| Register facade parity | `#spureverbregisters` | `tests/unit/registers/test_register_facade.c::register_facade_unit` | Plan 02-03 wrappers |
| All 35 registers round-trip | `#spureverbregisters` | `tests/unit/registers/test_register_roundtrip.c::register_roundtrip` | |
| Register identity (count, names, offsets) | `#spureverbregisters` | `tests/unit/registers/test_register_identity.c::register_identity_unit` | |
| Preset table byte-for-byte audit | `#reverb-examples` | `tests/unit/preset/test_preset_table_integrity.c::preset_table_integrity` | 334/350 cells match two sources |
| Preset load + active/pending split | `#reverb-examples` | `tests/unit/preset/test_preset_load_all.c::test_preset_load_all` | D-08 policy respected |
| Non-Off preset produces reverb tail | `#reverb-examples` | `tests/unit/preset/test_preset_nonzero_tail.c::test_preset_nonzero_tail` | |
| Block-size invariance | N/A (project invariant) | `tests/unit/process/test_process_block_size.c::test_process_block_size` | API-03 |
| In-place processing bit-identity | N/A (project invariant) | `tests/unit/process/test_process_in_place.c::test_process_in_place` | API-03 |
| Flush zero-tail state | `#reverb-processing` | `tests/unit/process/test_process_flush.c::test_process_flush` | |
| Mid-stream write determinism | N/A (project invariant) | `tests/python/fuzz_process.py::fuzz_process` | 10⁶ steps, API-06 |
| Buffer wrap fuzz (10⁶ steps) | `#reverb-buffer` | `tests/python/fuzz_buffer.py::fuzz_buffer` | Independent Python reference model |
| Reverb network fuzz | `#reverb-processing` | `tests/python/fuzz_reverb.py::fuzz_reverb` | Bit-exact vs Python reference |
| FIR chain fuzz | `#reverb-buffer-resampling` | `tests/python/fuzz_fir.py::fuzz_fir` | Bit-exact vs Python reference |
| RT-safety: no heap in hot path | N/A (project invariant) | `tests/rt_safety/test_no_syscalls.sh::rt_no_syscalls` | Phase 5 strace harness |

## Per-Spec-Paragraph Coverage

Rows map each anchor in the pinned wayback snapshot
(`https://web.archive.org/web/20260114082525/https://psx-spx.consoledev.net/soundprocessingunitspu/`)
to the ctest(s) that satisfy its factual claims. New anchors cited in future
plans append rows here.

| Snapshot anchor | test: | Notes |
|-----------------|-------|-------|
| `#spureverbregisters` | `tests/unit/registers/test_register_roundtrip.c::register_roundtrip` | All 35 registers round-trip |
| `#reverb-processing` | `tests/unit/reverb/test_reverb_body.c::reverb_body` | All 8 stages composed (SAME IIR, DIFF IIR, 4-tap comb, APF1, APF2, input scale, output scale, hard clip) |
| `#reverb-buffer` | `tests/unit/buffer/test_buffer_wrap.c::buffer_wrap` | BufferAddress wrap + mBASE floor |
| `#reverb-buffer-resampling` | `tests/unit/fir/test_fir_round_trip_transparency.c::fir_round_trip_transparency` | 39-tap decimate + interpolate round-trip |
| `#spu-fixed-point` | `tests/unit/q15/test_q15.c::q15_unit` | Q15 truncation-not-rounding, saturation |
| `#reverb-examples` | `tests/unit/preset/test_preset_table_integrity.c::preset_table_integrity` | 10 PS1 factory presets, byte-for-byte |

## Known Gaps

Rows below have an empty `test:` cell intentionally. Each names a later plan
that is expected to fill the gap; the `check_coverage.py` validator tolerates
empty `test:` cells only inside this section.

| Gap | Planned test | Planned plan | Notes |
|-----|--------------|--------------|-------|
| lv2-psx-reverb witness-diff per preset |  | 07-04 | Split-band aligned-RMS divergence; tolerance ADR follow-up |
| 50-golden preset × input corpus |  | 07-03 | 10 presets × 5 inputs; `.wav` + `.sha256` sidecars |
| 35-register modulation harness |  | 07-05 | Sine / sweep / random-walk × all rates |
| pytest-benchmark hot-path timing reports |  | 07-06 | Report-only; hot-path alloc gate via strace |
