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
| `vLOUT`  | Master Output | free | clean through 11 kHz | |
| `vROUT`  | Master Output | free | clean through 11 kHz | |
| `mBASE`  |  | catastrophic | clean through 11 kHz | |
| `dAPF1`  |  | sample-quantized | clean through 11 kHz | |
| `dAPF2`  |  | sample-quantized | clean through 11 kHz | |
| `vIIR`   |  | free | ~500 Hz | |
| `vCOMB1` |  | free | clean through 11 kHz | |
| `vCOMB2` |  | free | clean through 11 kHz | |
| `vCOMB3` |  | free | clean through 11 kHz | |
| `vCOMB4` |  | free | clean through 11 kHz | |
| `vWALL`  |  | free | clean through 11 kHz | |
| `vAPF1`  |  | free | ~500 Hz | |
| `vAPF2`  |  | free | clean through 11 kHz | |
| `mLSAME` |  | catastrophic | clean through 11 kHz | |
| `mRSAME` |  | catastrophic | clean through 11 kHz | |
| `mLCOMB1` |  | catastrophic | clean through 11 kHz | |
| `mRCOMB1` |  | catastrophic | clean through 11 kHz | |
| `mLCOMB2` |  | catastrophic | clean through 11 kHz | |
| `mRCOMB2` |  | catastrophic | clean through 11 kHz | |
| `dLSAME` |  | sample-quantized | clean through 11 kHz | |
| `dRSAME` |  | sample-quantized | clean through 11 kHz | |
| `mLDIFF` |  | catastrophic | clean through 11 kHz | |
| `mRDIFF` |  | catastrophic | clean through 11 kHz | |
| `mLCOMB3` |  | catastrophic | clean through 11 kHz | |
| `mRCOMB3` |  | catastrophic | clean through 11 kHz | |
| `mLCOMB4` |  | catastrophic | clean through 11 kHz | |
| `mRCOMB4` |  | catastrophic | clean through 11 kHz | |
| `dLDIFF` |  | sample-quantized | clean through 11 kHz | |
| `dRDIFF` |  | sample-quantized | clean through 11 kHz | |
| `mLAPF1` |  | catastrophic | clean through 11 kHz | |
| `mRAPF1` |  | catastrophic | clean through 11 kHz | |
| `mLAPF2` |  | catastrophic | clean through 11 kHz | |
| `mRAPF2` |  | catastrophic | clean through 11 kHz | |
| `vLIN`   |  | free | clean through 11 kHz | |
| `vRIN`   |  | free | clean through 11 kHz | |
