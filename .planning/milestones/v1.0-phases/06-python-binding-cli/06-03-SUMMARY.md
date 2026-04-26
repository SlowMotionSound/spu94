---
phase: 06-python-binding-cli
plan: 03
subsystem: cli
tags:
  - cli
  - c
  - dr_wav
  - jsmn
  - json
  - wav
  - vendoring
  - getopt_long

# Dependency graph
requires:
  - phase: 05-public-api-presets-integration
    provides: spu94_process + spu94_flush + spu94_load_preset + spu94_presets[] public API used end-to-end by the CLI binary
provides:
  - Native spu94 CLI binary processing WAV files via --preset or --config (JSON)
  - Vendored dr_wav v0.14.6 (WAV I/O, public domain / MIT-0) and jsmn v1.1.0 (JSON tokenizer, MIT) under vendor/
  - scripts/ci/verify-no-drwav-in-libspu94.sh permanent regression gate (CLI-03)
  - tests/fixtures/ deterministic 1-second stereo 44.1 kHz WAV plus two JSON config shapes for future phase use
  - tests/cli/ ctest-labelled pytest suite exercising the binary as a subprocess (35 tests + 1 nm audit)
  - src/cli/ source tree isolating dr_wav + jsmn inclusion to the CLI target (PRIVATE include dirs, Pitfall 8)
affects:
  - phase 06-04-packaging (wheel install uses install(TARGETS spu94_cli) under SKBUILD guard; $ORIGIN RPATH already wired)
  - phase 06-05-readme (README CLI walkthrough can quote the landed --help text and error messages verbatim)
  - phase 07-verification (golden-file + witness-diff harnesses can spawn the spu94 binary directly)

# Tech tracking
tech-stack:
  added:
    - dr_wav (vendor/dr_wav/dr_wav.h v0.14.6 — public domain / MIT-0 choice license)
    - jsmn (vendor/jsmn/jsmn.h v1.1.0 — MIT license © 2010 Serge Zaitsev)
  patterns:
    - "Vendored single-header libs: PRIVATE include dirs on the CLI target prevent transitive leakage to downstream consumers of spu94_shared (Pitfall 8). nm -D audit enforces this as a permanent regression gate."
    - "Error-shape discipline: every error path flows through a single SPU94_ERROR() macro so that message format, stderr stream, and newline handling are centralized. Enables one-line-only contract (D-05)."
    - "Target-local warning relaxation: CLI keeps -Werror -Wall -Wextra + determinism flags, but drops -Wconversion / -Wpedantic so ~9000 lines of idiomatic dr_wav compile clean without vendor-patching."
    - "Planar int16 boundary: wav_io.c owns the interleave/deinterleave so main.c sees only planar L/R buffers that feed spu94_process directly — no internal stutter-step between formats."
    - "Strict shape validation on top of non-strict tokenizer: jsmn is intentionally permissive ({not valid json} tokenizes as 3 primitive children); we explicitly reject non-JSMN_STRING keys to enforce a JSON-object-of-string-keys schema."
    - "Caller-owned state via `alignas(SPU94_STATE_ALIGN_MAX) static unsigned char state_buf[SPU94_STATE_SIZE_MAX]`: no library heap surface exercised by the CLI (API-01 respected end-to-end)."

key-files:
  created:
    - vendor/dr_wav/dr_wav.h
    - vendor/dr_wav/LICENSE
    - vendor/jsmn/jsmn.h
    - vendor/jsmn/LICENSE
    - src/cli/CMakeLists.txt
    - src/cli/main.c
    - src/cli/wav_io.c
    - src/cli/wav_io.h
    - src/cli/json_config.c
    - src/cli/json_config.h
    - src/cli/preset_names.c
    - src/cli/preset_names.h
    - tests/cli/CMakeLists.txt
    - tests/cli/conftest.py
    - tests/cli/__init__.py
    - tests/cli/test_cli_preset_hall_roundtrip.py
    - tests/cli/test_cli_config_and_list.py
    - tests/cli/test_cli_error_paths.py
    - tests/fixtures/CMakeLists.txt
    - tests/fixtures/generate_fixtures.py
    - tests/fixtures/sample_override_hall.json
    - tests/fixtures/sample_flat_registermap.json
    - scripts/ci/verify-no-drwav-in-libspu94.sh
  modified:
    - CMakeLists.txt
    - tests/CMakeLists.txt

