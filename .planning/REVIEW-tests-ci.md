# SPU-94 Tests + CI Infrastructure Code Review — Milestone v1.0 Close-Out

**Reviewed:** 2026-04-24
**Depth:** deep (cross-file + invariant chase)
**Scope:** `tests/**`, `scripts/ci/**`, `scripts/*.py`, `.github/workflows/ci.yml`

## Summary

Strong test infrastructure overall — the reproducibility Docker pin, SHA-pinned GH Actions, negative meta-tests for the hotpath alloc gate, and the Python-model-vs-C comparison in `fuzz_buffer.py` are all genuinely high-quality. The grep-guard is self-tested with an explicit "known limitation" pin.

**However, the work_buf_size bug class is not an isolated mistake — it is a systemic hole in multiple layers at once.** The C-side library silently no-ops buffer accesses when they exceed `work_buf_size` (`spu94_reverb.c:54`, `:69`), there is no preset-buffer-requirement validator anywhere, and the Python binding defaults the value to an 8 KiB that is provably insufficient for 5+ of the 10 presets. Every layer trusts the next; none gate.

**Critical-severity finding count: 6.**
**High-severity: 7. Medium: 6. Low: 3. Nits: 3.**

The goldens corpus is the one line of defense that *would* have caught the Hall work_buf_size bug — but only because the CLI hardcodes 512 KB. That makes the goldens correct, but it also means "Python binding default + modulation harness" is an entirely separate code path that never touches the gated surface.

---

## Critical Issues

### CR-01: `spu94_reverb.c` silently drops out-of-range buffer accesses — no caller is warned

**Files:** `src/spu94/spu94_reverb.c:51-72`

`reverb_read_u16`/`reverb_write_u16` return `0` / no-op when `byte_off + 1 >= work_buf_size`. There is no error path, no `SPU94_CLAMPED`-style return, no diagnostic counter, and critically: no `err_*` accumulator incremented. Every reverb-body test's "stability_ok" assertion is satisfied even when every single memory tap is returning zero. This is the *mechanism* by which the work_buf_size bug produced degraded audio that passed tests.

**Fix:** At minimum, bump an `err_work_buf_short` counter inside both branches, surface it via a `spu94_get_error_counters()` reader, and assert it's zero in `test_process_reverb_audible.c`, `test_modulation_harness.py`, and all four fuzz harnesses. Better: have `spu94_load_preset` compute the preset's max byte offset (scan mL*/mR*/dL*/dR* and double them) and return `SPU94_WORK_BUF_TOO_SMALL` when `work_buf_size` is insufficient. This is a one-time O(35) scan at load time — well within rt-safety.

### CR-02: Python binding default `work_buf_size=8192` is insufficient for half the factory presets

**Files:**
- `python/spu94/api.py:52` — `def init(work_buf_size: int = 8192)`
- `python/spu94/reverb.py:56` — `def __init__(self, work_buf_size: int = 8192)`
- `python/spu94/api.py:414` — `self_test()` uses 8192 too
- `tests/python/fuzz_reverb.py:79` — `WORK_BUF_SIZE = 8192`
- `tests/python/fuzz_buffer.py:63` — `WORK_BUF_SIZE = 8192`

Hall's `mLSAME=0x0DFB` (3579 halfwords → 7158 byte offset, right at the limit), Space Echo's `mLSAME=0x15BA` (11194 → exceeds), and several Delay/Echo offsets all exceed 8192 bytes. Combined with CR-01, any call through the Python binding with defaults silently produces wrong (degraded) reverb output. The modulation harness triggered exactly this.

**Fix:** Default to 0x80000 (512 KB, matching CLI `main.c:153` and `bench_latency.py:71` which already uses 64 KB, hotpath target which uses 524288). 512 KB is nothing on any host Python could possibly run on, and it matches the PS1 SPU's actual reverb RAM. Alternatively, default to `None` and have `load_preset` re-allocate if the current buffer is too small. Bump `fuzz_reverb.py:79` and `fuzz_buffer.py:63` in lockstep.

### CR-03: Preset "stability" tests pass on silently-degraded audio

**Files:** `tests/python/test_modulation_harness.py:55-63`, `tests/python/modulation_harness.py:215-219`

`stability_ok = isfinite(x) and min >= -32768 and max <= 32767`. A preset whose entire internal delay network returns zero (CR-01 firing) trivially satisfies all three conditions — `0` is finite and within int16 bounds. The assertion is weaker than the claim "D-17 stability + determinism verified for every register at every modulation rate." Determinism tests reading the same zero twice also pass.

