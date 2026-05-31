# Phase 61 — Deferred / Out-of-Scope Items

Discoveries made during execution that are OUT OF SCOPE for the current task
(SCOPE BOUNDARY rule: only auto-fix issues directly caused by the current task's
changes). Logged here, not fixed.

## Pre-existing full-suite failures (NOT caused by Plan 61-01)

Observed during the Plan 61-01 full-suite regression run (`ctest --test-dir build`):

| Test # | Name | Label | Result | Why out of scope |
|--------|------|-------|--------|------------------|
| 101 | `test_packaging_editable_install` | packaging | Timeout (600s) | Python `pip install -e .` wheel test in `tests/packaging/`. No reference to `PluginProcessor.h`; the Plan 61-01 header change cannot affect it. Environment/network-bound pip build, pre-existing. |
| 102 | `test_packaging_wheel_tag` | packaging | Timeout (600s) | Python wheel platform-tag test in `tests/packaging/`. Same rationale — unrelated to the C++ plugin layer. |

These are Python packaging/install tests; the user's standing guidance is to not
chase README / install-path / PyPI / packaging findings. They predate this phase
and are not a regression from the friend-seam header additions. Left untouched.

The 6 `voice_controls_*` failures in the same run are the **intended RED baseline**
of this RED→GREEN TDD pair (Plan 02 flips them green), documented in
`61-01-SUMMARY.md` — they are NOT deferred items.
