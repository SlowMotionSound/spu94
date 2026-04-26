---
phase: 01-foundation-fixed-point-math-build-infrastructure
reviewed: 2026-04-18T00:00:00Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - .clang-tidy
  - .github/workflows/ci.yml
  - .gitignore
  - CMakeLists.txt
  - LICENSE
  - cmake/spu94_warnings.cmake
  - docs/DECISIONS.md
  - include/spu94/spu94.h
  - include/spu94/spu94_q15.h
  - scripts/ci/grep-guard.sh
  - scripts/ci/test-grep-guard.sh
  - scripts/ci/verify-flags.sh
  - src/spu94/CMakeLists.txt
  - src/spu94/spu94_placeholder.c
  - tests/CMakeLists.txt
  - tests/unit/CMakeLists.txt
  - tests/unit/q15/CMakeLists.txt
  - tests/unit/q15/test_q15.c
findings:
  critical: 0
  warning: 4
  info: 6
  total: 10
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-04-18
**Depth:** standard
**Files Reviewed:** 18
**Status:** issues_found

## Summary

Phase 1 establishes the C project scaffold, Q15 fixed-point helpers, and the CI
bit-faithfulness gates. Overall quality is high: the Q15 math is demonstrably
correct for all documented edge cases, the compile-time `_Static_assert` guard
for ASR semantics is well-placed, the OBJECT-library + PRIVATE-warnings pattern
correctly prevents flag leakage to downstream consumers, and third-party
actions are pinned to full commit SHAs. The hand-computed Q15 reference table
is auditable and covers the ASR-vs-division distinguisher plus the INT16_MIN²
saturation edge case per ADR-0001.

No critical bugs or security issues were found. The findings below are CI
hardening opportunities (fail-closed robustness, least-privilege permissions,
determinism flag completeness) and one matter of coverage in the test table.
None block the phase from landing, but several warrant a follow-up before the
guard surface accumulates real code to protect.

## Warnings

### WR-01: Missing least-privilege `permissions:` block in CI workflow

**File:** `.github/workflows/ci.yml:12-23`
**Issue:** The workflow does not declare a top-level `permissions:` block.
GitHub Actions defaults to a permissive `GITHUB_TOKEN` scope (read/write to
contents on public repos, or repository-admin defaults depending on org
settings). For a supply-chain-sensitive project that SHA-pins actions
specifically to resist third-party compromise, the defensive posture should
extend to token scope. If any pinned action (or any future action) became
hostile despite the SHA pin review, default write scope would give it broader
blast radius than needed. Phase 1 jobs only need `contents: read`.
**Fix:** Add a top-level `permissions` block:
```yaml
# Below the `on:` block:
permissions:
  contents: read
```
If any future job needs elevated scope (e.g., publishing releases), grant it
at the job level only.

### WR-02: `verify-flags.sh` does not check the `.arguments` form of compile_commands.json

**File:** `scripts/ci/verify-flags.sh:28-36`
**Issue:** The script reads `.command` from every entry in the compilation
database, but CMake may emit either `.command` (single-string form) or
`.arguments` (array form) depending on generator and version. Ninja on recent
CMake can emit `.arguments` exclusively. When that happens, `jq -r '.command'`
yields the literal string `"null"`, the subsequent `grep -qE` fails to match
any required flag, and the script reports EVERY core TU as missing EVERY flag
— loud failure, which is safe — BUT an author who "fixes" this by adding
`// select .command != null` or by ignoring null entries converts a
fail-closed state into a silent false pass. Harden now.
**Fix:** Make the extraction robust to either form:
```bash
# Prefer .command; if null/empty, join .arguments with spaces.
jq -r '
  .[]
  | select(.file | test("src/spu94/.*\\.c$"))
  | [.file,
     (if (.command // "") != "" then .command
      else (.arguments // [] | join(" "))
      end)]
  | @tsv
' "$CDB"
```
Also add an explicit assert that the extracted `$cmd` is non-empty before the
flag-scan loop, to catch future schema drift as a loud error rather than a
per-flag cascade.

### WR-03: `grep-guard.sh` does not strip C/C++ comments before scanning

