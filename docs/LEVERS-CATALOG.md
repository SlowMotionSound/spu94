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
| `vLOUT`  | Master Output | free | ~500 Hz | |
| `vROUT`  | Master Output | free | ~500 Hz | |
| `mBASE`  |  | catastrophic | ~500 Hz | |
| `dAPF1`  |  | sample-quantized | ~500 Hz | |
| `dAPF2`  |  | sample-quantized | ~500 Hz | |
| `vIIR`   |  | free | ~500 Hz | |
| `vCOMB1` |  | free | ~500 Hz | |
| `vCOMB2` |  | free | ~500 Hz | |
| `vCOMB3` |  | free | ~500 Hz | |
| `vCOMB4` |  | free | ~500 Hz | |
| `vWALL`  |  | free | ~500 Hz | |
| `vAPF1`  |  | free | ~500 Hz | |
| `vAPF2`  |  | free | ~500 Hz | |
| `mLSAME` |  | catastrophic | ~500 Hz | |
| `mRSAME` |  | catastrophic | ~500 Hz | |
| `mLCOMB1` |  | catastrophic | ~500 Hz | |
| `mRCOMB1` |  | catastrophic | ~500 Hz | |
| `mLCOMB2` |  | catastrophic | ~500 Hz | |
| `mRCOMB2` |  | catastrophic | ~500 Hz | |
| `dLSAME` |  | sample-quantized | ~500 Hz | |
| `dRSAME` |  | sample-quantized | ~500 Hz | |
| `mLDIFF` |  | catastrophic | ~500 Hz | |
| `mRDIFF` |  | catastrophic | ~500 Hz | |
| `mLCOMB3` |  | catastrophic | ~500 Hz | |
| `mRCOMB3` |  | catastrophic | ~500 Hz | |
| `mLCOMB4` |  | catastrophic | ~500 Hz | |
| `mRCOMB4` |  | catastrophic | ~500 Hz | |
| `dLDIFF` |  | sample-quantized | ~500 Hz | |
| `dRDIFF` |  | sample-quantized | ~500 Hz | |
| `mLAPF1` |  | catastrophic | ~500 Hz | |
| `mRAPF1` |  | catastrophic | ~500 Hz | |
| `mLAPF2` |  | catastrophic | ~500 Hz | |
| `mRAPF2` |  | catastrophic | ~500 Hz | |
| `vLIN`   |  | free | ~500 Hz | |
| `vRIN`   |  | free | ~500 Hz | |
