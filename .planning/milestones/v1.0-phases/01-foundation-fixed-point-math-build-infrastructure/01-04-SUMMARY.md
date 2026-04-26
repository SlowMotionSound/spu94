---
phase: 01-foundation-fixed-point-math-build-infrastructure
plan: 04
subsystem: infra
tags: [ci, grep-guard, documentation, fixture, bash]

requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    provides: "Plan 01-02 shipped the functional grep-guard (Pass 1 + Pass 2 `long long`-subtraction logic) and the 6-case fixture meta-test. This gap plan consumes both as-is and only adds documentation + case 7 on top."
provides:
  - "Formal KNOWN LIMITATIONS comment block in scripts/ci/grep-guard.sh documenting the line-granular `long long` / `long` edge case and pointing to the fixture pin in test-grep-guard.sh"
  - "Fixture case 7 in scripts/ci/test-grep-guard.sh that pins the CURRENT documented behavior (exit 0 on `long long ll; long n;`) so a future tightening of the guard cannot ship silently"
  - "Updated OK message in test-grep-guard.sh reading `all 7 fixture cases passed, incl. case 7 known-limitation pin`"
affects: [01-03, 01-verification, 01-review, 02-register-file, future-ci-hardening]

tech-stack:
  added: []
  patterns:
    - "Fixture-pinned CI documentation: every 'accepted limitation' in a CI guard is paired with a fixture case that will FAIL the day someone changes the guard, forcing intentional, reviewed updates"

key-files:
  created: []
  modified:
    - scripts/ci/grep-guard.sh
    - scripts/ci/test-grep-guard.sh

key-decisions:
  - "Documentation-only + fixture-pin (no pattern-logic change) — WR-01..WR-04 awk tokenizer / pre-commit / per-token hardening items from 01-REVIEW.md are explicitly deferred out of scope per the 01-04-PLAN scope boundary"
  - "Case 7 asserts exit 0, not exit 1 — we pin the CURRENT documented behavior, not aspirational per-token behavior; a future tightening must update both the guard and case 7, and that update is the forcing function"

patterns-established:
  - "Accepted-limitation-as-fixture: a CI script's KNOWN LIMITATIONS block names the fixture location that pins the limitation; any future change to the guard's semantics trips the fixture and demands intentional review"

requirements-completed:
  - BUILD-07

duration: 6min
completed: 2026-04-18
---

# Phase 01 Plan 04: Grep-Guard KNOWN LIMITATIONS Gap Closure Summary

**Added formal KNOWN LIMITATIONS block to grep-guard.sh (documenting the line-granular `long long`/`long` edge case and linking to the fixture pin) and case 7 to test-grep-guard.sh (asserting current exit-0 behavior on `long long ll; long n;`) — the final discipline layer promised by 01-02-PLAN and flagged missing by 01-VERIFICATION.md.**

## Performance

- **Duration:** ~6 min
- **Started:** 2026-04-18T22:33:00Z (approx)
- **Completed:** 2026-04-18T22:38:53Z
- **Tasks:** 1 (single combined gap-closure task)
- **Files modified:** 2

## Accomplishments

- `scripts/ci/grep-guard.sh` now carries a 27-line formal KNOWN LIMITATIONS block (separator lines + heading + body + trailing separator), not the drive-by 3-line inline comment it had before. The block uses the term "line-granular" verbatim, explains the Pass 2 `grep -v 'long long'` mechanism, enumerates four reasons the limitation is accepted, and names `scripts/ci/test-grep-guard.sh` with the exact case 7 label as the fixture pin.
- `scripts/ci/test-grep-guard.sh` now invokes `run_case "known limitation: mixed long long + long on one line" 0 "src/edge.c" 'long long ll; long n;'` as case 7, with a 5-line explanatory comment pointing back at grep-guard.sh's KNOWN LIMITATIONS block. The final OK message reads `test-grep-guard: OK (all 7 fixture cases passed, incl. case 7 known-limitation pin).`.
- Guard pattern logic is byte-for-byte unchanged (`FORBIDDEN_CORE`, `LONG_PATTERN`, both `grep -nE` invocations, and the `grep -v 'long long'` subtraction are untouched). The change is pure documentation + fixture scope.
- Re-verification against Phase 1 Success Criterion 4 now passes cleanly (previously `VERIFIED (partial)`).