**Fix:** Add a non-silence floor: after preset-prime (e.g., 2 seconds of noise input), require `abs(output).max() >= SOME_AUDIBLE_FLOOR` for any non-Off preset, AND cross-check against a precomputed "reverb actually working" signature (even a loose one: RMS-dBFS of output vs the known Off reference >= some floor). The C-side `test_process_reverb_audible.c:111-152` already does this correctly; the Python modulation harness needs to inherit the same discipline.

### CR-04: `test_init_success` accepts `work[64]` — zero-size/under-size work buffer is "legal"

**File:** `tests/unit/state/test_state_lifecycle.c:79-96`

`test_init_success` allocates `work[64]` (64 bytes!) and asserts `spu94_init` returns non-NULL. `test_init_accepts_null_workbuf_with_zero_size` explicitly pins "NULL + 0 is legal." These tests *codify* that no work-buffer size is ever rejected by the library — which is the precondition that lets CR-02 ship.

**Fix:** Once CR-01 gains a preset-validation layer, add `test_init_with_undersized_work_buf_is_flagged_on_preset_load` asserting that load_preset returns a new `SPU94_WORK_BUF_TOO_SMALL` (or similar) for each (preset × too-small size) pair.

### CR-05: Witness-diff has NO pass/fail threshold on divergence magnitude

**File:** `scripts/ci/witness_diff.py:788`

The harness exits 0 as long as it successfully *computes* numbers. D-06 is explicit: "this harness prints numbers and writes JSON. It does NOT gate pass/fail on divergence magnitude." This is documented, but it means the "bit-faithful vs lv2-psx-reverb" witness-diff job never fails CI on drift. Combined with CR-01: Hall's low_band_diff could silently climb to 0 dBFS (total divergence) and the job still reports PASS. `test_witness_determinism.py` only verifies that running the harness *twice* produces identical numbers — which is also satisfied by "always producing garbage in the same way."

**Fix:** Land the deferred tolerance-policy ADR before M1 close-out. At minimum, set a loose gate (e.g., low_band_diff_dbfs < -20 dBFS for each preset × input on the subset where lv2 is a valid witness per ADR-Phase-4-I). A loose gate that catches "completely broken" is infinitely better than no gate at all. The determinism test pins that numbers are *stable*, not that they are *correct*.

### CR-06: Goldens' SHA256 baseline is only as trustworthy as the CLI that generated them

**Files:** `scripts/regenerate_goldens.py:117` (locates CLI), `src/cli/main.c:153` (512 KB work buf)

The 50 golden WAVs were generated by shelling to the native `spu94` CLI, which uses 512 KB. Good — but that is the *only* thing that saves the goldens. If anyone ever regenerates via Python (`python -m spu94 ...` or an equivalent Python-driven path), the goldens will be contaminated with CR-01/CR-02 degradation, and subsequent `--check` runs will silently validate that corruption (they only compare new renders to the committed SHA; a corrupted *committed* SHA is not detectable). There is no externally-anchored "reference" anywhere.

**Fix:** (a) add a test that `regenerate_goldens.py` refuses to run via a Python-rendered path; (b) add a spot-check test that at least three presets' goldens exceed a specific known-good audible-energy floor (precomputed offline and hardcoded — tiny anchor so "bits are identical but to garbage" is impossible); (c) document the Python-render path is explicitly not supported and never will be a golden-generation mechanism.

---

## High-Severity Issues

### WR-01: `test-grep-guard.sh` fixtures cannot distinguish "scanned and found nothing" from "scanned nothing"

**File:** `scripts/ci/test-grep-guard.sh:17-43`

CASE 1 asserts "clean tree exit 0" by seeding ONE allowed file — it does not prove the guard ran against that file at all. `>/dev/null 2>&1` hides the `scanned N files` confirmation line. You cannot distinguish "guard passed because it scanned 1 file and found nothing" from "guard passed because find returned zero files and it early-exited."

**Fix:** Capture stdout and grep for `scanned 1 files` (or `scanned N files` with N>0) before accepting a `0` exit as legitimate.

### WR-02: `verify-no-heap-symbols.sh` CI job only checks `libspu94.so`, not the CLI binary or linksym static artifact

**File:** `.github/workflows/ci.yml:112-127` (`verify-no-heap` job)