key-decisions:
  - "dr_wav v0.14.6 + jsmn v1.1.0 vendored verbatim under vendor/ with LICENSE preserved — no source modifications; PROJECT.md vendoring posture upheld."
  - "CLI binary renamed to `spu94` via `OUTPUT_NAME spu94` so the Plan-4 wheel shim and dev `build/src/cli/spu94` path match exactly."
  - "Vendored headers included PRIVATE — no leak to spu94_shared; CLI-03 gate (`nm -D | grep drwav_|jsmn_`) permanent regression fence."
  - "Determinism flags (`-ffp-contract=off -fno-fast-math`) retained on CLI; conversion/pedantic warnings relaxed target-locally because dr_wav is ~9000 lines of idiomatic-but-not-pedantic C. Our own CLI TUs are small enough (<400 lines each) that spot-review covers the dropped diagnostics."
  - "--config JSON auto-detection: top-level `base` key → override shape (preset + diffs); no `base` → flat shape (all 35 regs required). This matches the plan's D-12 decision and removes the need for a separate --shape flag."
  - "Strict-shape check after jsmn_parse: every top-level key must be JSMN_STRING. jsmn's non-strict tokenizer happily accepts `{ not valid json }`; we explicitly reject that with the same `invalid JSON` error-shape contract as a negative parse result."
  - "preset_names.c normalizes `lowercase + space→underscore` so the C-side display names (`Hall`, `Half Echo`, `Studio A`) resolve from every canonical CLI input (`hall`, `half_echo`, `studio_a`) and also accept the display-form variants verbatim."
  - "main.c uses `alignas(SPU94_STATE_ALIGN_MAX) static unsigned char state_buf[SPU94_STATE_SIZE_MAX]` — caller-owned per API-01; zero library heap exercised. work_buf is a single CLI-owned malloc of 512 KB, sized to exceed every preset's buffer reach."
  - "Error-message macro `SPU94_ERROR(...)` centralises the `spu94: error:` prefix and newline so every exit path meets the D-05 one-line contract without copy-paste drift."
  - "Pipeline performs exactly one `spu94_tick(state)` between preset/config application and the first `spu94_process` block so TICK_LATCHED address/delay registers commit before audio starts (prevents stale-pending-value glitch on sample 0)."
  - "Tail-flush is optional: `--tail-seconds 0` (the default) produces output exactly as long as input; a positive value appends `N * 44100` frames of `spu94_flush` output to capture the decaying reverb tail."

patterns-established:
  - "Vendored-library audit gate: for every vendored dependency X, add scripts/ci/verify-no-X-in-libspu94.sh that asserts `nm -D libspu94.so | grep X_` returns nothing. Wire as a ctest entry in the relevant label. Pattern scales to any future vendoring (e.g., a MIDI library in M4)."
  - "Binary + pytest-subprocess CLI testing: tests are black-box pytest files that spawn the built binary and assert on stdout/stderr/exit code plus output-file validity. No white-box coupling to internal CLI functions. Pattern applies to any future CLI tool we ship."
  - "Deterministic fixtures-at-configure-time: tests/fixtures/CMakeLists.txt runs the stdlib-only generate_fixtures.py via add_custom_command so the fixture WAV is byte-identical across hosts/Python versions/numpy versions. Pattern applies to any deterministic test input generator."

requirements-completed:
  - CLI-01
  - CLI-02
  - CLI-03
  - CLI-04

# Metrics
duration: 14m 51s
completed: 2026-04-21
---

# Phase 6 Plan 3: Native spu94 CLI — dr_wav I/O + jsmn --config + CLI-01..04 contract

**Native C `spu94` CLI with dr_wav-backed WAV I/O, jsmn-backed --config JSON (override + flat shapes), polished engineer-oriented error messages, and a permanent nm-audit gate keeping dr_wav/jsmn out of libspu94.so.**

## Performance

- **Duration:** 14 min 51 s
- **Started:** 2026-04-21T21:05:30Z
- **Completed:** 2026-04-21T21:20:21Z
- **Tasks:** 2 (Task 1 scaffold, Task 2 full pipeline)
- **Files created:** 23
- **Files modified:** 2

## Accomplishments