## Task Commits

1. **Task 1: Add KNOWN LIMITATIONS block to grep-guard.sh and case 7 to test-grep-guard.sh** — `6adabf8` (docs)

Plan metadata (this SUMMARY + final state): _to be written by orchestrator per plan-04's no-STATE-update directive_.

## Files Created/Modified

- `scripts/ci/grep-guard.sh` — +28 lines / -0 lines (50 → 78 lines total). New KNOWN LIMITATIONS section inserted between the existing `# See docs/DECISIONS.md ADR-0003 area (future) ...` comment and `set -euo pipefail`. The pre-existing short inline comment at what are now lines 62-65 (Pass 2 caveat) is retained unchanged — both the inline form and the formal block now coexist.
- `scripts/ci/test-grep-guard.sh` — +9 lines / -1 line (77 → 85 lines total). Case 7 block inserted between the case 6 subshell's `) || fail=1` and the final `if [ "$fail" -ne 0 ]` check. Final `echo` updated to reference 7 cases.

Byte/line check via `git diff --stat HEAD~1 HEAD`:
```
 scripts/ci/grep-guard.sh      | 28 ++++++++++++++++++++++++++++
 scripts/ci/test-grep-guard.sh | 10 +++++++++-
 2 files changed, 37 insertions(+), 1 deletion(-)
```

`git diff --name-only HEAD~1 HEAD`:
```
scripts/ci/grep-guard.sh
scripts/ci/test-grep-guard.sh
```

Exactly the two expected files. No collateral changes to `.github/workflows/ci.yml`, `.clang-tidy`, `scripts/ci/verify-flags.sh`, `docs/DECISIONS.md`, or anything under `src/`, `include/`, `tests/`.

## Post-Change Verification

Both scripts exit 0:

```
$ bash scripts/ci/grep-guard.sh
grep-guard: OK (scanned 3 files).

$ bash scripts/ci/test-grep-guard.sh
PASS: clean tree (expected exit 0, got 0)
PASS: float in src (expected exit 1, got 1)
PASS: malloc in include (expected exit 1, got 1)
PASS: long long allowed (expected exit 0, got 0)
PASS: unqualified long forbidden (expected exit 1, got 1)
PASS: empty tree (exit 0)
PASS: known limitation: mixed long long + long on one line (expected exit 0, got 0)
test-grep-guard: OK (all 7 fixture cases passed, incl. case 7 known-limitation pin).
```

All 7 PASS lines present. The new case 7 appears last and reports `expected exit 0, got 0` as the KNOWN LIMITATIONS block promises.

All 8 automated acceptance conditions from 01-04-PLAN's `<acceptance_criteria>` verify:

1. `grep -q 'KNOWN LIMITATIONS' scripts/ci/grep-guard.sh` — PASS
2. `grep -q 'line-granular' scripts/ci/grep-guard.sh` — PASS (the word is used in lowercase in the body: "is line-granular, not token-granular")
3. `grep -q 'test-grep-guard.sh' scripts/ci/grep-guard.sh` — PASS
4. `grep -q 'known limitation: mixed long long + long on one line' scripts/ci/test-grep-guard.sh` — PASS
5. `grep -q 'long long ll; long n;' scripts/ci/test-grep-guard.sh` — PASS
6. `grep -q 'all 7 fixture cases' scripts/ci/test-grep-guard.sh` — PASS
7. `bash scripts/ci/grep-guard.sh` exits 0 on the real repo tree — PASS
8. `bash scripts/ci/test-grep-guard.sh` exits 0 with 7 PASS lines — PASS