If a wheel ships an accidentally-linked helper binary that pulls malloc into a process that was supposed to be rt-safe, nothing catches that.

**Fix:** Extend the `verify-no-heap` CI job to also run the script against `build/src/cli/spu94` with an expected-allowlist argument.

### WR-03: `rt_bench_latency` threshold is 2.0 in CMake but script defaults to 3.0

**Files:**
- `tests/rt_safety/bench_latency.py:28` — `THRESHOLD = ... else 3.0`
- `tests/rt_safety/CMakeLists.txt:134` — `RT_LATENCY_THRESHOLD "2.0"`

A developer who runs `python bench_latency.py` manually gets 3.0 and thinks green. No CI-measured value is recorded anywhere.

**Fix:** (a) Make the threshold required (no default). (b) Record the CI-observed ratio into a report artifact per-run. (c) Consider an absolute bound in addition to the relative ratio.

### WR-04: `hotpath_alloc_gate.sh` collapses three failure modes into one generic FAIL

**File:** `tests/rt_safety/hotpath_alloc_gate.sh:72-78`

`MARKER_COUNT != 2` could mean (a) target raised zero SIGUSR1, (b) raised one (crashed), or (c) raised more than two. All three report as "expected 2, got N". A crashed target that never reached the hot window would ALSO fail and WILL_FAIL would flip it to PASS — a false-green on the negative test.

**Fix:** Split the check with distinct exit codes; use `PASS_REGULAR_EXPRESSION`/`FAIL_REGULAR_EXPRESSION` in ctest to pin exact stderr content.

### WR-05: `test_no_syscalls.sh` regex is locale-and-format-fragile

**File:** `tests/rt_safety/test_no_syscalls.sh:61-70`

If a future strace version changes format (e.g., drops pid prefix), EVERY line fails to match, `STEADY_SYSCALLS=0`, `SCAFFOLD_COUNT=0`, and the script prints PASS for a broken parser. The inner `|| true` on `grep -c` ensures pipeline success regardless.

**Fix:** Add a sanity assertion: `if [ "$STEADY_SYSCALLS" -lt 3 ]; then echo "parser produced suspiciously few syscalls; possible format drift"; exit 2; fi`.

### WR-06: grep-guard may not scan `src/cli/` — and `src/cli/main.c:155` calls `malloc`/`free`

**Files:**
- `scripts/ci/grep-guard.sh:47` — `find src include`
- `src/cli/main.c:155` — `malloc((size_t)WORK_BUF_SIZE)` — **THIS IS IN `src/`**

Either: (a) the guard has been silently ignoring this all along, or (b) the script header or exclusion carves cli out. If (a), the guard is broken.

**Fix:** **Verify urgently.** Run `bash scripts/ci/grep-guard.sh` locally. Document the scope explicitly in the script header. If CLI is exempt, add a third Pass scanning `src/cli/*.c` with a narrower allowlist.

### WR-07: `test_check_coverage.py` is a meta-test but not included in this review scope — unverified

**File:** `scripts/ci/test_check_coverage.py` (referenced by ci.yml:171, not loaded here)

If COVERAGE.md claims coverage for behaviors that map to tests that pass for the wrong reasons (CR-03), `check_coverage.py`'s "ctest returned 0" check is satisfied and COVERAGE.md reports full coverage.

**Fix:** Separate audit of `check_coverage.py` + `tests/conformance/test_coverage_map_integrity.py`. Verify empty-test-cell detection actually fires when it should.

---

## Medium-Severity Issues

### WR-08: Reproducibility Docker pin is digest-pinned but arch-scope undocumented

`Dockerfile.repro:33` — single digest may be manifest-list or arch-specific. Non-amd64 runners would silently fall back to pulling-by-tag.

**Fix:** Add a comment clarifying the digest form and that CI is pinned to amd64.

### WR-09: `witness_diff_build.sh` clones `lv2-psx-reverb` unpinned then checks out the SHA

`scripts/ci/witness_diff_build.sh:43-51`. The post-checkout `rev-parse HEAD != $LV2_COMMIT` check is good and catches substitution. Remaining risk: `git clone` pulls all refs, including potentially injected attacker-orphan-branches.

**Fix:** Consider `git init + git fetch --depth=1 $LV2_REPO $LV2_COMMIT` to pull only that one commit's history.

### WR-10: `validate_lv2_port_layout` soft-skips when `lv2info.txt` missing