**File:** `scripts/ci/grep-guard.sh:29,38`
**Issue:** The guard greps raw file contents, so a comment mentioning a
forbidden token (e.g., `/* do not call malloc here */` or `// uses float
arithmetic intentionally at the clip site`) would fail the guard. Today's
tree is clean (spu94_placeholder.c deliberately phrases its comments to avoid
the forbidden words), but this creates a perverse incentive: authors writing
real documentation in-code will be pushed to paraphrase around the guard
rather than be plain. Worse, ADR-0002 / ADR-0003 in later phases will want to
reference by name the wrap/saturate/negation behaviors that document hardware
quirks — some of those docstrings may naturally mention `float` or similar
tokens in contrast.
**Fix:** Either (a) pre-process files through `gcc -fpreprocessed -dD -E -P`
or a small awk comment-stripper before grepping, OR (b) document that comments
are in scope and add an explicit allowlist mechanism (e.g., a trailing
`/* grep-guard: allow-mention */` marker that the guard's second pass
subtracts). Option (a) is more robust; option (b) keeps the guard simple.
Deferrable, but decide before Phase 3's core CORE-02 mix-bus clip lands, when
natural prose about saturation will proliferate.

### WR-04: `q15_add_sat` has no unit coverage for mixed-sign non-saturating cases beyond a single pair

**File:** `tests/unit/q15/test_q15.c:82-90`
**Issue:** The `q15_add_sat` table asserts zero, positive saturation, negative
saturation, and one mixed-sign case (`INT16_MAX + INT16_MIN == -1`). It does
NOT exercise: (a) near-boundary add that is within range but one away from
saturation (`INT16_MAX - 1, 1` → `INT16_MAX`), (b) negative non-saturating
cases (`-50, -50` → `-100`), (c) any result near `INT16_MIN` that is *just*
reachable without saturating (`INT16_MIN + 1, 0` → `INT16_MIN + 1`). The
current coverage cannot distinguish, for example, a buggy implementation that
saturates one step too aggressively (returns `INT16_MAX - 1` at the true max)
from a correct one.
**Fix:** Convert `test_q15_add_sat_table` to the same
case-struct-with-message pattern as `mul_cases` and add rows:
```c
{ INT16_MAX - 1,  1,            INT16_MAX,        "one-short-of-sat positive" },
{ 1,              INT16_MAX - 1, INT16_MAX,        "commutes" },
{ INT16_MIN + 1, -1,            INT16_MIN,        "one-short-of-sat negative (hits exactly)" },
{ -50,           -50,           -100,             "small negative, no saturation" },
{ INT16_MIN,      INT16_MAX,    -1,               "full mixed already covered; keep" },
```
Low risk to land now — the helper is simple — but this is the last chance to
pin down behavior before downstream code (Phase 3 tap sums, Phase 4 output
mix) depends on it.

## Info

### IN-01: UBSan CFLAGS `-O1` coexists with `CMAKE_BUILD_TYPE=Debug` default `-O0`

**File:** `.github/workflows/ci.yml:122-127`
**Issue:** The UBSan job sets `CFLAGS='-fsanitize=undefined ... -g -O1'` and
`-DCMAKE_BUILD_TYPE=Debug`. CMake Debug prepends its own flags (typically
`-g -O0`). The final compile line contains both `-O0` (from
`CMAKE_C_FLAGS_DEBUG`) and `-O1` (from the env CFLAGS), order-dependent.
Last-one-wins gives `-O1` in practice with both gcc and clang, but this is
brittle — a future CMake version or a CI runner image update could reorder
them.
**Fix:** Either drop the `-DCMAKE_BUILD_TYPE=Debug` from the UBSan configure
step (the env CFLAGS already carry `-g -O1`) or drop `-O1` from CFLAGS and
let Debug's `-O0` stand. UBSan docs recommend `-O1` so `-O0` is wasteful;
preferred fix is to remove the `-DCMAKE_BUILD_TYPE=Debug` on that job.

### IN-02: `verify-flags.sh` does not guard against `-Ofast` or `-ffast-math` appearing

**File:** `scripts/ci/verify-flags.sh:11-12`
**Issue:** The REQUIRED list positively asserts the presence of
`-ffp-contract=off`, `-fno-fast-math`, `-Werror`. It does not negatively
assert the absence of `-Ofast` (which implies `-ffast-math` and silently
re-enables FMA contraction on some compilers) or a literal `-ffast-math`. If
a future author adds `-Ofast` to a target, verify-flags passes because the
required flags are still present, yet determinism is broken.
**Fix:** After the REQUIRED-present check, add a FORBIDDEN-absent check:
```bash
FORBIDDEN=('-Ofast' '-ffast-math' '-ffp-contract=fast' '-ffp-contract=on')
for flag in "${FORBIDDEN[@]}"; do
    if printf '%s' "$cmd" | grep -qE "(^| )${flag}( |\$)"; then
        echo "ERROR [verify-flags]: $src_file has forbidden flag: $flag"
        fail=1
    fi
done
```