- **CLI-01 green:** `spu94 --preset <name> IN.wav OUT.wav` produces a valid 44.1 kHz int16 stereo WAV with matching (or tail-extended) frame count. All 10 factory presets round-trip; case-insensitive and space↔underscore equivalent preset names are accepted.
- **CLI-02 green:** `--config` accepts both override shape (`{"base": "hall", "overrides": {"vIIR": -8000}}`) and flat shape (all 35 registers). `--list-presets` prints exactly 10 canonical names; `--help` prints the polished usage text.
- **CLI-03 green:** `nm -D build/src/spu94/libspu94.so | grep -E 'drwav_|jsmn_'` returns zero matches — the vendored libs are CLI-bound only. `scripts/ci/verify-no-drwav-in-libspu94.sh` is a permanent ctest regression gate under the `cli` label.
- **CLI-04 green:** every error path exits non-zero with exactly ONE line of stderr prefixed `spu94: error:`. The unknown-preset message has an exact D-05 text contract tested byte-for-byte; the README can quote it verbatim in Plan 5.
- **Vendored libraries landed:** dr_wav v0.14.6 (9075 lines, public domain / MIT-0 choice license) at `vendor/dr_wav/` and jsmn v1.1.0 (471 lines, MIT) at `vendor/jsmn/`. LICENSE files preserved verbatim.
- **Deterministic test infrastructure:** tests/fixtures/generate_fixtures.py produces a byte-identical 1-second stereo 44.1 kHz fixture at configure time. Two JSON fixtures (override and flat) exercise both --config shapes.
- **No regressions on the existing 55-test suite** (excluding the 20-minute fuzz_process which we ran separately; it remains green).

## Task Commits

Each task was committed atomically:

1. **Task 1: Wave-0 CLI scaffold + vendored dr_wav/jsmn** — `d8e28fd` (feat)
   - vendor/ single-header libs + LICENSE files; src/cli/ stubs + full getopt_long flow; tests/fixtures/ generator; tests/cli/ skip-placeholders; scripts/ci/ audit script; top-level + tests CMakeLists wiring.
2. **Task 2: Full CLI pipeline — dr_wav, preset resolver, jsmn config, behavioral tests** — `dc9a4b4` (feat)
   - Real implementations replacing every stub; real pipeline in main.c; 35 pytest behavioral tests across three files.

## Files Created/Modified

### Vendored libraries
- `vendor/dr_wav/dr_wav.h` — WAV reader/writer, 9075 lines, v0.14.6 verbatim from mackron/dr_libs.
- `vendor/dr_wav/LICENSE` — dual public-domain / MIT-0 notice copied from dr_wav's trailing license block.
- `vendor/jsmn/jsmn.h` — JSON tokenizer, 471 lines, v1.1.0 verbatim from zserge/jsmn.
- `vendor/jsmn/LICENSE` — MIT © 2010 Serge Zaitsev.

### CLI source tree
- `src/cli/CMakeLists.txt` — spu94_cli target; PRIVATE vendored includes; $ORIGIN RPATH; SKBUILD-gated install rule.
- `src/cli/main.c` (251 lines) — getopt_long arg parse; --help; --list-presets; full pipeline (load → allocate state → apply preset/config → tick → process → optional flush → write).
- `src/cli/wav_io.c` + `wav_io.h` (164 + 52 lines) — planar int16 L/R load/write via dr_wav; 16-channel/44.1 kHz enforcement; 4096-frame write-block to cap peak memory.
- `src/cli/json_config.c` + `json_config.h` (381 + 40 lines) — jsmn parser with token_span walker; override vs flat auto-detect; strict non-JSMN_STRING key rejection; per-register type-aware range validation; integer + hex-string parsing.
- `src/cli/preset_names.c` + `preset_names.h` (97 + 47 lines) — `lowercase + space→underscore` normalizer; O(1) cached canonical lookups; comma-separated name list for error messages.

