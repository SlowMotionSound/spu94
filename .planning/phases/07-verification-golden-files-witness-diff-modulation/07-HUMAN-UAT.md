---
status: resolved
phase: 07-verification-golden-files-witness-diff-modulation
source: [07-VERIFICATION.md]
started: 2026-04-24T00:51:08Z
updated: 2026-04-24T01:15:00Z
---

## Current Test

[all tests resolved]

## Tests

### 1. Docker reproducibility re-run (BUILD-08 / TEST-08)
expected: Both commands exit 0; `docker run --rm spu94-repro` prints `PASS: 50/50 goldens match`
result: pass
verified: 2026-04-24
evidence: Anthony ran `python3 scripts/regenerate_goldens.py --check` on host → "PASS: 50/50 goldens match". Then `sudo docker run --rm spu94-repro` in the pinned bookworm-slim container (image ID da444c99ce21) → "PASS: 50/50 goldens match". Byte-reproducibility contract holds across host + container after Wave 3/4 changes.
command: |
  python3 scripts/regenerate_goldens.py --check
  sudo docker run --rm spu94-repro
why_human: Host smoke-test was verified on 2026-04-23 (per 07-02-SUMMARY), but the test needs a re-run in a clean container to confirm the repro contract still holds after Wave 3/4 changes. CI has not yet run since phase completion.

### 2. First LEVERS-CATALOG HAND entry (DOCS-02)
expected: After editing, run `python3 scripts/write_levers_catalog.py` and confirm your hand-written annotation survives the rewrite unchanged.
result: pass
verified: 2026-04-24
evidence: Anthony added "Master Output" to the Musical role (HAND) column for `vLOUT` and `vROUT` in docs/LEVERS-CATALOG.md. Ran `python3 scripts/write_levers_catalog.py` — writer reported "No changes to docs/LEVERS-CATALOG.md", confirming the idempotent preservation contract. HAND entries survived verbatim.
command: |
  # Edit docs/LEVERS-CATALOG.md — fill ONE register's HAND columns
  # (e.g., vIIR: musical role = "reverb feedback amount / decay tail")
  python3 scripts/write_levers_catalog.py
  git diff docs/LEVERS-CATALOG.md  # HAND entry should NOT appear as removed
why_human: DOCS-02 requires LEVERS-CATALOG.md to be "begun and maintained." AUTO columns are populated; HAND columns (musical role, M4 lever grouping) are intentionally empty for you to fill. This first entry confirms the writer's preservation contract works with real content.

## Summary

total: 2
passed: 2
issues: 0
pending: 0
skipped: 0
blocked: 0

## Gaps