### IN-03: `-fwrapv` and `-fno-strict-aliasing` are not among the core-TU flags

**File:** `cmake/spu94_warnings.cmake:22-25`
**Issue:** For a bit-faithful hardware simulator that will legitimately model
defined-wrap semantics at specific sites (ADR-0002 vIIR, future CORE-02 hard
clip), `-fwrapv` converts signed-overflow UB into two's-complement wrap,
which is what the hardware does anyway. Without `-fwrapv`, the compiler is
free to miscompile UB-triggering code under optimization (though Phase 1's
UBSan job will catch any *exercised* UB, it won't catch it on non-Debug
builds). `-fno-strict-aliasing` is belt-and-suspenders for the eventual
type-punned register file access.
**Fix:** Consider adding both flags to the INTERFACE target. Caveat: ADR-0003
deliberately takes a surgical `SPU94_NO_SANITIZE_INTEGER` approach instead;
adding `-fwrapv` globally is a different philosophy (define-the-behavior-
globally vs annotate-each-site). Raise as an ADR before landing to preserve
the Phase 1 discipline that blanket UB policies require an ADR.

### IN-04: `.clang-tidy` `HeaderFilterRegex` uses anchored capture; tests headers (`tests/unit/**`) not included

**File:** `.clang-tidy:23`
**Issue:** `HeaderFilterRegex: '^(include|src)/spu94/.*\.h$'` correctly scopes
to core library headers only, matching the CI invocation which also only
passes core `.c`/`.h` files to clang-tidy. This is consistent, but note that
vendored Unity's `unity.h` is still excluded by the file scope (not by this
regex), which is the intent. No action needed; documenting that the regex
alone would NOT be sufficient if a future author changed the find command to
scan tests — both scoping mechanisms must move together.
**Fix:** No code change. Consider a one-line comment in the `Run clang-tidy`
CI step linking the file-scope `find` to the `HeaderFilterRegex` so an author
changing one is prompted to change the other.

### IN-05: `spu94_placeholder.c` `spu94_internal_version` has external linkage with no `static`

**File:** `src/spu94/spu94_placeholder.c:13`
**Issue:** `const int32_t spu94_internal_version = 0;` has external linkage
and will be exported from the shared library as a visible symbol
(`nm libspu94.so | grep spu94_internal`). The comment says "internal use
only" but the symbol is reachable by any consumer of `libspu94.so` via
`extern const int32_t spu94_internal_version;` declaration. Phase 2 will
replace this, but if any downstream (Python wheel in Phase 6, plugin in M4)
snapshots the symbol list, this becomes a de-facto ABI surface.
**Fix:** Mark `static`:
```c
static const int32_t spu94_internal_version = 0;
/* Reference it to avoid unused-variable warnings under -Wunused. */
const int32_t *spu94__internal_version_ptr(void) { return &spu94_internal_version; }
```
Or simply delete the token entirely — the file being non-empty is what
matters for the library to link, and an empty TU with only the header
include would also compile.

### IN-06: `grep-guard.sh` `long`-pattern coarseness is acknowledged but undocumented in tests

**File:** `scripts/ci/grep-guard.sh:34-41`, `scripts/ci/test-grep-guard.sh:45-54`
**Issue:** The guard's `long` pass uses `grep -v 'long long'` to subtract
`long long` matches, with a comment noting that a line containing BOTH
`long long` AND unqualified `long` would be incorrectly allowed. The test
script has no fixture case exercising this known weakness, so a future author
who tightens the regex won't have a regression fixture to confirm the new
behavior — nor is there a fixture to confirm the current known-coarse
behavior is what's intended.
**Fix:** Add a fixture case to `test-grep-guard.sh` documenting current
behavior:
```bash
# CASE 7: known-coarse — line with both 'long long' AND unqualified 'long'
# is ACCEPTED today (documented limitation). Expect exit 0.
run_case "known-coarse: long long + long same line" 0 \
    "src/edge.c" 'long long ll; long n;'
```
This turns the comment into an executable spec. If a future fix tightens the
regex, this test will break and force an explicit policy change.

---

_Reviewed: 2026-04-18_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