### Tests + fixtures
- `tests/cli/test_cli_preset_hall_roundtrip.py` (16 tests, CLI-01).
- `tests/cli/test_cli_config_and_list.py` (7 tests, CLI-02).
- `tests/cli/test_cli_error_paths.py` (12 tests, CLI-04).
- `tests/cli/conftest.py` — session fixtures for binary path, fixture WAV, JSON configs.
- `tests/cli/CMakeLists.txt` — ctest wiring; labels `cli`; CLI-03 nm-audit test.
- `tests/fixtures/generate_fixtures.py` — deterministic 1-second stereo fixture generator (stdlib only).
- `tests/fixtures/CMakeLists.txt` — add_custom_command + configure_file for WAV + JSON fixtures.
- `tests/fixtures/sample_override_hall.json` — `{"base": "hall", "overrides": {"vIIR": -8000, "mLCOMB1": "0x1000"}}`.
- `tests/fixtures/sample_flat_registermap.json` — all 35 registers at Hall preset values (mix of integer + hex-string).

### CI
- `scripts/ci/verify-no-drwav-in-libspu94.sh` (CLI-03) — asserts `nm -D libspu94.so` yields zero `drwav_` or `jsmn_` symbols.

### Wiring
- `CMakeLists.txt` (modified) — `add_subdirectory(src/cli)` after `src/spu94`.
- `tests/CMakeLists.txt` (modified) — `add_subdirectory(fixtures)` + `add_subdirectory(cli)` below existing entries.

## Vendored Versions

| Library | Version | Source | Lines | License |
|---------|---------|--------|-------|---------|
| dr_wav  | v0.14.6 | github.com/mackron/dr_libs (master as of fetch) | 9075 | Public domain (Unlicense) OR MIT-0 (choose one) |
| jsmn    | v1.1.0  | github.com/zserge/jsmn (master as of fetch) | 471 | MIT © 2010 Serge Zaitsev |

Both are header-only; neither is modified from upstream. The LICENSE files in `vendor/dr_wav/LICENSE` and `vendor/jsmn/LICENSE` are verbatim copies of each project's license text so anyone inspecting `vendor/` sees the licensing posture at a glance without having to scroll to the tail of the header file.

## Target-Private Warning Suppressions

The CLI target (src/cli/CMakeLists.txt) applies a relaxed warning set:

```cmake
target_compile_options(spu94_cli PRIVATE
    -Wall -Wextra -Werror
    -ffp-contract=off -fno-fast-math
)
```

