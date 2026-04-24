# SPU-94 — Register Levers Catalog

Each of the 35 reverb-affecting SPU registers, annotated with its musical
role, modulation cost, observed zipper behavior, and suggested M4 lever
grouping candidacy. Mechanical columns (AUTO) are populated by
`tests/python/modulation_harness.py` via `scripts/write_levers_catalog.py`;
subjective columns (HAND) are written by Anthony and preserved across
catalog regenerations.

**Modulation cost legend (AUTO — populated by the harness):**

- **free** — v-prefix gain register; any modulation rate stays stable and
  deterministic; zipper audibility catalogued in the onset column when
  it fires at the representative 500 Hz sine test.
- **sample-quantized** — d-prefix delay pointer; modulation is honored at
  22.05 kHz tick boundaries; finer rates produce discrete steps.
- **catastrophic** — m-prefix address-base register (plus `mBASE`);
  mid-stream writes relocate memory regions the reverb is still reading
  from; audible as hard clicks. Not a bug — this is the PS1 hardware
  character. M4 smoothing layer is how this is made musical for live
  performance.

**HAND / AUTO column split (D-16):**

- HAND columns are preserved across regenerations. If a HAND cell is
  empty, the writer leaves it empty — it NEVER fills a default.
- AUTO columns are rewritten from `tests/python/modulation_report.json`
  on every run.
- To regenerate AUTO columns:

  ```
  pytest tests/python/test_modulation_harness.py -q
  python3 scripts/write_levers_catalog.py
  ```

## Register Levers

| Register | Musical role (HAND) | Modulation cost (AUTO) | Zipper onset (AUTO) | M4 lever (HAND) |
|----------|---------------------|------------------------|---------------------|-----------------|
| `vLOUT`  |  | — | — | |
| `vROUT`  |  | — | — | |
| `mBASE`  |  | — | — | |
| `dAPF1`  |  | — | — | |
| `dAPF2`  |  | — | — | |
| `vIIR`   |  | — | — | |
| `vCOMB1` |  | — | — | |
| `vCOMB2` |  | — | — | |
| `vCOMB3` |  | — | — | |
| `vCOMB4` |  | — | — | |
| `vWALL`  |  | — | — | |
| `vAPF1`  |  | — | — | |
| `vAPF2`  |  | — | — | |
| `mLSAME` |  | — | — | |
| `mRSAME` |  | — | — | |
| `mLCOMB1` |  | — | — | |
| `mRCOMB1` |  | — | — | |
| `mLCOMB2` |  | — | — | |
| `mRCOMB2` |  | — | — | |
| `dLSAME` |  | — | — | |
| `dRSAME` |  | — | — | |
| `mLDIFF` |  | — | — | |
| `mRDIFF` |  | — | — | |
| `mLCOMB3` |  | — | — | |
| `mRCOMB3` |  | — | — | |
| `mLCOMB4` |  | — | — | |
| `mRCOMB4` |  | — | — | |
| `dLDIFF` |  | — | — | |
| `dRDIFF` |  | — | — | |
| `mLAPF1` |  | — | — | |
| `mRAPF1` |  | — | — | |
| `mLAPF2` |  | — | — | |
| `mRAPF2` |  | — | — | |
| `vLIN`   |  | — | — | |
| `vRIN`   |  | — | — | |
