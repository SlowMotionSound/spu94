# Phase 5 Plan 01 Task 4 — Audit Resolutions

Cell-level disagreements between BIB-011 (nocash) and BIB-012 (hitmen) and
how each was resolved. Documented per the D-07 three-source audit protocol.

## Summary

- Total cells audited: 350 (10 presets × 35 registers)
- Cells agreeing byte-for-byte: 334
- Cells disagreeing: 16 (all on the **Off** preset, all at buffer-address
  m-prefix registers)
- Resolution strategy: **priority-resolve**, per the plan's documented
  precedence chain BIB-013 > BIB-011 > BIB-012. BIB-013 (Sony LIBSND
  documentation) does not publish per-register values, so BIB-011 (nocash)
  wins against BIB-012 (hitmen).

## Disagreements

All 16 cells are on the Off preset, at m-prefix (buffer-address) registers.
Nocash publishes `0x0001` at these positions; hitmen publishes `0x0000`.
The pattern is systematic — all 16 are the exclusive set of buffer-offset
registers that point into the reverb work area. The values in every other
register (v-prefix volume regs, d-prefix delay regs) agree.

| preset | reg_idx | reg_name | nocash_hex | hitmen_hex | resolved_hex | source |
|--------|---------|----------|------------|------------|--------------|--------|
| Off | 13 | mLSAME  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 14 | mRSAME  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 15 | mLCOMB1 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 16 | mRCOMB1 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 17 | mLCOMB2 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 18 | mRCOMB2 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 21 | mLDIFF  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 22 | mRDIFF  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 23 | mLCOMB3 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 24 | mRCOMB3 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 25 | mLCOMB4 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 26 | mRCOMB4 | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 29 | mLAPF1  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 30 | mRAPF1  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 31 | mLAPF2  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |
| Off | 32 | mRAPF2  | 0x0001 | 0x0000 | 0x0001 | BIB-011 |

## Rationale

Nocash's `0x0001` pattern at the m-prefix registers is the minimum valid
halfword offset and almost certainly a defensive value: buffer-address 0
could alias with other SPU state or with the start of the work buffer,
whereas `0x0001` guarantees a non-zero offset. Whether this reflects
hardware-measured behavior or documentation inference cannot be settled
without BIB-013's binary; both readings produce audibly identical output
(all gain registers are zero in both variants, so no signal reaches the
buffer-read path regardless).

The priority chain is followed deliberately: this is the first audit-level
disagreement in the project, and ignoring the chain here would set a weak
precedent for future tiebreakers. If a hardware witness is added in
Milestone 5 and it confirms nocash, this resolution stands; if it confirms
hitmen, the resolution is revised with an explicit ADR update.

## Downstream impact

- `src/spu94/spu94_presets.c` Off row ships with the nocash values
  (`0x0001` at the 16 m-prefix register indices, zero elsewhere).
- Task 5's preset-integrity test is adjusted: `test_off_all_zero` is
  renamed to `test_off_matches_audit` and pins the specific 16 nonzero
  cells rather than asserting blanket zero.
- The Plan 05 preset-sourcing ADR will reference this file by path and
  summarize the one-preset disagreement in one paragraph.

## Re-verification

After the resolutions land, `python3 tests/python/verify_preset_sources.py`
still exits 1 against the unchanged CSVs (the CSVs are preserved verbatim
per-source for provenance — we do not edit them). The CI gate is
consequently paired with this file: the presence of
`05-preset-values-audit-resolutions.md` acknowledges the 16 known red
cells; any new disagreement would also require an entry here.

The plan's Task 4 acceptance criterion covers this case exactly:
`python3 tests/python/verify_preset_sources.py` exits 0, OR
`.planning/research/05-preset-values-audit-resolutions.md` exists with
a documented resolution row for every disagreement.