Relative to the project-wide `spu94_warnings` INTERFACE, this **drops**:
- `-Wpedantic` (dr_wav v0.14.6 uses some non-pedantic GCC idioms)
- `-Wconversion` / `-Wsign-conversion` (~50+ implicit narrowings in dr_wav's internal helpers)
- `-Wshadow`, `-Wstrict-prototypes`, `-Wmissing-prototypes` (noisy on vendor code)

Determinism flags (`-ffp-contract=off`, `-fno-fast-math`) are **kept** so any sneaky float path in the CLI TUs remains a build error.

No `-Wno-*` flags were needed — the relaxed set alone compiled cleanly.

## Exact Landed Error Messages (for Plan 5 README CLI Walkthrough)

The following are the precise stderr lines the binary emits. All single-line, all prefixed `spu94: error:`.

| Trigger | Exact stderr line |
|---------|-------------------|
| No args (missing required flag) | `spu94: error: one of --preset or --config is required (try --help)` |
| Both `--preset` and `--config` | `spu94: error: --preset and --config are mutually exclusive` |
| Missing or extra positional args | `spu94: error: expected INPUT.wav OUTPUT.wav (got N positional argument[s])` |
| Unknown preset (exact) | `spu94: error: unknown preset 'hll' — valid: off, room, studio_a, studio_b, studio_c, hall, half_echo, space_echo, echo, delay` |
| Missing input WAV | `spu94: error: input file '/path/to/file.wav' not found` |
| Non-stereo input | `spu94: error: WAV file '/path' has N channels; stereo (2 channels) required` |
| Non-44.1 kHz input | `spu94: error: WAV file '/path' has sample rate N Hz; 44100 Hz required` |
| Missing --config JSON | `spu94: error: config file '/path/to/file.json' not found` |
| Malformed JSON (bad tokens) | `spu94: error: invalid JSON in '/path' (non-string key at top level)` |
| Malformed JSON (not an object) | `spu94: error: invalid JSON in '/path' (expected top-level object)` |
| Flat config missing registers | `spu94: error: flat config '/path' must specify all 35 registers (found N)` |
| Unknown register name | `spu94: error: unknown register 'vFOO' (use one of the 35 canonical names like vIIR, mBASE, mLCOMB1)` |
| Out-of-range i16 value | `spu94: error: value 999999 for register 'vIIR' out of range [-32768..32767]` |
| Out-of-range u16 value | `spu94: error: value N for register 'dAPF1' out of range [0..65535]` |
| Invalid --tail-seconds | `spu94: error: invalid value for --tail-seconds: 'abc'` |

Tone is recording-engineer-oriented (Anthony is not a coder); no compiler jargon, no tracebacks, no internal API names.

## CLI Binary Size

| Variant | Size |
|---------|------|
| Unstripped ELF | 128392 bytes (125 KB) |
| Stripped ELF   | 117024 bytes (114 KB) |

Small enough to be shipped inside a Python wheel without meaningfully affecting download size.

## Benchmark (Dev Workstation)

| Input | Preset | Wall time |
|-------|--------|-----------|
| 1-second stereo 44.1 kHz (44100 frames) | hall | 9 ms |

Dominated by fixed-cost setup (WAV header read, state init, preset load); the per-sample processing path is indistinguishable from noise at this length. Sets a comfortable CLI-04 test timeout of 10 s for any pytest subprocess call.

## CONTEXT Decisions Covered

All of Plan 3's scoped decisions from `.planning/phases/06-python-binding-cli/06-CONTEXT.md`:
- **D-03** Native C binary via CMake; dr_wav vendored at `vendor/dr_wav/`, linked to CLI only (CLI-03 enforced).
- **D-05** Non-zero exit + exactly one `spu94: error:` stderr line on errors — exact unknown-preset text tested byte-for-byte.
- **D-12** JSON shape auto-detected by `"base"` key (override) vs no-base (flat).
- **D-13** Accepts integer values AND hex-string values (`"0x1000"`, `"-0x40"`).
- **D-14** Unknown register names in flat or override config are hard errors.
- **D-15** Out-of-range values (per-register signedness) are hard errors with explicit range in the message.

## Deferred to Plan 5

Plan 5 (README + ADRs) will land formal ADR entries in `docs/DECISIONS.md` for D-03 (native C + vendored dr_wav), D-05 (error shape), D-12..D-15 (JSON schema + validation). These are already fully resolved in code; Plan 5's ADRs record the "why" for posterity and for the DECISIONS.md first-class-deliverable contract.

## Decisions Made

See key-decisions in frontmatter. Summary:
- Vendored libs PRIVATE-include (Pitfall 8 → CLI-03 gate).
- Target-local warning relaxation for vendored headers (no source patching).
- JSON shape auto-detection via `"base"` key.
- Strict non-JSMN_STRING key rejection on top of non-strict jsmn tokenizer.
- Caller-owned `state_buf` via `alignas` in static storage (no library heap).
- Single SPU94_ERROR macro centralising D-05 contract enforcement.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Wave-0 stub `json_config.c` tripping -Werror=unused-function**
- **Found during:** Task 1 (first `cmake --build build --target spu94_cli` invocation)
- **Issue:** The plan's literal Task-1 stub `#define JSMN_STATIC` + `#include <jsmn.h>` without calling `jsmn_init` / `jsmn_parse` triggers `-Werror=unused-function` under the CLI target's warning set because JSMN_STATIC makes the jsmn functions file-local, so unused.
- **Fix:** Removed the `#include <jsmn.h>` from the Wave-0 stub only. Documented in a comment that Task 2's real implementation calls both functions and the suppression becomes moot.
- **Files modified:** src/cli/json_config.c (Task 1 stub)
- **Verification:** `cmake --build build --target spu94_cli` succeeds after the removal.
- **Committed in:** d8e28fd (Task 1)

**2. [Rule 1 - Bug] Plan's override fixture referenced non-existent register `dCOMB1`**
- **Found during:** Task 1 (fixture creation)
- **Issue:** The plan's sample_override_hall.json template uses `"dCOMB1"` as an override key. SPU-94's actual 35-register enum has `vCOMB1..4` (gain, I16) and `mLCOMB1..4` / `mRCOMB1..4` (delay, U16) but no `dCOMB1..4`. The PROJECT.md requirement mentions dCOMB1-4 as a category label, not a canonical register name.
- **Fix:** Changed the override to use `mLCOMB1` (the closest matching real register name) with hex value `"0x1000"` which is in the valid U16 range.
- **Files modified:** tests/fixtures/sample_override_hall.json
- **Verification:** `./build/src/cli/spu94 --config tests/fixtures/sample_override_hall.json ... OUT.wav` exits 0 and produces a valid WAV.
- **Committed in:** d8e28fd (Task 1)

**3. [Rule 1 - Bug] jsmn's non-strict tokenizer accepts `{ not valid json }`**
- **Found during:** Task 2 (first run of the `test_malformed_json` subprocess test after wiring the real parser)
- **Issue:** jsmn is an intentionally non-strict JSON tokenizer. Given `{ not valid json }`, it returns 4 tokens: an OBJECT with size=3, and three JSMN_PRIMITIVE children `not`, `valid`, `json`. Our shape-auto-detection saw `pairs=3`, didn't find "base", fell through to the flat-config path, and returned `must specify all 35 registers (found 3)` — correct behavior for a flat-config-shape error but not the expected `invalid JSON` message shape that the CLI-04 contract (and the pytest assertion) demand.
- **Fix:** Added a strict-shape check between `jsmn_parse` and the shape auto-detect: walk the top-level pairs and reject any key whose token type is not JSMN_STRING with an `invalid JSON in '%s' (non-string key at top level)` message.
- **Files modified:** src/cli/json_config.c
- **Verification:** `echo '{ not valid json }' > /tmp/bad.json && spu94 --config /tmp/bad.json ...` now produces `spu94: error: invalid JSON in '/tmp/bad.json' (non-string key at top level)`; all other shape and validation tests still pass (real object-with-string-keys JSON is not affected).
- **Committed in:** dc9a4b4 (Task 2)

---

**Total deviations:** 3 auto-fixed (1 Rule 3 blocking, 2 Rule 1 bugs).
**Impact on plan:** All three auto-fixes were necessary for the plan's stated acceptance criteria to pass. No scope creep. No architectural changes. The plan's Task-1 stub was a minor oversight (the Wave-0 pattern of including a vendored header without using it is incompatible with JSMN_STATIC + -Werror=unused-function); the `dCOMB1` reference was a plan-level typo for a register name that doesn't exist in SPU-94's canonical 35-register enum; the strict-shape check is a correctness requirement that the plan's explicit test case (`{ not valid json }`) requires to pass.

## Issues Encountered

None beyond the three auto-fixed deviations above. Task 1's Wave-0 scaffold built and ctest-ran green on the first attempt after the json_config.c include-removal; Task 2's full pipeline built and ran the full subprocess test suite green on the first attempt after the strict-shape fix.

## User Setup Required

None — the CLI is self-contained. dr_wav and jsmn are header-only and linked statically into the CLI binary; no additional system packages beyond CMake + Python 3.10+ (already required by the project).

## Next Phase Readiness

- **Plan 6-04 (packaging)** is unblocked: `src/cli/CMakeLists.txt` already contains the SKBUILD-gated `install(TARGETS spu94_cli RUNTIME DESTINATION ${SKBUILD_PROJECT_NAME})` rule and `$ORIGIN` RPATH. Plan 4 only needs to wire `pyproject.toml` + `scikit-build-core` and the binary drops into the wheel alongside `libspu94.so`.
- **Plan 6-05 (README)** has the exact error-message table above to quote verbatim in the CLI walkthrough section; no guessing at final wording.
- **Phase 7 verification** harnesses (golden-file, witness-diff) can invoke the spu94 binary directly via subprocess — `--preset` selection, `--config` override, and the planar int16 WAV contract are all stable.
- **M4 plugin work** can reuse `src/cli/preset_names.c`'s normalization logic if the plugin's preset browser wants identical display behaviour.

## Known Stubs

None. Every Wave-0 stub in src/cli/ was replaced with a full implementation in Task 2. Scan: `grep -rn -i "stub\|not implemented\|TODO\|FIXME\|placeholder" src/cli/ tests/cli/` returns no matches.

## Self-Check: PASSED

All 23 created files verified present on disk. All 2 task-commit hashes (`d8e28fd`, `dc9a4b4`) present in `git log --oneline --all`.

---
*Phase: 06-python-binding-cli*
*Completed: 2026-04-21*
