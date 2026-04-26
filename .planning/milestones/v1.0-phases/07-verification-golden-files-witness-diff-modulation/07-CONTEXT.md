# Phase 7: Verification — Golden Files, Witness Diff, Modulation - Context

**Gathered:** 2026-04-23
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 7 delivers **the proof layer** for SPU-94's bit-faithful accuracy claim. Phases 1–6 built the algorithm, the public C API, the Python binding, and the CLI. Phase 7 is where the project earns its evidence — a spec-conformance coverage map (every nocash-documented reverb behavior → at least one passing test), a witness-diff harness against lv2-psx-reverb output, per-preset golden-file regression snapshots (byte-identical across a Docker-pinned CI container and the host dev environment), a modulation harness that exercises all 35 registers during live audio, a pytest-benchmark regression guard, plus the polished `docs/LEVERS-CATALOG.md` (the M4-lever planning document) and expanded `docs/BIBLIOGRAPHY.md`.

**In scope:**
- `docs/COVERAGE.md` — single machine-parseable file with three sections (per-register, per-behavior, per-spec-paragraph). CI-enforced: each row names a test; the check parses the file, verifies the named test exists and passes, fails the build on any gap.
- Witness-diff harness — builds lv2-psx-reverb fresh each run at a pinned version, runs the standard input set through it via an LV2 host, computes split-band aligned-RMS divergence per preset (low-band ≤ 10 kHz gate-candidate; high-band > 10 kHz informational). Phase 7 ships the *measurement* harness; the tolerance *policy* (what divergence is "passing") lands as a follow-up ADR after we see real numbers.
- Golden-file regression tests — 10 presets × 5 standard inputs = 50 `.wav` files + 50 `.sha256` sidecars under `tests/golden/<preset>/<input>.wav`. Regeneration requires an ADR.
- Standard input set: impulse, white noise, 1 kHz sine, silence, frequency sweep. Exact parameters (lengths, amplitudes, noise seed, sweep range) are Claude's discretion; they need only be deterministic and committed.
- Docker-pinned reproducibility — new GitHub CI job spins up a pinned Docker container (base image pinned by digest; no per-package explicit pins), builds SPU-94 inside it, regenerates the 50 goldens, and SHA-256-diffs against the committed ones. Host dev workstation is the other side of the comparison.
- Modulation harness (`tests/python/modulation_harness.py` or equivalent) — exercises every one of the 35 registers in three modes (sine, frequency sweep, random walk) during live `spu94_process` audio. Gate = stability + determinism at every modulation rate from slow through audio-rate. Zipper / stepped / time-smeared audio at high modulation rates is *character*, catalogued in LEVERS-CATALOG.md, not failed on.
- `docs/LEVERS-CATALOG.md` — one row per register. Mechanical columns (modulation cost, expected zipper behavior) populated by the harness directly. Subjective columns (musical role, suggested M4 lever grouping) hand-written.
- Benchmark harness — pytest-benchmark running `spu94_process` across presets. Hot-path allocation signal (heap call anywhere in the `spu94_process` call tree, detected via extension of Phase 5's strace-based `test_no_syscalls` approach) is a hard CI fail. Timing numbers are reported to a CI artifact/log, not gated.
- `docs/BIBLIOGRAPHY.md` — additive entries for new Phase 7 sources (lv2-psx-reverb witness, any new nocash sections used) + cleanup pass (cross-reference every DECISIONS.md claim to a `BIB-xxx` pointer; cluster by primary/secondary/witness tier; polish tone to match README).
- ADR landings in `docs/DECISIONS.md` — planner discretion for exact count and split; the reinterpretation of ROADMAP SC-4's "zipper-free on gain-type registers" wording is one of them.

**Explicitly NOT in scope:**
- Per-preset divergence tolerance *values* for the witness-diff harness — deferred. Phase 7 measures and reports; the pass/fail ADR follows once actual numbers are on the table.
- Parameter smoothing for smooth-under-audio-rate-modulation — M4 (plugin smoothing layer) / M5 (Eurorack CV smoothing layer) work. SPU-94 core stays bit-faithful to the PS1 hardware, which has no smoothing.
- M4 musical lever names ("Room Size", "Pre Delay", "Decay", "Diffusion", "Damping") — M4 work. Phase 7's LEVERS-CATALOG suggests *groupings* per register, not the final lever UI.
- Cross-AI review, security review, UI audit — Phase 7 has no UI and no external-untrusted-input surface.
- MCU cross-compile validation — Phase 8.
- JUCE / VST3 / AU / LV2 plugin wrapper — Milestone 4.
- Hardware validation via original PSX — Milestone 5.

</domain>

<decisions>
## Implementation Decisions

### Area A — Spec-conformance coverage map (TEST-01)

- **D-01: Single file, three sections.** `docs/COVERAGE.md` has three sections in one file:
  - **Per-register** — 35 rows, one per SPU register (`vIIR`, `vWALL`, `dCOMB1`, …, `mBASE`, `vLOUT`, `vROUT`). Each row names the test(s) that touch that register.
  - **Per-behavior** — ~15-20 rows, one per documented algorithmic behavior: input scale, SAME IIR, DIFF IIR, 4-tap comb, APF1, APF2, output scale, hard clip, mix-bus saturation, `BufferAddress` wrap formula, `mBASE` snap-on-write, vIIR=0x8000 anomaly, L/R tick alternation, 39-tap FIR decimate, 39-tap FIR interpolate, split write-timing policy, comb-sum intermediate precision, Q15 truncation-not-rounding.
  - **Per-spec-paragraph** — one row per anchor in the pinned psx-spx.consoledev.net wayback snapshot. Each row names the tests that satisfy that section's claims.
  Three sections in one file (not three separate files; not a unified matrix with registers×behaviors×paragraphs cross-tabulated). Three small tables are cheap to scan top-to-bottom; a matrix gets unreadable past ~20 rows.
- **D-02: CI-enforced across all three sections.** Each row has a machine-parseable field (e.g., `test: tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic`). A small CI check parses the file, verifies each named test exists and passes, and fails the build if any row has no test or a non-passing test. The forcing function of "if you added a behavior, you wired it to a test" is exactly what keeps the doc from decaying into stale prose. Matches the spirit of Phase 1's grep-guard and verify-flags CI gates.
- **D-03: Existing tests count as coverage.** Phase 7 audits the existing `tests/unit/` tree (~40 C unit tests covering q15, buffer, registers, reverb, fir, preset, process) and `tests/python/binding/` + `tests/python/fuzz_*.py` suites, maps existing tests onto rubric rows, and only writes *new* tests for genuinely uncovered behaviors. Most new-test surface is expected in the per-spec-paragraph section, where nocash prose claims don't always have a clean 1:1 existing test.
- **D-04: Spec citations point at a pinned psx-spx.consoledev.net wayback snapshot.** The per-spec-paragraph section cites specific archive.org snapshot URLs with a frozen date. Pinning the snapshot (rather than citing the live psx-spx.consoledev.net URL) means the coverage table won't decay if psx-spx reorganizes next year. Exact snapshot date is planner's discretion; pick one that has all current reverb-relevant sections present and cite that snapshot throughout.

### Area B — Witness-diff harness (TEST-03)

- **D-05: Build lv2-psx-reverb fresh each run.** The harness fetches or clones the lv2-psx-reverb source at a pinned tag/commit during the CI job that runs the diff, compiles it, invokes it via a minimal LV2 host to render SPU-94's standard input set through each preset, and captures the output WAVs. "Tuning fork handed out at every rehearsal" — always current with whatever lv2 version we pin. Mechanics (LV2 host choice — `jalv.console` vs a custom minimal host; lv2 version-pin mechanism — commit SHA vs release tag; fetch method — submodule vs runtime clone) are Claude's discretion.
- **D-06: Phase 7 ships a *measurement* harness; tolerance policy deferred.** The harness computes aligned-RMS divergence per preset and **prints the numbers** as its default output. No pass/fail gate is wired up in Phase 7. Once the first measurements are in hand, a follow-up ADR decides the tolerance policy (frozen per-preset value? single engineering threshold? report-only forever?). ROADMAP SC-2's "within documented per-preset tolerances" is satisfied by this staged approach — "documented" happens after measurement. The tolerance ADR may land as a small Phase 7.1 remediation or as an in-phase addendum once numbers are reviewed.
- **D-07: Split-band diff.** Both SPU-94 output and lv2-psx-reverb output are band-split (low-band ≤ ~10 kHz; high-band > ~10 kHz). The harness reports two aligned-RMS numbers per preset per input:
  - **Low-band divergence** — the gated number (once tolerance ADR lands). This is the axis where lv2 is a valid witness.
  - **High-band divergence** — informational only, never gates. Honors ADR-Phase-4-I's exclusion of lv2 as a spectral-accuracy witness above ~10 kHz (because lv2 skips the 39-tap half-band FIR).
  This is equivalent to grading two clocks at every altitude but only holding sea-level agreement to a standard — the high-band numbers are recorded for future inspection, not failure. Exact filter cutoff, filter order, and alignment algorithm (cross-correlation lag) are Claude's discretion.
- **D-08: lv2 is a behavioral witness, not source material.** The witness-diff harness runs the lv2 binary to capture its output audio; the lv2 source code is NOT read as primary development material. Continues PROJECT.md's licensing posture (GPL sources are not read as primary activity). An ADR captures this explicitly so the discipline is visible in the paper trail.

### Area C — Golden files + standard input set (TEST-04, TEST-08)

- **D-09: Each golden is a `.wav` file + a `.sha256` sidecar.** WAV writing uses the existing vendored dr_wav (frozen version from Phase 6), so header bytes are deterministic across builds. The `.sha256` sidecar is a plain text file containing the SHA-256 of the `.wav` bytes, generated at commit time and regenerated by the reproducibility CI job. Gives byte-identity gating *and* human-audible inspection (double-click any `.wav` to hear what "Hall on impulse" actually sounds like).
- **D-10: Location — in-repo under `tests/golden/<preset>/<input>.wav`.** 50 files × ~350 KB each ≈ 14 MB total, comfortably within git's sweet spot. No Git LFS (overkill at this scale), no separate repo (unnecessary friction). Goldens live alongside the tests that check them.
- **D-11: Standard input set — 5 inputs × 10 presets = 50 goldens.** The five inputs:
  1. **Impulse** — a single full-scale sample at t=0, silence after. Captures the reverb's raw response.
  2. **White noise** — fixed-seed pseudo-random int16 samples at full scale. Tests steady-state response across all frequencies at once.
  3. **1 kHz sine** — integer-amplitude sinusoid. Tests narrowband steady-state behavior.
  4. **Silence** — all zeros. Tests that state starts clean and stays clean.
  5. **Frequency sweep** — linear or log sweep from low to high frequency over a fixed duration. Tests time-varying frequency response — catches bugs that only manifest in specific frequency bands.
  Exact parameters (durations, noise seed value, sine amplitude, impulse amplitude, sweep range, sweep type linear-vs-log) are Claude's discretion; they need only be deterministic and committed. Rationale for adding sweep beyond the roadmap's four: impulse already covers the transient-burst axis; sweep adds a genuinely new *time-varying frequency* axis that neither white noise (all frequencies simultaneous) nor sine (single frequency) covers.
- **D-12: Regeneration requires an ADR.** Any time goldens need to change (intentional algorithm fix, dr_wav upgrade, preset table correction, etc.), an ADR lands in `docs/DECISIONS.md` naming the cause and the expected shape of the change ("ADR-XX: comb-sum precision widened from 16-bit to 32-bit accumulator — expected to shift Hall and Studio A goldens in the 4th sample onward; all 50 goldens regenerated via `scripts/regenerate_goldens.py`"). The regeneration script itself is trivial; the friction lives in the *justification*. Same spirit as DECISIONS.md being a first-class committed deliverable.

### Area D — Reproducibility / Docker pin (TEST-08, BUILD-08)

- **D-13: GitHub CI is the second environment.** The existing `.github/workflows/ci.yml` gets a new job (`reproducibility` or similar) that spins up a pinned Docker container, builds `libspu94.so` inside it from the committed source, regenerates all 50 goldens inside the container, and SHA-256-diffs them against the committed `*.sha256` sidecars. CI fails if any golden's hash doesn't match. The *host dev environment* (Anthony's workstation) is the other side of the comparison — goldens are originally generated on host (via `scripts/regenerate_goldens.py`) and committed; CI's job is to prove the pinned container can reproduce those exact bytes. Anthony never has to run docker by hand.
- **D-14: Docker pin = base image by digest only.** The Dockerfile starts with `FROM debian:<release>@sha256:<digest>` (exact release and digest are planner's discretion; pick a Debian stable release that's actively supported). That one pinned digest transitively locks every tool the container needs — the C compiler, libc, cmake, ninja, Python, numpy — because the base image is a frozen bundle. No per-package `apt install pkg=version` pins are added on top; the image digest already implies them. Maintenance cost: near-zero until a digest update is needed.
- **D-15: Docker pin updates land via ADR.** If the pinned base image ever needs updating (release EOL, security-patch backports demanded, etc.), the digest bump lands via an ADR explaining why, and goldens are regenerated in the new container as part of that ADR's landing. Keeps the reproducibility claim honest against the possibility of silent toolchain drift.

### Area E — Modulation harness + LEVERS-CATALOG (TEST-05, DOCS-02)

- **D-16: Harness writes mechanical columns to LEVERS-CATALOG directly.** The harness exercises every one of the 35 registers in three modes (sine, frequency sweep, random walk) during live `spu94_process` audio, measures per-register behavior, and writes those measurements into LEVERS-CATALOG.md's mechanical columns: **modulation cost** (free / sample-quantized / catastrophic) and **expected zipper behavior** (e.g., "clean up to ~200 Hz modulation; audible stepping above"). The subjective columns — **register name**, **musical role** (one-line prose description of what the register does musically), **suggested M4 lever grouping** — stay hand-written by Anthony. The harness refuses to overwrite the human-authored columns; they're visually separated in the table to make the split clear.
- **D-17: Gate = stability + determinism at every modulation rate.** The harness's pass/fail gate is two-fold:
  - **Stability** — no crashes, no NaN, no unbounded output, no buffer corruption (out-of-bounds reads/writes), at every modulation rate from slow (~0.1 Hz) through audio-rate (up to Nyquist at the 22.05 kHz internal tick rate, i.e., ~11 kHz).
  - **Determinism** — the same input audio + the same register modulation stream produces bit-exactly the same output audio every run. Honored by the split immediate-vs-tick-latched write policy from Phase 2 (D-08 in 02-CONTEXT.md).
  Anything outside these two gates (zipper noise at high modulation rates, stepped audio during fast sweeps, time-smearing on address-register changes, polarity flips when `vIIR` crosses `0x8000`) is *character* of the PS1 hardware — catalogued in LEVERS-CATALOG's mechanical columns, never a CI failure.
- **D-18: ROADMAP SC-4 reinterpretation is captured in an ADR.** ROADMAP SC-4's phrase "free of zipper noise on gain-type registers" is *reinterpreted* as "no internal-tick zipper arising from write-policy violations" — which maps to the determinism gate in D-17, not to a smoothness-at-fast-modulation claim. Rationale: (a) Eurorack-format instruments (the M5 target per PROJECT.md) treat audio-rate parameter modulation as the design point, not an edge case. (b) The PS1 SPU hardware has no parameter smoothing; audio-rate zipper is what the hardware would produce if you modulated it that fast. (c) Making SPU-94 smooth under fast modulation would require adding smoothing to the core, which contradicts bit-faithfulness and is explicitly deferred to M4 (plugin layer) and M5 (Eurorack CV layer). The ADR makes this interpretation explicit so the SC-4 phrasing can't be misread later as promising smoothness the core doesn't provide.
- **D-19: Harness exercises all 35 registers in all three modes.** Each register gets tested in sine modulation (at multiple rates from slow to audio-rate), frequency sweep modulation (from low to audio-rate), and random-walk modulation (bounded walk within the register's valid range). Exact rate sets, sweep range, random-walk seed + step size are Claude's discretion; they need only be deterministic and reasonable.

### Area F — Benchmark harness (BUILD-06)

- **D-20: Gate split — allocations gate, timing reports.** The benchmark harness uses pytest-benchmark to time `spu94_process` across all 10 presets at representative block sizes. Two distinct metrics, two distinct policies:
  - **Hot-path allocation signal** — any heap call (`malloc`, `mmap`, `brk`, or equivalent syscall) anywhere in the `spu94_process` call tree is a hard CI fail. Detection extends Phase 5's strace-based `test_no_syscalls` approach; mechanics (strace filter expressions, pytest integration) are Claude's discretion. This is binary: zero hot-path allocations is the only passing state.
  - **Timing measurements** — pytest-benchmark's per-preset ns/block numbers get published to a CI artifact/log on every run. They are *not* gated. Threshold-based timing gates would fight CI-runner noise for marginal benefit on a personal project; report-only is more honest.
- **D-21: Baseline storage — committed, human-reviewed.** Timing baselines land in a committed file (`tests/python/benchmark_baselines.json` or equivalent, planner's call on format). The file is updated when Anthony (or a future contributor) deliberately looks at a new number and endorses it as the new baseline — no automated threshold-crossing triggers. Planner decides format and refresh cadence.

### Area G — BIBLIOGRAPHY.md polish (DOCS-03)

- **D-22: Additive + cleanup pass.** Phase 7's work on `docs/BIBLIOGRAPHY.md` has two parts:
  - **Additive** — new entries for any Phase 7 sources: lv2-psx-reverb GitHub repo (pinned tag/commit; cited as a *witness binary* source, not as source-code material); any new nocash sections used to seed LEVERS-CATALOG.md's musical-role and M4-lever-grouping columns; pytest-benchmark docs; strace docs; the pinned psx-spx.consoledev.net wayback snapshot.
  - **Cleanup** — walk the existing 146 lines top-to-bottom: ensure every claim in `docs/DECISIONS.md` has a `BIB-xxx` pointer backing it; add any missing entries for inline citations; cluster entries by primary/secondary/witness tier for readability; polish prose to match the README's polished tone (while keeping internal docs honest about known gaps).
  Not a full rewrite — the existing bones are good.

### Claude's Discretion (within locked decisions above)

- Exact LV2 host mechanism for witness-diff (`jalv.console`, minimal in-process host, etc.)
- Exact lv2-psx-reverb version pin (commit SHA vs release tag)
- Exact Debian release + image digest for the pinned Docker container
- Exact filter cutoff and filter order for the split-band witness-diff comparison
- Exact cross-correlation alignment algorithm
- Exact parameters of the standard input set: durations, noise seed, sine amplitude, impulse amplitude, sweep range (start/end frequencies + sweep type linear-vs-log)
- Exact modulation rate set, sweep range, random-walk seed for the modulation harness
- Exact machine-parseable format of `docs/COVERAGE.md` (markdown tables with consistent row fields, CSV, YAML — whatever is easy to parse)
- Exact organization of `tests/conformance/` if new tests get added there (or into the existing `tests/unit/` tree — planner decides)
- Exact pytest-benchmark baseline file format and location
- Exact naming of new scripts (`regenerate_goldens.py`, `witness_diff.py`, `modulation_harness.py`, `generate_coverage_report.py`, etc.)
- Exact number and split of ADRs appended to `docs/DECISIONS.md` — whether each D-XX here gets its own ADR or natural groupings combine (one for coverage map, one for witness-diff split-band approach, one for tolerance-policy-deferral, one for golden format + input set, one for Docker pin, one for modulation harness + SC-4 reinterpretation, one for benchmark gate split, one for bibliography cleanup — eight is a reasonable upper bound, fewer if groupings make sense)
- Whether the modulation harness is invoked as a single pytest test with parametrized registers, or as a standalone script producing a structured report. Suggestion: pytest-parametrized, because it fits the existing `tests/python/` ctest integration.

### Folded Todos

None — no pending todos matched Phase 7 scope at discussion time (`gsd-tools todo match-phase 7` returned empty `matches`).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before proceeding.**

### Project Spec (internal)

- `.planning/PROJECT.md` — Constraints that govern Phase 7: paraphrase-not-transcribe licensing posture (applies to BIBLIOGRAPHY.md cleanup and LEVERS-CATALOG musical-role prose); GPL-sources-are-not-primary-material discipline (applies to lv2-psx-reverb witness-diff — binary consulted, source not read); "SPU-94 is a living instrument, not a preset engine" framing (applies to the modulation harness reinterpretation of SC-4 — audio-rate parameter modulation is the design point); Linux-first; plain C core with no heap and no locks (applies to the hot-path allocation gate); bit-faithful where spec is explicit, documented where it isn't.
- `.planning/REQUIREMENTS.md` — Phase 7 owns TEST-01, TEST-03, TEST-04, TEST-05, TEST-08, BUILD-06, BUILD-08, DOCS-02, DOCS-03 (9 requirements). Coverage mapping updated in REQUIREMENTS.md after Phase 7 closes.
- `.planning/ROADMAP.md` § Phase 7 — six success criteria must all be TRUE. SC-1 spec-conformance suite + coverage map; SC-2 witness-diff harness with per-preset tolerance bounds + frequency-response exclusion; SC-3 byte-identical goldens across CI and host; SC-4 modulation harness over all 35 registers with stability/buffer/zipper criteria (reinterpreted in D-18 above); SC-5 pytest-benchmark harness with no hot-path allocation; SC-6 LEVERS-CATALOG + BIBLIOGRAPHY complete.

### Prior Phase CONTEXT.md files (must read for consistency)

- `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-CONTEXT.md` — ADR format for DECISIONS.md (Phase 7 continues this); CI infrastructure (grep-guard, verify-flags) that Phase 7's CI-enforced coverage check extends; license-placeholder policy still active.
- `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md` — D-08 split write-timing policy (immediate vs tick-latched per register) — this is *what the modulation harness's determinism gate is verifying*. D-09/D-10 mBASE snap-on-write — the modulation harness treats mBASE as an "address/base" class register where sudden buffer-layout shifts are allowed (documented in LEVERS-CATALOG), not as a stability failure. D-17 `spu94_reg_name` / `spu94_reg_hw_offset` accessors — consumed by the Python binding's runtime-reflected `Register` IntEnum, which the modulation harness iterates over.
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md` — algorithm-level behaviors that the per-behavior section of COVERAGE.md enumerates: SAME IIR, DIFF IIR, 4-tap comb, APF1, APF2, hard clip, vIIR=0x8000 anomaly, comb-sum intermediate precision.
- `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-CONTEXT.md` — ADR-Phase-4-I (coefficient provenance) — cited in BIBLIOGRAPHY cleanup. ADR excluding lv2-psx-reverb as a spectral-accuracy witness above ~10 kHz — *this is the decision D-07's split-band diff operationalizes*. Any change in Phase 7's witness-diff axis must trace back to this Phase 4 decision.
- `.planning/phases/05-public-api-presets-integration/05-CONTEXT.md` — `spu94_process` + `spu94_flush` + `spu94_load_preset` public surface — wrapped by the modulation harness (via Phase 6 Python binding) and by the witness-diff harness (via the CLI). `test_no_syscalls` strace harness — *Phase 7's hot-path allocation gate extends this*. `spu94_presets[]` table — iterated by the golden-file generator and the witness-diff harness.
- `.planning/phases/06-python-binding-cli/06-CONTEXT.md` — D-06 runtime-reflected `Register` IntEnum — the modulation harness iterates over this enum to exercise every register. D-01/D-02 raw-panel API + `SPU94` class — both layers are available to Phase 7 harnesses; modulation harness will likely use the raw-panel layer for fine-grained control. Strict int16 numpy contract — Phase 7 harnesses respect this (all audio passed to `process` is `np.int16` C-contiguous). Native `spu94` CLI + vendored dr_wav — used by the golden-file generator to render `.wav` files deterministically.

### Public C API + Python binding surface (wrapped by Phase 7 harnesses)

- `include/spu94/spu94.h` — `spu94_init` / `spu94_reset` / `spu94_destroy` / `spu94_process` / `spu94_flush` / `spu94_load_preset` / `spu94_state_size` / `spu94_get_latency_samples` / `spu94_presets[]`. Consumed by witness-diff harness (via CLI), golden-file generator (via CLI), modulation harness (via Python binding), benchmark harness (via Python binding).
- `include/spu94/spu94_registers.h` — `spu94_reg_t` enum (35 entries, `SPU94_REG__COUNT`), `spu94_set_reg_i16` / `spu94_set_reg_u16` / `spu94_get_reg_i16` / `spu94_get_reg_u16` + `_pending` variants, `spu94_reg_name`, `spu94_reg_hw_offset`, `spu94_snapshot_registers`. *All 35 registers are iterated by the modulation harness.*
- `python/spu94/` — Phase 6's binding. Harnesses import: `spu94.SPU94` (class wrapper), `spu94.Register` (runtime-reflected IntEnum — iterate `for reg in spu94.Register:`), `spu94.Preset` (IntEnum), `spu94.presets` (accessor), `spu94.process`, `spu94.flush`, `spu94.load_preset`, `spu94.set_reg_i16`, `spu94.set_reg_u16`, `spu94.get_reg_i16`, `spu94.get_reg_u16`.
- `src/cli/main.c` (Phase 6's native `spu94` CLI) — used by the golden-file generator to render `.wav` files for the 50-golden × 10-preset × 5-input matrix. Accepts `--preset <name>` and `--config <json>`. Output WAV is dr_wav-written deterministically.

### Existing test tree (coverage candidates for COVERAGE.md)

- `tests/unit/q15/` — Q15 truncation helpers.
- `tests/unit/buffer/` — `BufferAddress` wrap + mBASE snap-on-write.
- `tests/unit/registers/` — per-register I/O roundtrip + type preservation + policy split.
- `tests/unit/reverb/` — `test_reverb_apf1.c`, `test_reverb_apf2.c`, `test_reverb_body.c`, `test_reverb_comb.c`, `test_reverb_diff_iir.c`, `test_reverb_edges.c`, `test_reverb_hard_clip.c`, `test_reverb_input_scale.c`, `test_reverb_output_scale.c`, `test_reverb_same_iir.c`.
- `tests/unit/fir/` — `test_fir_bit_identity.c`, `test_fir_chain_latency.c`, `test_fir_coef_table.c`, `test_fir_dc.c`, `test_fir_decimate.c`, `test_fir_err_overflow_taps.c`, `test_fir_frequency_sweep.c`, `test_fir_impulse.c`, `test_fir_interpolate.c`, `test_fir_overflow_proof.c`, `test_fir_round_trip_transparency.c`.
- `tests/unit/preset/` — `test_preset_load_all.c`, `test_preset_nonzero_tail.c`, `test_preset_table_integrity.c`.
- `tests/unit/process/` — `test_process_basic.c`, `test_process_block_size.c`, `test_process_flush.c`, `test_process_in_place.c`, `test_process_mix_bus.c`, `test_process_reverb_audible.c`, `test_process_reverb_linearity.c`.
- `tests/python/fuzz_buffer.py`, `fuzz_reverb.py`, `fuzz_fir.py`, `fuzz_process.py` — 10⁶-step random-walk fuzz harnesses. `fuzz_process.py` specifically already exercises all 35 registers during live `spu94_process` with bit-exact Python-model comparison — Phase 7's modulation harness extends this with structured sine + sweep + random-walk modes and per-register stability/determinism reporting into LEVERS-CATALOG.md.
- `tests/python/binding/` — `test_binding_drift_detection.py`, `test_binding_numpy_contract.py`, `test_binding_preset_table.py`, `test_binding_register_intenum.py`, `test_binding_surface.py` — Phase 6 binding validation; not directly relevant to Phase 7's coverage rubric but counts toward binding-surface coverage.
- `tests/rt_safety/` — Phase 5's RT-safety CI infrastructure (`test_no_heap.sh`, `verify-no-locks.sh`, `test_no_syscalls` strace harness, `bench_latency.py`) — Phase 7's hot-path allocation gate extends `test_no_syscalls`.

### ADR Log (Phase 7 appends)

- `docs/DECISIONS.md` — existing: ADR-0001..ADR-0011 (Phases 1–3), ADR-0012..ADR-0020 (Phase 4 FIR), ADR-Phase-5-A..F (Phase 5 public API + presets + RT-safety), ADR-Phase-6-A..H (Phase 6 binding + CLI + wheel + README + H for master-send default relocation). Phase 7 appends ADRs for: COVERAGE.md structure (D-01..D-04), witness-diff harness + split-band approach + tolerance deferral (D-05..D-08), golden file format + standard input set + regeneration policy (D-09..D-12), Docker pin strategy (D-13..D-15), modulation harness + SC-4 reinterpretation (D-16..D-19), benchmark gate split (D-20..D-21), bibliography polish (D-22). Eight ADRs is a reasonable upper bound; planner may combine where groupings make sense.

### External References (paraphrased only — do NOT transcribe per PROJECT.md licensing posture)

- **Pinned psx-spx.consoledev.net wayback snapshot** — the authoritative spec reference for the per-spec-paragraph section of COVERAGE.md. Specific archive.org snapshot URL + date are planner's discretion; pick a snapshot where all reverb-relevant sections are present. BIBLIOGRAPHY.md gets a dedicated entry pointing at the pinned snapshot.
- **lv2-psx-reverb** (github.com/lucianodato/lv2-psx-reverb or wherever the canonical repo lives — planner confirms) — GPLv3 emulator. Used as a *behavioral witness binary only*; source code is NOT consulted as primary material. Version-pinned to a specific commit or release tag. BIBLIOGRAPHY.md entry notes license + witness-only posture.
- **pytest-benchmark** (pytest-benchmark.readthedocs.io) — benchmark harness framework. Used for timing measurement; hot-path allocation detection uses strace separately.
- **strace** (man strace) — syscall tracer; extended from Phase 5's `test_no_syscalls` for the hot-path allocation gate. Filter expression to catch heap calls (`brk`, `mmap`, `mmap2`) is planner's discretion.
- **SHA-256 (RFC 6234)** — hash function for golden-file sidecars. Computed via `sha256sum` (coreutils) or equivalent.
- **LV2 spec** (lv2plug.in) — the plugin spec lv2-psx-reverb implements. An LV2 host (`jalv.console` or a custom minimal host) is required to run the plugin. BIBLIOGRAPHY.md entry notes LV2 spec as background for the witness harness infrastructure.
- **Docker** (docs.docker.com) — container runtime. Base image digest pinning uses standard Docker `FROM image@sha256:digest` syntax. BIBLIOGRAPHY.md entry is brief; Docker is tooling, not a content source.

### Not to be read as primary source

- lv2-psx-reverb source code (GPLv3) — only binary is used.
- Mednafen (GPLv2), DuckStation, MiSTer PSX core source code — per PROJECT.md licensing posture. Phase 4 empirically tested their FIR implementation status; Phase 7 uses none of their source.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (from Phases 1–6)

- `libspu94.so` (built by CMake target `spu94_shared`) — full Phase 5 public API. Phase 7 harnesses load this via the Phase 6 binding or via the CLI.
- `src/cli/main.c` + vendored `dr_wav` + vendored `jsmn` — Phase 6 CLI. The golden-file generator shells out to `spu94 --preset <name> <input.wav> <output.wav>` (or the `--config` variant) to produce the 50 `.wav` files. dr_wav's determinism is already proven by Phase 6's CI; golden-file header bytes are therefore stable across builds.
- `python/spu94/` package — ctypes binding. Modulation harness + benchmark harness + witness-diff harness (for computing divergence from captured WAVs) all import from this. Runtime-reflected `Register` IntEnum lets the modulation harness iterate `for reg in spu94.Register: ...` without any hand-typed register list.
- `tests/python/fuzz_*.py` — proven pattern: `from spu94 import ...`, construct `SPU94()` state, drive `process` with numpy int16 arrays, compare against Python reference model. Modulation harness extends this pattern with structured modulation modes + per-register reporting.
- `tests/rt_safety/test_no_syscalls` (strace-based) — proven mechanism for catching syscalls in a code path. Phase 7's hot-path allocation gate is a targeted filter of this (looking for `brk`, `mmap*` specifically rather than all syscalls).
- `docs/DECISIONS.md` — existing ADR log (~2500 lines). Phase 7 appends ADRs per the D-XX decisions above.
- `docs/BIBLIOGRAPHY.md` — existing 146-line bibliography with BIB-001..BIB-006+ entries. Phase 7 does additive + cleanup pass.
- `.github/workflows/ci.yml` — existing CI config with SHA-pinned third-party actions. Phase 7 adds a new `reproducibility` job (or extends an existing job) for the Docker-pinned golden-file diff.
- `CMakeLists.txt` + `tests/CMakeLists.txt` — existing CMake test topology with ctest integration. Phase 7 adds test targets for: coverage-check CI step (bash or Python script parsing COVERAGE.md), witness-diff harness (Python + compiled lv2), golden-file diff (shell or Python), modulation harness (pytest-parametrized or standalone), benchmark (pytest-benchmark).

### Established Patterns

- **ADR-style entries in `docs/DECISIONS.md`** (since Phase 1) — Phase 7 continues; each D-XX may or may not get its own ADR per planner discretion.
- **Hand-computed inline reference tables in tests** (Phase 1 D-10) — COVERAGE.md's machine-parseable rows follow the same spirit: explicit, versioned, auditable. No magic.
- **Env-var-first, installed-path-fallback for `SPU94_LIB`** (Phase 6) — Phase 7 harnesses inherit this via the Python binding; dev can point at a freshly built `build/src/spu94/libspu94.so` while CI uses the installed location.
- **Paraphrase-not-transcribe** (PROJECT.md) — applies to BIBLIOGRAPHY.md cleanup (no nocash prose is copied into our docs) and to LEVERS-CATALOG.md's musical-role column (written in Anthony's own voice, citing `BIB-xxx` entries only for facts like register addresses).
- **One-concern-per-TU grain** (Phase 2) — Phase 7 script organization: one concern per script (`regenerate_goldens.py`, `witness_diff.py`, `modulation_harness.py`, `generate_coverage_report.py`).
- **Pitfall 4 single-call-site discipline** (ADR-0005) — Phase 7 harnesses each have a single call site for the C entry points they exercise; no secondary code paths through the library.
- **CI SHA-pinning for third-party actions** (Phase 1) — the new `reproducibility` CI job follows the same SHA-pinning discipline as existing jobs.

### Integration Points

- **Phase 6 Python binding is the sole Python-side interface to libspu94.** Phase 7 harnesses do not add new ctypes declarations or reach into `src/spu94/*_internal.h` from Python. If the modulation harness needs struct offsets not exposed via the public API (e.g., for invariant checks on internal state), they stay in the harness with a clearly labeled warning block — matches Phase 6 D-08.
- **Phase 5 public C API is the sole C-side interface the witness-diff and golden-file harnesses use (via the CLI).** No internal headers consumed.
- **Phase 8 (MCU cross-compile)** does NOT consume Phase 7's harnesses. MCU is bare-metal; no Python, no pytest, no lv2, no Docker. Phase 7's work is Linux-host-only.
- **Milestone 4 (plugin era)** heavily consumes Phase 7's outputs: LEVERS-CATALOG.md is the starting document for M4's musical lever design; the modulation-harness-measured per-register zipper thresholds tell M4 which registers need smoothing at which rates; BIBLIOGRAPHY.md is referenced from the plugin's about-box or docs.
- **Milestone 5 (hardware validation)** consumes Phase 7's outputs: witness-diff harness can be extended to accept PS1 hardware captures as an additional witness; golden files can be compared byte-for-byte against hardware-rendered outputs of the same inputs; the modulation-harness-measured determinism gate is directly testable against hardware.

</code_context>

<specifics>
## Specific Ideas

### Rough shape of `docs/COVERAGE.md`

```markdown
# SPU-94 — Spec Conformance Coverage

Pinned spec reference: https://web.archive.org/web/<snapshot-date>/https://psx-spx.consoledev.net/soundprocessingunitspu/

## Per-Register Coverage (35 rows)

| Register | Type | Test(s) | Notes |
|----------|------|---------|-------|
| `vIIR`   | signed Q15 | `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic`, `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_anomaly` | vIIR=0x8000 anomaly covered in second test |
| `vWALL`  | signed Q15 | `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic` | |
| ...      |      |         |       |

## Per-Behavior Coverage (~15-20 rows)

| Behavior | Spec reference | Test(s) | Notes |
|----------|----------------|---------|-------|
| SAME IIR stage | psx-spx §reverb-processing ¶2 | `tests/unit/reverb/test_reverb_same_iir.c::*` | |
| DIFF IIR stage | psx-spx §reverb-processing ¶3 | `tests/unit/reverb/test_reverb_diff_iir.c::*` | |
| 4-tap comb | psx-spx §reverb-processing ¶4 | `tests/unit/reverb/test_reverb_comb.c::*` | D-07 cascading saturation |
| APF1 | psx-spx §reverb-processing ¶5 | `tests/unit/reverb/test_reverb_apf1.c::*` | |
| APF2 | psx-spx §reverb-processing ¶6 | `tests/unit/reverb/test_reverb_apf2.c::*` | |
| vIIR=0x8000 anomaly | psx-spx §reverb-processing ¶X | `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_anomaly` | |
| mBASE snap-on-write | psx-spx §reverb-buffer ¶Y | `tests/unit/buffer/test_mbase_snap.c::*` | |
| BufferAddress wrap | psx-spx §reverb-buffer ¶Z | `tests/unit/buffer/test_buffer_wrap.c::*` | |
| 39-tap FIR decimate | psx-spx §reverb-buffer-resampling | `tests/unit/fir/test_fir_decimate.c::*`, `test_fir_impulse.c::*` | |
| 39-tap FIR interpolate | psx-spx §reverb-buffer-resampling | `tests/unit/fir/test_fir_interpolate.c::*` | |
| Hard clip on mix bus | psx-spx §spu-output ¶Q | `tests/unit/reverb/test_reverb_hard_clip.c::*`, `tests/unit/process/test_process_mix_bus.c::*` | |
| L/R tick alternation | psx-spx §reverb-timing | `tests/unit/reverb/test_reverb_body.c::test_lr_tick_order` | |
| Split write-timing (immediate vs tick-latched) | Phase 2 D-08 + psx-spx §spu-register-timing | `tests/unit/registers/test_write_policy.c::*` | |
| Q15 truncation-not-rounding | psx-spx §spu-fixed-point | `tests/unit/q15/test_q15_truncate.c::*` | ADR-0001 |
| Comb-sum intermediate precision | ADR-Phase-3-X | `tests/unit/reverb/test_reverb_comb.c::test_comb_intermediate` | |

## Per-Spec-Paragraph Coverage (one row per anchor in pinned snapshot)

| Snapshot anchor | Test(s) satisfying claims | Notes |
|-----------------|---------------------------|-------|
| #spureverbregisters | `tests/unit/registers/*` — all 35 registers round-trip | |
| #reverb-processing | `tests/unit/reverb/*` — all stages tested in isolation + composition | |
| #reverb-buffer | `tests/unit/buffer/*` — wrap formula + mBASE policy | |
| #reverb-buffer-resampling | `tests/unit/fir/*` — 39-tap decimate + interpolate + round-trip | |
| ...             |                           |       |
```

### Rough shape of `docs/LEVERS-CATALOG.md`

```markdown
# SPU-94 — Register Levers Catalog

Each of the 35 reverb-affecting registers, annotated with musical role,
modulation cost, and M4 lever grouping candidacy.

**Legend — modulation cost:**
- **free** — any modulation rate stays stable and deterministic; zipper audible above catalogued threshold
- **sample-quantized** — modulation is honored at tick boundaries; finer rates produce discrete steps
- **catastrophic** — modulation causes buffer discontinuities requiring M4/M5 smoothing to be musical

| Register | Musical role (hand-written) | Modulation cost (mechanical) | Zipper behavior (mechanical) | Suggested M4 lever (hand-written) |
|----------|-----------------------------|------------------------------|------------------------------|-----------------------------------|
| `vIIR`   | IIR tail density — how much of the previous tick's output feeds back into the next. High values = long dense tail; negative values = polarity inversion; `0x8000` = full negation (anomaly) | free | clean up to ~XXX Hz; stepping audible above | Decay |
| `vWALL`  | Room damping — how aggressively the IIR attenuates each pass | free | clean up to ~XXX Hz | Damping |
| `vCOMB1-4` | Four independent early-reflection taps — shape early echo density | free | clean up to ~XXX Hz | Diffusion |
| `vAPF1/2`  | All-pass filter coefficients — shape late-field smearing | free | clean up to ~XXX Hz | Diffusion |
| `dCOMB1-4` | Four comb-tap delay addresses — position of each early reflection | sample-quantized | stepping audible at any rate | Size / Pre Delay |
| `dAPF1/2`  | All-pass delay addresses — late-field delay | sample-quantized | stepping audible | Size |
| `mBASE`    | Reverb buffer start address — whole-buffer layout | catastrophic | buffer discontinuity at any modulation | (not M4-modulatable; set-and-forget per preset) |
| ...        |                              |                              |                              |                                   |
```

### Rough shape of the modulation harness invocation

```python
# tests/python/modulation_harness.py (standalone or pytest-parametrized)
import spu94
import numpy as np

# Iterate all 35 registers via the runtime-reflected IntEnum
for reg in spu94.Register:
    # Three modes per register
    for mode in ("sine", "sweep", "random_walk"):
        for rate_hz in RATES:
            output = run_modulated_process(reg, mode, rate_hz, input_audio)
            # Gate
            assert is_stable(output), f"{reg.name} unstable at {mode}@{rate_hz}Hz"
            assert is_deterministic(reg, mode, rate_hz), f"{reg.name} non-deterministic at {mode}@{rate_hz}Hz"
            # Characterize (writes to LEVERS-CATALOG mechanical columns)
            catalog_row(reg, mode, rate_hz, zipper_onset(output), ...)
```

### Rough Docker pin shape

```dockerfile
# Dockerfile.repro (planner picks exact release + digest)
FROM debian:12.5@sha256:<digest>

RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build \
    python3 python3-venv python3-pip \
    strace \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && python3 -m venv /venv \
    && /venv/bin/pip install -e .

CMD ["/venv/bin/python", "scripts/regenerate_goldens.py", "--check"]
```

### Rough CI job for reproducibility

```yaml
# New job appended to .github/workflows/ci.yml
reproducibility:
  name: reproducibility (BUILD-08, TEST-08)
  runs-on: ubuntu-latest
  steps:
    - name: Checkout
      uses: actions/checkout@<pinned-sha>
    - name: Build and run reproducibility container
      run: |
        docker build -f Dockerfile.repro -t spu94-repro .
        docker run --rm spu94-repro
    # If the container's --check run succeeds, goldens match.
    # If any SHA differs, the container exits non-zero and CI fails.
```

### Rough witness-diff shape

```python
# tests/python/witness_diff.py
# 1. Build lv2-psx-reverb fresh (fetch pinned commit, compile, install to /tmp/lv2)
# 2. For each preset, for each input:
#    a. Render SPU-94 output via `spu94` CLI
#    b. Render lv2 output via LV2 host (jalv.console or custom) at same sample rate
# 3. For each output pair:
#    a. Cross-correlate for alignment lag
#    b. Apply low-pass at ~10 kHz to both signals
#    c. Compute aligned-RMS divergence on low-band
#    d. Apply high-pass at ~10 kHz to both signals
#    e. Compute aligned-RMS divergence on high-band
# 4. Report: { preset: { input: { low_band_db: X, high_band_db: Y, alignment_samples: Z } } }
# 5. Print numbers; do not gate (Phase 7 deliverable is the measurement harness only).
```

</specifics>

<deferred>
## Deferred Ideas

### Deferred to a follow-up ADR (measure-first)

- **Per-preset divergence tolerance values for witness-diff.** Phase 7 ships the measurement harness; the pass/fail ADR lands after actual divergence numbers are reviewed. May become part of Phase 7 itself (if numbers are inspected and tolerances agreed inside the phase window) or a small Phase 7.1 remediation.

### Deferred to Milestone 4 (Plugin Era)

- **Parameter smoothing** for smooth-under-audio-rate-modulation on gain registers. The PS1 hardware has no smoothing; SPU-94's core stays bit-faithful and produces whatever the hardware would at any modulation rate. M4's plugin layer wraps the core with an interpolation/smoothing filter on the control side.
- **Named musical levers** ("Room Size", "Pre Delay", "Decay", "Diffusion", "Damping") — the LEVERS-CATALOG.md "suggested M4 lever grouping" column is Phase 7's contribution. The actual M4 lever UI, smoothing curves, CV-input mappings, and parameter automation are M4 work.
- **Plugin UI** for visualizing modulation — Phase 7's modulation harness produces numbers in a report file; M4's UI can read that file and show per-register zipper-onset graphs to the user.

### Deferred to Milestone 5 (Eurorack + Hardware Validation)

- **Eurorack CV smoothing layer** — analog CV inputs pass through an anti-aliasing low-pass before hitting the register-write logic. Phase 7's modulation harness proves the underlying registers can be modulated at audio rates without crashing; M5 adds the CV-side interpolation to make that musical.
- **Witness-diff extension** to accept captured PS1 hardware outputs as an additional witness — Phase 7's witness-diff harness is designed to accept any output WAV with a known-good alignment process; hardware captures plug in as another witness source in M5.
- **Bit-exact golden comparison against hardware** — Phase 7's golden files will be compared byte-for-byte against hardware-rendered outputs of the same inputs in M5 validation.

### Deferred Platform Support

- **Windows/macOS/aarch64 reproducibility containers** — Phase 7 pins a single Linux Docker image. Post-M1 deferred; mostly a matter of adding platform-specific Dockerfiles.
- **Self-hosted CI** — Phase 7 uses GitHub's shared runners. Self-hosted (on a computer Anthony controls) is post-M1 if benchmark noise or runner availability becomes a pain point.

### Deferred Harness Features

- **Fuzz the witness-diff harness** — random inputs beyond the 5 standard ones, see where SPU-94 and lv2 diverge most. Interesting but not Phase 7 scope.
- **Real-time audio-rate modulation with live MIDI/OSC input** — the modulation harness uses deterministic modulation streams for gate + catalog purposes. Live-input modulation is M4 plugin territory.
- **Per-register oversampling analysis** — when zipper is audible, what's the equivalent smoothing filter that would remove it? Data for M4 smoothing design, not Phase 7 rigor. LEVERS-CATALOG records the threshold; M4 decides the filter.
- **GPU/SIMD vectorization benchmarking** — Phase 7 benchmarks `spu94_process` as written. Vectorization variants (if ever added) would need their own benchmark baselines.

### Raised in Discussion, Routed Elsewhere

- **"Why do we need GitHub at all for this?"** (user, 2026-04-23) — Conversational detour to clarify what CI, local-dev, and GitHub-hosted CI actually mean in this context. Not a decision point. Resolved: GitHub CI continues as the reproducibility gate's second environment (D-13) because it runs automatically on every push without manual docker invocation.
- **"It's mostly standard in the Eurorack format that all parameter control can handle up to audio rate modulation."** (user, 2026-04-23) — Surfaced the realization that ROADMAP SC-4's "free of zipper noise on gain-type registers" wording could be misread as promising smoothness the core doesn't provide. Resolved: gate = stability + determinism at every modulation rate including audio-rate (D-17); zipper at high rates is character, not failure (D-17); SC-4 reinterpretation captured in ADR (D-18). Smoothing remains M4/M5 layer work.
- **"Can we wait to see what level of deviations we are seeing before deciding how strict to be?"** (user, 2026-04-23) — Applied to witness-diff tolerance (D-06). Phase 7 measures and reports; tolerance policy is a follow-up ADR once numbers are visible. This measure-first posture may generalize to other gates if similar uncertainty surfaces later.

### Reviewed Todos (not folded)

None — no pending todos existed at discussion time (confirmed via `gsd-tools todo match-phase 7` returning empty `matches`).

### Scope Creep Rejections

None raised during discussion. The six areas (spec-conformance coverage, witness-diff, golden files, reproducibility, modulation harness + LEVERS-CATALOG, benchmark + BIBLIOGRAPHY) all stayed inside the Phase 7 domain boundary (verification + the two verification-adjacent doc deliverables).

</deferred>

---

*Phase: 07-verification-golden-files-witness-diff-modulation*
*Context gathered: 2026-04-23*
*Next step: `/gsd-plan-phase 7` — planner consumes this CONTEXT.md. Planner will first invoke research (for lv2-psx-reverb build details, LV2 host options, pytest-benchmark + strace integration patterns, Debian image-digest pin conventions, pinned wayback snapshot anchors in psx-spx.consoledev.net), then task breakdown across approximately 5–7 plans.*