## 01-VERIFICATION.md `missing:` Items Closed

Both items from the `gaps:` block of 01-VERIFICATION.md are satisfied verbatim:

- "Add a KNOWN LIMITATIONS block near the top of scripts/ci/grep-guard.sh documenting the line-granular 'long long' subtraction edge case and pointing to the fixture pin in test-grep-guard.sh" — closed by the 27-line block inserted at lines 12-39 of grep-guard.sh, which uses the term "line-granular" and explicitly names "scripts/ci/test-grep-guard.sh" alongside the exact case 7 label.
- "Add case 7 to scripts/ci/test-grep-guard.sh: run_case \"known limitation: mixed long long + long on one line\" 0 \"src/edge.c\" 'long long ll; long n;' and update the final message to reference case 7" — closed by the new case 7 at line 76 of test-grep-guard.sh and the updated final OK message at line 84.

Phase 1 Success Criterion 4 (`A CI grep guard fails the build if float, double, malloc, calloc, realloc, free, or unqualified long appear in core library sources.`) now verifies fully, no longer "partial".

## Decisions Made

- **Retained the pre-existing short inline Pass 2 comment unchanged.** The 01-04-PLAN explicitly permitted either keeping it or trimming it to a one-line pointer. Kept as-is to minimize diff surface; the inline and block comments do not conflict — the inline describes _what_ Pass 2 does, the block describes _why the line-granular behavior is accepted and pinned_.
- **Case 7 asserts exit 0, not exit 1.** The fixture pins the CURRENT documented behavior. A future contributor who tightens the guard to per-token matching MUST update both the guard and case 7; that coupled update is the forcing function. Pinning the aspirational exit-1 would have made the fixture fail today, which is wrong.
- **WR-01..WR-04 explicitly left out of scope.** The 01-REVIEW.md code-review items (awk tokenizer, pre-commit hook, further CI hardening) are optional non-blocking follow-ups and will be addressed — if at all — in a separate code-review-fix cycle at the user's discretion. The scope boundary in 01-04-PLAN is narrow and was respected.

## Deviations from Plan

None — plan executed exactly as written. The verbatim comment blocks from the `<action>` section landed as-is in both files; the final OK message matches the prescribed string verbatim.

## Issues Encountered

None. The gap-closure diff was small and the verifier's acceptance criteria were pinned to exact grep patterns, which made verification deterministic.

## User Setup Required

None — no external service configuration, no environment variables, no dashboard work. This plan is pure CI-script documentation + fixture pinning.

## Next Phase Readiness

- Phase 1 is now fully closed: all four Success Criteria verify cleanly on re-check. The last `VERIFIED (partial)` status on SC4 is resolved.
- BUILD-07's documentation sub-gap is closed; BUILD-07 itself was functionally satisfied by Plan 01-02. No new requirement IDs introduced by this plan.
- WR-01..WR-04 from 01-REVIEW.md remain open as optional follow-ups. They do not block Phase 2.
- Ready to proceed to Phase 2 (Register File + Mid-Stream Write API) once the orchestrator closes Phase 1 formally.

## Self-Check: PASSED

- FOUND: scripts/ci/grep-guard.sh (modified, 78 lines, contains KNOWN LIMITATIONS block)
- FOUND: scripts/ci/test-grep-guard.sh (modified, 85 lines, contains case 7 and 7-fixture OK message)
- FOUND: .planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-04-SUMMARY.md (this file)
- FOUND commit: 6adabf8 (`docs(01-04): close gap — KNOWN LIMITATIONS block + fixture case 7`)
- Verified both scripts exit 0 and test-grep-guard reports 7 PASS lines (pasted verbatim above).
- Verified `git diff --name-only HEAD~1 HEAD` lists exactly the two expected files.

---
*Phase: 01-foundation-fixed-point-math-build-infrastructure*
*Completed: 2026-04-18*