`scripts/ci/witness_diff.py:636-685`. If `witness_diff_build.sh` fails to produce `lv2info.txt` but exits 0, downstream witness_diff.py runs with hardcoded port constants against whatever port happens to be at index 4.

**Fix:** In CI, add `test -s .artifacts/lv2-psx-reverb/lv2info.txt` as a discrete step after the build, OR have `validate_lv2_port_layout` hard-fail when `SPU94_WITNESS_STRICT=1`.

### WR-11: Goldens' sidecar spot-check is only 3 of 50

`tests/conformance/test_goldens_present.py:61-74`. The other 47 could have mutually-consistent but wrong (forged) hashes and still pass unless the reproducibility Docker job runs.

**Fix:** Spot-check all 50 (cost is trivial), or document the Docker repro job as the primary integrity gate.

### WR-12: `self_test()` uses work_buf_size=8192 with Hall

`python/spu94/api.py:414`. Same as CR-02, but note: the `.any()` assertion is the weakest possible audibility check — any non-zero sample passes.

**Fix:** Same fix as CR-02. Additionally, verify output RMS exceeds a floor AND matches a precomputed signature.

### WR-13: `verify-flags.sh` only scopes to `src/spu94/*.c` — cli and tests unchecked

`scripts/ci/verify-flags.sh:36`. CLI could compile with `-ffast-math` / `-ffp-contract=on` without CI noticing. Since CLI participates in golden generation (CR-06), non-bit-faithful CLI compile flags would contaminate goldens.

**Fix:** Add a separate pass for `src/cli/*.c` requiring the same flags.

---

## Low-Severity Issues

### WR-14: `fuzz_process.py` hand-typed struct offsets drift without a compile-time anchor

`tests/python/fuzz_process.py:107-111`. If the struct grows and offsets shift earlier, the size-guard still passes but offsets are wrong.

**Fix:** Ship a tiny C helper emitting offsets via `offsetof` that Python picks up.

### WR-15: `check_coverage.py` ctest timeout is per-test, not per-job

`scripts/ci/check_coverage.py:191`. N × 1800s timeout stacks to exceed GH Actions' 360-min cap with no error.

**Fix:** Track cumulative time and bail early.

### WR-16: Witness-diff xcorr alignment ignores silence-case

`scripts/ci/witness_diff.py:510-518`. For silent inputs, `argmax` returns arbitrary index; lag becomes nonsense. Determinism still passes (same garbage twice).

**Fix:** When `|xcorr|.max()` is below some floor, emit `reliability=low` and have downstream gate skip low-reliability rows.

---

## Nits

### NT-01: Three fuzz scripts pick three different `WORK_BUF_SIZE` constants

`fuzz_reverb.py`: 8192. `fuzz_buffer.py`: 8192. `fuzz_process.py`: 256K. Share one via `tests/python/_constants.py`.

### NT-02: `regenerate_goldens.py` uses `os.environ.setdefault` for LC_ALL/TZ

`setdefault` means a pre-set `LC_ALL=en_US.UTF-8` persists. For reproducibility we want *force*, not default.

**Fix:** `os.environ["LC_ALL"] = "C"` unconditionally.

### NT-03: `witness_diff_build.sh` uses `rm -rf "$WORK"` without path validation

Line 35. If `WITNESS_WORK` is set to `/` by mistake, catastrophic.

**Fix:** Add `[[ "$WORK" == *"/.artifacts/lv2-psx-reverb"* ]] || { echo FAIL; exit 2; }` before the rm.

---

## Recommendations for M1 Close-out

**Do not close M1 without resolving CR-01, CR-02, CR-03 at minimum.** The work_buf_size bug the user found is not a local issue — it's an architectural gap. The triad of (silent no-op in C) + (inadequate default in Python) + (weak assertion in harness) is the bit-faithful claim's weakest link. Fix any two of the three and you have defense in depth; fix all three and the bug class is structurally impossible.

**CR-04 (test_init_success accepting 64-byte work buf)** is the codification of the bug — fixing it forces CR-01 to happen, which is the right ordering.

**CR-05 (witness-diff no gate)** should ship with at least a loose gate before M1 closes. The witness diff is the #1 externally-verifiable claim of bit-faithfulness; leaving it as "we print numbers" is leaving the strongest evidence on the table.

**WR-06 (cli malloc possibly escaping grep-guard)** needs verification *right now* — if true, it's a critical finding silently sitting in shipped code.

Everything else is tighten-the-net work that can land in M1.1 or a docs-review pass.
