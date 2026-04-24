"""tests/python/_constants.py — single source of truth for cross-harness constants.

Prevents silent drift between Python modules that hardcode the same value
(e.g., the lv2-psx-reverb commit pin). When a pin is bumped, this file is
the only Python-side edit required; the shell build script
(``scripts/ci/witness_diff_build.sh``) reads the same SHA from its
``LV2_COMMIT`` env var and must be bumped in lockstep. The
``test_witness_diff_pin_single_source_of_truth`` style meta-assertion in
``test_witness_determinism.py`` enforces agreement between this module
and the Python harness.
"""
from __future__ import annotations

# D-05 supply-chain pin: the lv2-psx-reverb commit SHA every witness-diff
# row must record. Duplicated intentionally in
# ``scripts/ci/witness_diff_build.sh`` (shell can't import Python) — that
# file's ``LV2_COMMIT`` default MUST match this constant.
LV2_COMMIT_PIN = "424e1e8ee7f780106b005011b036386513c61db3"
