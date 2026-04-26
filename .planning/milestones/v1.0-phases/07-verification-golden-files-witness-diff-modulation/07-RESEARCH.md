# Phase 7: Verification — Golden Files, Witness Diff, Modulation — Research

**Researched:** 2026-04-23
**Domain:** DSP verification (spec-conformance coverage, cross-implementation witness diff, golden-file regression, per-register modulation harness, allocation-gated benchmarking, reproducibility via Docker digest pin, documentation hardening)
**Confidence:** HIGH for stack/tooling; HIGH for Docker digest pin (verified against live Docker Hub API); HIGH for wayback snapshot availability (verified via HTTP HEAD); MEDIUM for exact behavioral-classifier thresholds (proposed, not measured — measure-first per D-06 spirit)

## Summary

Phase 7 ships the proof layer behind SPU-94's bit-faithful claim. The twenty-two D-01..D-22 locked decisions in `07-CONTEXT.md` answer every major *shape* question; this research fills in the *mechanics* the planner needs to turn those decisions into task-level plans without further discussion cycles.

The phase has six work areas (coverage map, witness diff, goldens + Docker, modulation harness + LEVERS-CATALOG, benchmark + allocation gate, bibliography cleanup) plus a persistent validation-of-the-validator meta-test concern. Every tool required is already installed on the host dev machine **except** `scipy` (needed for witness-diff cross-correlation + band-split filter) and `pytest-benchmark` (needed for benchmark harness). Both are installable from Ubuntu universe or PyPI and add to Phase 7's `[project.optional-dependencies]` dev group cleanly.

**Primary recommendation:** Use `lv2apply` (from `lilv-utils`) as the LV2 host — it is a single-binary, zero-daemon, zero-audio-server CLI that is designed for exactly this use case (render an LV2 plugin on an input WAV to an output WAV). Pin `debian:bookworm-slim` by its verified multi-arch manifest digest `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252` (published 2026-04-22). Pin `lv2-psx-reverb` at commit `424e1e8ee7f780106b005011b036386513c61db3` (2023-09-08, `master` tip, no release tags exist). Use wayback snapshot `https://web.archive.org/web/20260114082525/https://psx-spx.consoledev.net/soundprocessingunitspu/` (2026-01-14 capture) as the pinned spec reference. Use scipy's `scipy.signal.butter` + `sosfiltfilt` for the ~10 kHz band split and `scipy.signal.correlate` with FFT mode for cross-correlation lag detection.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

Verbatim from `07-CONTEXT.md` `<decisions>`. Twenty-two decisions across seven areas. The planner MUST honor every one; research below does not re-litigate them.

#### Area A — Spec-conformance coverage map

- **D-01:** Single file `docs/COVERAGE.md`, three sections: per-register (35 rows), per-behavior (~15-20 rows), per-spec-paragraph (one row per pinned snapshot anchor).
- **D-02:** CI-enforced across all three sections via a machine-parseable `test:` field per row; CI parses, verifies the named test exists and passes, fails on any gap.
- **D-03:** Existing tests count toward coverage. Phase 7 audits the ~40+ existing C/Python tests and only writes new tests for genuinely uncovered behaviors.
- **D-04:** Spec citations point at a *pinned* psx-spx.consoledev.net wayback snapshot (frozen date).

#### Area B — Witness-diff harness

- **D-05:** Build lv2-psx-reverb fresh each run from a pinned tag/commit.
- **D-06:** Phase 7 ships the *measurement* harness only; per-preset tolerance *policy* is deferred to a follow-up ADR after numbers are inspected.
- **D-07:** Split-band diff (low-band ≤ ~10 kHz gated; high-band > ~10 kHz informational). Low-band is the axis where lv2 is a valid witness (per ADR-Phase-4-I).
- **D-08:** lv2 is a behavioral witness *binary only*; lv2 source code is NOT read as primary material. ADR captures this.

#### Area C — Golden files + standard input set

- **D-09:** `.wav` + `.sha256` sidecar. WAV via vendored dr_wav (frozen from Phase 6).
- **D-10:** In-repo at `tests/golden/<preset>/<input>.wav`. ~14 MB total; no LFS, no separate repo.
- **D-11:** 5 inputs × 10 presets = 50 goldens. Inputs: impulse, white noise, 1 kHz sine, silence, frequency sweep.
- **D-12:** Regeneration requires an ADR.

#### Area D — Docker pin

- **D-13:** GitHub CI is the second environment. New `reproducibility` job builds inside pinned container, regenerates all 50 goldens, diffs against committed `.sha256` sidecars.
- **D-14:** Docker pin = base image by digest only (`FROM debian:<release>@sha256:<digest>`). No per-package apt pins.
- **D-15:** Docker pin updates land via ADR; goldens regenerate in the new container as part of that ADR.

#### Area E — Modulation harness + LEVERS-CATALOG

- **D-16:** Harness writes mechanical columns (modulation cost, zipper behavior) directly to `docs/LEVERS-CATALOG.md`. Subjective columns (name, musical role, M4 lever grouping) stay hand-written; harness refuses to overwrite human-authored columns.
- **D-17:** Gate = stability (no crash / no NaN / no unbounded output / no buffer corruption) + determinism (bit-exact reproducibility) at *every* modulation rate from slow through audio-rate (~11 kHz at 22.05 kHz tick). Zipper / stepping / polarity flips at high modulation rates are *character*, catalogued not gated.
- **D-18:** ROADMAP SC-4 reinterpreted: "free of zipper noise on gain-type registers" becomes "no internal-tick zipper arising from write-policy violations" — maps to the determinism gate. Captured in an ADR.
- **D-19:** All 35 registers × all three modes (sine, sweep, random-walk). Rate sets / sweep range / random-walk seed are Claude's discretion.

#### Area F — Benchmark + allocation gate

- **D-20:** Two-policy split. Hot-path allocation (any `malloc`/`mmap`/`brk` / equivalent in the `spu94_process` call tree) = hard CI fail. Timing numbers = report-only, never gated.
- **D-21:** Timing baselines land in a committed, human-reviewed file. No automated threshold-crossing triggers; Anthony endorses each new baseline explicitly.

#### Area G — BIBLIOGRAPHY.md polish

- **D-22:** Additive (new BIB-entries for lv2-psx-reverb witness, pinned wayback snapshot, pytest-benchmark, strace, LV2 spec) + cleanup pass (cross-reference every DECISIONS.md claim to a `BIB-xxx` pointer; cluster by primary/secondary/witness tier; polish tone). Not a full rewrite.

### Claude's Discretion

Verbatim from CONTEXT.md. Research below makes concrete recommendations for each:

- Exact LV2 host mechanism → **recommended: `lv2apply` from `lilv-utils`**
- Exact lv2-psx-reverb version pin → **recommended: commit `424e1e8ee7f780106b005011b036386513c61db3` (master tip, 2023-09-08; no releases exist)**
- Exact Debian release + image digest → **recommended: `debian:bookworm-slim` @ `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252`**
- Exact filter cutoff and order for band-split → **recommended: 10 kHz crossover, 8th-order Butterworth zero-phase via `sosfiltfilt`**
- Exact cross-correlation alignment algorithm → **recommended: `scipy.signal.correlate(..., mode='full', method='fft')`**
- Exact parameters of standard input set → **recommended values below in "Standard Input Set parameters"**
- Exact modulation rate set / sweep range / random-walk seed → **recommended geometric sweep 0.1 Hz to 11.025 kHz, fixed `np.random.default_rng(0x1094_DADA)` seed**
- Exact machine-parseable format of `docs/COVERAGE.md` → **recommended: pure-markdown tables with explicit `test:` column per row + a ~50-line Python validator at `scripts/ci/check_coverage.py`**
- Exact organization of new tests → **recommended: `tests/conformance/` subtree for spec-paragraph-only tests; everything else stays in `tests/unit/`**
- Exact pytest-benchmark baseline file format and location → **recommended: committed `tests/python/benchmarks/baseline.json` with pytest-benchmark's native format**
- Exact script naming → **recommended: `scripts/regenerate_goldens.py`, `scripts/ci/witness_diff.py`, `scripts/ci/modulation_harness.py` or `tests/python/modulation_harness.py`, `scripts/ci/check_coverage.py`, `scripts/ci/hotpath_alloc_gate.sh`**
- Exact number and split of ADRs → **recommended: eight ADRs (ADR-Phase-7-A..H), one per work area + one for SC-4 reinterpretation** (see ADR plan below)
- Pytest-parametrized vs standalone modulation harness → **recommended: pytest-parametrized (matches existing ctest integration pattern)**

### Deferred Ideas (OUT OF SCOPE)

Verbatim from CONTEXT.md:

- Per-preset divergence tolerance *values* for the witness-diff harness — deferred to follow-up ADR after Phase 7 measurements visible.
- Parameter smoothing for smooth-under-audio-rate-modulation — M4 / M5 work.
- Named musical levers ("Room Size", "Pre Delay", "Decay", "Diffusion", "Damping") — M4 work.
- Eurorack CV smoothing layer — M5 work.
- Hardware-witness extension of witness-diff harness — M5 work.
- Windows/macOS/aarch64 reproducibility containers — post-M1.
- Fuzzing the witness-diff harness — interesting but not Phase 7 scope.
- Real-time audio-rate modulation with live MIDI/OSC — M4 territory.
- Per-register oversampling analysis — M4 data; LEVERS-CATALOG records threshold only.
- GPU/SIMD vectorization benchmarking — not applicable until the core is vectorized.
- Cross-AI review, security review, UI audit — phase has no UI and no external untrusted input.
- MCU cross-compile validation — Phase 8.
- JUCE / VST3 / AU / LV2 plugin wrapper — Milestone 4.
- Hardware validation via original PSX — Milestone 5.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEST-01 | Spec-conformance test suite — each nocash-documented reverb behavior has a corresponding test | § Coverage Map Mechanics (Area A) + § Standard Stack (markdown-table parser pattern) + § Code Examples (validator skeleton) |
| TEST-03 | Witness-diff harness running SPU-94 vs lv2-psx-reverb, freq-response excluded per ADR-Phase-4-I | § Witness-Diff Mechanics (Area B) + § Standard Stack (lv2apply, scipy.signal) + § Code Examples (band split + xcorr skeleton) |
| TEST-04 | Golden-file regression — each preset × standard-input with SHA-256 sidecars | § Golden-File Mechanics (Area C) + § Common Pitfalls (dr_wav determinism, endianness, float vs int16 path) |
| TEST-05 | Modulation test — every register modulated (sine, sweep, random walk) during live processing | § Modulation Harness Mechanics (Area E) + § Code Examples (pytest-parametrized over `spu94.Register`) + § Classifiers (modulation-cost + zipper-behavior measurement) |
| TEST-08 | Reproducibility — goldens byte-identical across Docker-pinned CI and host dev | § Reproducibility Mechanics (Area D) + § Standard Stack (debian:bookworm-slim digest) + § Common Pitfalls (determinism environment variables) |
| BUILD-06 | Benchmark harness — pytest-benchmark verifying no hot-path allocations and no pathological timing regressions | § Benchmark + Allocation Gate Mechanics (Area F) + § Code Examples (strace extension of test_no_syscalls) + § Standard Stack (pytest-benchmark 5.2.3) |
| BUILD-08 | Docker-pinned reproducible build environment | § Reproducibility Mechanics (Area D) + § Standard Stack (verified digest + toolchain audit) |
| DOCS-02 | `docs/LEVERS-CATALOG.md` — 35 rows × {musical role, modulation cost, zipper behavior, M4 lever grouping} | § LEVERS-CATALOG Mechanics (Area E) + § Classifiers + § Architecture Patterns (human/machine column split) |
| DOCS-03 | `docs/BIBLIOGRAPHY.md` additive + cleanup pass | § Bibliography Cleanup Mechanics (Area G) + § Code Examples (DECISIONS.md ↔ BIB-xxx cross-ref script) |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

No project-root `./CLAUDE.md` file exists in this repository (verified via `Read` — file not found). The user-global `~/.claude/CLAUDE.md` directive set applies:

- **Hands-on guided walkthrough posture** for deployed-system tasks. Phase 7 is not a deployed-system operation (it's CI + verification), but any manual UAT step (e.g., human spot-check a golden `.wav`) should be presented as a one-step-at-a-time checklist rather than a batch.
- **No autonomous agents on deployed systems.** Phase 7 CI is self-contained; no autonomous-agent work lands on Anthony's workstation.

Additional project-level constraints (sourced from `.planning/PROJECT.md` — treated with equal authority to a CLAUDE.md since the project constraints are explicit there):

- **Paraphrase-not-transcribe.** BIBLIOGRAPHY cleanup must not copy nocash prose into SPU-94 docs. LEVERS-CATALOG musical-role column is written in Anthony's voice, citing `BIB-xxx` facts only.
- **GPL sources not read as primary material.** lv2-psx-reverb is consulted as a binary witness only — its source code is not read during harness development. Confirm in ADR.
- **Plain C99/C11 core, no heap / no locks / no syscalls on hot path.** Phase 7 verifies (doesn't widen) these constraints via the allocation gate.
- **Linux-first.** Phase 7 reproducibility container is Linux/Debian only. Windows/macOS/aarch64 reproducibility containers are deferred.
- **Bit-faithful where spec is explicit; documented where it isn't.** The PS1 SPU has no parameter smoothing; audio-rate modulation character (zipper, stepping) is *accepted*, catalogued, never smoothed in the core. D-18 reinterpretation codifies this.

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `lilv-utils` (provides `lv2apply`) | 0.24.26-1 on host (Ubuntu 25.10) [VERIFIED: `apt-cache show`] | Headless LV2 plugin host CLI: `lv2apply -i in.wav -o out.wav <uri>` | Drobilla / David Robillard's reference LV2 utilities; shipped in every Debian/Ubuntu since bookworm; zero daemon, zero audio-server, no JACK/ALSA required; designed for offline render |
| `libsndfile1-dev` | ≥ 1.2 (transitive via `lilv-utils`) [CITED: manpages.debian.org/lv2apply] | WAV file I/O inside `lv2apply` (both read input and write output) | Already used by `lv2apply`; independent of SPU-94's own dr_wav vendored path — lv2's output WAV is written by libsndfile, SPU-94's output WAV is written by dr_wav, the two sides of the witness diff use different WAV writers by design (neutral-ground comparison) |
| `scipy` (Python) | ≥ 1.11 (provides `scipy.signal.correlate`, `butter`, `sosfiltfilt`, `chirp`) [VERIFIED: scipy.signal API stable since 1.x] | Band-split (10 kHz Butterworth via SOS form), cross-correlation lag detection, frequency-sweep input generation | Industry-standard DSP toolkit in Python; SOS form avoids direct-form-II numerical blow-up at 8th order; `sosfiltfilt` gives zero-phase band separation so the split doesn't shift the alignment. [ASSUMED: scipy is already a declared test-time dependency] — actually NOT yet declared; see Wave 0 gaps |
| `pytest-benchmark` (Python) | 5.2.3 (released 2025-11-09) [VERIFIED: pypi.org/project/pytest-benchmark] | Timing measurement for `spu94_process`; writes native `.benchmarks/<machine-id>/NNNN_<name>.json` artifacts; supports comparison to saved baselines | Dominant pytest timing plugin; handles statistical aggregation (min/max/mean/median/stddev/rounds/iterations); native compare-to-saved-baseline workflow via `--benchmark-compare` + `--benchmark-save`/`--benchmark-autosave` |
| `strace` | 6.16 on host [VERIFIED: `strace --version`]; ≥ 5.x acceptable inside container | Filter for heap syscalls (`brk`, `mmap`, `munmap`, `mremap`) inside `spu94_process` hot region | Already used by Phase 5's `test_no_syscalls.sh`; Phase 7 adds a *targeted* filter (this set only, no all-syscalls sweep) |
| `sha256sum` (coreutils) | Standard on any Linux | SHA-256 sidecar generation + diff | RFC 6234 hash; part of every Debian base image; deterministic |
| `docker` | 20.10+ (29.1.3 on host [VERIFIED]) | Reproducibility container runner | Standard CI runtime; `FROM image@sha256:<digest>` syntax is portable across all runtimes |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `numpy` | 2.2.4 on host [VERIFIED] (minimum 1.23 per `pyproject.toml`) | int16 sample generation, accumulator math in witness-diff metrics, numpy RNG for random-walk modulation | All Python harnesses; the binding already requires it |
| `numpy.random.default_rng(seed)` | numpy ≥ 1.17 | Deterministic random-walk modulation source | Use instead of `random.Random()` — fuzz_process.py already uses numpy RNG; stay consistent |
| `scipy.signal.chirp` | scipy.signal stable API | Frequency-sweep input generator | For the "frequency sweep" standard input (D-11); linear or logarithmic sweep across e.g., 20 Hz → 20 kHz over 2 seconds |
| `hashlib.sha256` (Python stdlib) | 3.10+ | SHA-256 sidecar generation from Python (alt to `sha256sum` shell) | `scripts/regenerate_goldens.py` is pure Python so uses hashlib rather than shelling out; shell `sha256sum` is for the container's `--check` diff |
| `LV2_PATH` env var | LV2 spec convention [CITED: manpages.debian.org/lv2apply] | Tell `lv2apply` where the lv2-psx-reverb bundle is installed | Set in the harness to the build output dir of lv2-psx-reverb so we don't pollute the system LV2 path |
| `SOURCE_DATE_EPOCH` | reproducible-builds.org convention | Freeze "now" for any timestamp that bleeds into build artifacts | Set in Dockerfile to a fixed UNIX timestamp (e.g., `1704067200` = 2024-01-01 UTC) |
| `LC_ALL=C`, `TZ=UTC` | POSIX | Locale/TZ determinism | Set in Dockerfile and in `regenerate_goldens.py` wrapper so numeric formatting, date stamps, and collation never vary |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `lv2apply` | `jalv.console` | jalv.console needs a JACK server or in-process nframes driver; more moving parts; higher container install footprint. `lv2apply` is purpose-built for file-in/file-out offline render. |
| `lv2apply` | Custom minimal in-process host via `lilv` C API | ~150 lines of C to load bundle, instantiate, activate, run 1 block, cleanup, writeout. More code to maintain; no practical upside unless `lv2apply`'s control-setting syntax turns out too limited (it doesn't — `-c symbol value` is exactly what we need for lv2-psx-reverb's single `preset` port). |
| `lv2apply` | Dockerized Carla engine | Carla has GUI and a plugin-host-framework feel; overkill. |
| `scipy.signal.correlate` FFT mode | `numpy.correlate` | numpy.correlate is O(N²); scipy's FFT mode is O(N log N). At 50 presets × 5 inputs × ≥1 second of stereo 44.1 kHz (N ≥ 88200) the difference is seconds vs. milliseconds per pair. |
| `scipy.signal.sosfiltfilt` 8th-order Butterworth | Linear-phase FIR split | FIR band split preserves phase perfectly but requires a long (≥ 1024-tap) filter for a clean ~10 kHz cutoff at 44.1 kHz. Butterworth SOS is 8 biquads, zero-phase via filtfilt, no ringing at transients. For an *informational* axis (high band) and a *gated* axis (low band) with tolerance policy deferred, either is defensible; Butterworth is the simpler ship. |
| `pytest-benchmark` | `timeit` + hand-rolled JSON | pytest-benchmark already exists, is rock-solid, integrates with pytest's test-discovery, and writes an open format for historical comparison. Hand-rolled is reinventing wheels. |
| Native JSON baseline | Hand-rolled flat JSON (`{preset: {block_size: mean_ns}}`) | pytest-benchmark writes a richer schema (statistics, machine info, git metadata) that is more honest about measurement conditions. Commit the pytest-benchmark JSON verbatim; don't compress into a flat dict. |
| `debian:bookworm-slim` | `debian:trixie-slim` | Trixie became stable in August 2025; bookworm remains in regular support until at least June 2028. Bookworm is the safer pin through the rest of M1. [VERIFIED: debian.org release schedule / Docker Hub tag list] |
| `debian:bookworm-slim` | `ubuntu:24.04` | No advantage; Debian is upstream; bookworm-slim is smaller. |
| `debian:bookworm-slim` | `alpine:3.19` | musl libc has *subtle* determinism differences vs glibc in locale + allocator behavior; dr_wav is glibc-tested via Phase 6 CI. Don't switch libcs under the golden-file gate. |
| Markdown table + YAML frontmatter | Markdown table with explicit `test:` column | YAML frontmatter per section is *more* parseable but *less* skimmable by humans reading the doc. The planner's job is to pick ship-now simplicity: one markdown table per section, one `test:` column per row, one ~50-line Python parser. |

### Installation

**Inside Docker container (`Dockerfile.repro` / D-14):**
```bash
apt-get update && apt-get install -y \
    build-essential cmake ninja-build \
    python3 python3-venv python3-pip python3-numpy python3-scipy python3-pytest \
    python3-pytest-benchmark \
    lilv-utils lv2-dev \
    strace \
    coreutils \
    git \
    ca-certificates
```

**On host dev (if not already present):**
```bash
# System-level (match container)
sudo apt-get install -y lilv-utils lv2-dev python3-scipy python3-pytest-benchmark strace

# OR via venv (if conflicts with system Python)
python3 -m pip install "scipy>=1.11" "pytest-benchmark>=5.2"
```

**Verify on host before planning:**
```bash
lv2apply --version
python3 -c "import scipy.signal; print(scipy.__version__)"
python3 -c "import pytest_benchmark; print(pytest_benchmark.__version__)"
strace --version | head -1
sha256sum --version | head -1
docker --version
```

### Version verification

Performed at research time (2026-04-23):

| Package | Registry version | Status on host |
|---------|------------------|----------------|
| `lilv-utils` | 0.24.26-1 (Ubuntu 25.10 universe) [VERIFIED: `apt-cache show`] | Not installed; `liblilv-0-0` present; install via `apt-get install lilv-utils` |
| `pytest-benchmark` | 5.2.3 (PyPI, 2025-11-09) [VERIFIED: pypi.org/project/pytest-benchmark] | Not installed; apt candidate `python3-pytest-benchmark` 5.1.0-1 |
| `scipy` | 1.14 stable line [ASSUMED] | **NOT INSTALLED ON HOST** [VERIFIED: `ModuleNotFoundError`]; Wave 0 gap — install before harness work |
| `strace` | 6.16 [VERIFIED: `strace --version`] | Present |
| `numpy` | 2.2.4 [VERIFIED: `numpy.__version__`] | Present (installed via pyproject) |
| `docker` | 29.1.3 [VERIFIED: `docker --version`] | Present |
| `sha256sum` (coreutils) | 9.x Debian default | Present |
| `debian:bookworm-slim` digest | `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252` (multi-arch manifest), linux/amd64 single-arch: `sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644`, published 2026-04-22T02:25:05 UTC [VERIFIED: Docker Hub Registry v2 API `hub.docker.com/v2/repositories/library/debian/tags/bookworm-slim/`] | Pinnable today; may be re-pinned later via D-15 ADR when security updates land |
| `lv2-psx-reverb` master tip | commit `424e1e8ee7f780106b005011b036386513c61db3` (2023-09-08) [VERIFIED: GitHub commits view] — NO RELEASE TAGS EXIST on the repo | Pin to this specific SHA. Use git submodule or a `git clone --depth 1 --branch master && git checkout <SHA>` in the witness-diff harness |
| Wayback snapshot `psx-spx.consoledev.net/soundprocessingunitspu/` | `20260114082525` (2026-01-14) — closest to today; server returned 200 + content contains 25 hits for reverb-section tokens [VERIFIED: `curl -sI` + HTML inspection]; earlier stable snapshot `20250614113232` (2025-06-14) also intact [VERIFIED: `curl -sI`] | Use `20260114082525` as the primary spec-paragraph citation snapshot; the older one is a fallback |

## Architecture Patterns

### Recommended Project Structure

Additive to the existing tree (established in Phases 1–6):

```
.planning/phases/07-verification-golden-files-witness-diff-modulation/
├── 07-CONTEXT.md                        # (already written)
├── 07-DISCUSSION-LOG.md                 # (already written)
├── 07-RESEARCH.md                       # (this file)
└── 07-NN-PLAN.md                        # (written by planner)

docs/
├── COVERAGE.md                          # NEW — 3 sections, CI-parsed
├── LEVERS-CATALOG.md                    # NEW — 35 rows, mechanical + hand-written columns
├── DECISIONS.md                         # APPEND — eight ADR-Phase-7-A..H entries
└── BIBLIOGRAPHY.md                      # EDIT — additive + cleanup pass

scripts/
├── regenerate_goldens.py                # NEW — renders 50 goldens via spu94 CLI; writes .wav + .sha256
└── ci/
    ├── check_coverage.py                # NEW — parses COVERAGE.md, verifies every row's named test exists and passes
    ├── witness_diff.py                  # NEW — builds lv2-psx-reverb, renders 50 witness outputs, computes split-band divergence
    ├── hotpath_alloc_gate.sh            # NEW — strace filter for brk/mmap/munmap/mremap in spu94_process call tree
    └── check_bib_crossref.py            # NEW — walks DECISIONS.md, finds unreferenced claims, reports gaps

tests/
├── conformance/                         # NEW — thin pytest layer for per-spec-paragraph tests that don't fit unit/
│   ├── CMakeLists.txt
│   ├── conftest.py
│   ├── test_coverage_map_integrity.py   # meta-test: COVERAGE.md parses, every row has a test:
│   └── test_spec_para_*.py              # per-spec-paragraph tests (one file per snapshot anchor)
├── golden/                              # NEW — 10 presets × 5 inputs = 50 .wav + 50 .sha256
│   ├── off/impulse.wav + impulse.wav.sha256
│   ├── off/white_noise.wav + .sha256
│   ├── off/sine_1khz.wav + .sha256
│   ├── off/silence.wav + .sha256
│   ├── off/sweep.wav + .sha256
│   ├── room/...
│   └── ... (10 preset dirs)
├── benchmarks/                          # NEW — pytest-benchmark harness (outside tests/python/ to keep binding/fuzz separation)
│   ├── CMakeLists.txt
│   ├── test_bench_process.py
│   └── baseline.json                    # committed pytest-benchmark baseline (D-21); updated via explicit `scripts/refresh_baseline.sh` ceremony
└── python/
    ├── modulation_harness.py            # NEW — pytest-parametrized over spu94.Register × {sine,sweep,random_walk}
    └── modulation_report.json           # NEW (gitignored? no — committed so LEVERS-CATALOG regeneration is idempotent)

Dockerfile.repro                         # NEW — pinned debian:bookworm-slim@sha256:... + toolchain install + build + regenerate_goldens.py --check

.github/workflows/ci.yml                 # EDIT — add job `reproducibility` + job `coverage-map-check` + job `hotpath-alloc-gate`
```

### Pattern 1: Markdown-Table-As-Source-Of-Truth

**What:** Machine-parseable markdown tables where each row has an explicit `test:` column. A short Python validator treats the markdown as structured data.

**When to use:** For living documentation that needs both human skimmability and CI enforcement. Fits D-02 exactly.

**Example (sketch):**
```markdown
## Per-Register Coverage

| Register | Type | test:                                                        | Notes |
|----------|------|--------------------------------------------------------------|-------|
| `vIIR`   | I16  | `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic` | |
| `vWALL`  | I16  | `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic` | |
| `dLSAME` | U16  | `tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic` | |
```

**Validator pattern:** regex extracts each `test:` column value → split on `::` into `<file>` + `<test_name>` → check file exists → shell out to `ctest -R <test_name>` or `pytest --co <file>::<test_name>` → fail CI on any miss. See § Code Examples below.

### Pattern 2: Human/Machine Column Split

**What:** Tables where some columns are hand-edited and others are machine-written. The machine-writing script refuses to touch the human columns.

**When to use:** LEVERS-CATALOG.md per D-16. Harness writes modulation-cost and zipper-behavior columns; register-name, musical-role, M4-lever-grouping stay hand-edited.

**Example (sketch):**
```markdown
| Register | Musical role (HAND)      | Modulation cost (AUTO) | Zipper onset (AUTO) | M4 lever (HAND) |
|----------|--------------------------|------------------------|---------------------|-----------------|
| `vIIR`   | IIR tail density         | free                   | ~180 Hz             | Decay           |
```

**Machine-write algorithm:**
1. Parse the existing table (preserve HAND columns verbatim).
2. Rewrite only the AUTO columns from `modulation_report.json`.
3. Round-trip: the script must be idempotent — running it twice in a row produces identical files.
4. Guard: if a HAND column is missing for a register, the script fails with an actionable error ("Register `vFOO` has no musical role — add one before running the harness") rather than filling a default.

### Pattern 3: Fresh-Build Witness With Pinned SHA

**What:** Rather than committing pre-rendered lv2 outputs, each CI run clones lv2-psx-reverb at a frozen commit, builds it, renders, diffs.

**When to use:** D-05. "Tuning fork handed out at every rehearsal."

**Example:**
```bash
# inside witness_diff.py or scripts/ci/build_lv2_witness.sh
LV2_COMMIT=424e1e8ee7f780106b005011b036386513c61db3
WORK=$(mktemp -d)
git clone --quiet https://github.com/ipatix/lv2-psx-reverb "$WORK/src"
git -C "$WORK/src" checkout --quiet "$LV2_COMMIT"
make -C "$WORK/src"
mkdir -p "$WORK/lv2"
cp -r "$WORK/src/psx-reverb.lv2" "$WORK/lv2/"
export LV2_PATH="$WORK/lv2"
lv2apply -i in.wav -o out.wav -c preset 5 'https://github.com/ipatix/lv2-psx-reverb'
```

### Pattern 4: Split-Band Divergence Measurement

**What:** Both SPU-94 and lv2 outputs are split into "low-band ≤ 10 kHz" and "high-band > 10 kHz" via zero-phase band-pass; RMS divergence is computed per band.

**When to use:** D-07. Low-band is the axis where lv2 is a valid witness (per ADR-Phase-4-I); high-band is informational only.

**Algorithm:**
1. Load both WAVs as float32 numpy arrays in [−1, 1) via dr_wav-read + int16-to-float-by-32768 OR via scipy.io.wavfile. (Conversion is lossless for our purposes; see Pitfall 3.)
2. Cross-correlate `correlate(spu, lv2, mode='full', method='fft')` on the low-passed signals, take argmax → integer sample lag.
3. Trim both signals to the aligned window.
4. Apply Butterworth band split:
   - Low: `sos_lp = butter(8, 10000/22050, btype='low', output='sos')` → `sosfiltfilt(sos_lp, x)`
   - High: `sos_hp = butter(8, 10000/22050, btype='high', output='sos')` → `sosfiltfilt(sos_hp, x)`
5. For each band: compute RMS of `(spu_band − lv2_band)` / RMS of `lv2_band` → a dimensionless divergence ratio. Report in dBFS: `20 * log10(diff_rms / ref_rms)`.
6. Write per-preset-per-input row to `witness_report.json`: `{preset, input, alignment_lag_samples, low_band_diff_dbfs, high_band_diff_dbfs}`.

### Anti-Patterns to Avoid

- **Overwriting hand-written LEVERS-CATALOG columns.** The harness must fail loudly if a HAND column is empty; never silently fill defaults. (D-16 discipline.)
- **Transcribing nocash prose into COVERAGE.md.** Cite anchor-only (`#reverb-processing`), never copy the spec's words into the doc. (PROJECT.md licensing posture.)
- **Reading lv2-psx-reverb source to resolve a gray area.** The entire witness-diff design treats lv2 as a black box. If its output ever disagrees with SPU-94 in a confusing way, the investigation path is: (a) print more numbers; (b) run the same input against Mednafen/DuckStation as a second witness; (c) mark the finding unresolved in DECISIONS.md. NOT: read lv2's reverb.c. (D-08 discipline.)
- **Committing the pytest-benchmark results timestamps.** Use `--benchmark-save=baseline` with the stability flag; do NOT commit the daily run outputs. Only the endorsed baseline is committed. (D-21 discipline.)
- **Letting the reproducibility CI job cache `pip install`.** If the image is pinned by digest and the container runs `apt-get install` inside, no `pip install` should happen — all Python deps are apt-provided. This keeps the pin surface exactly one digest.
- **Using `time.time()` or `time.monotonic()` in the benchmark harness.** pytest-benchmark's own `benchmark()` fixture handles clock selection (`time.perf_counter_ns` by default); don't shadow it.
- **Gating on zipper-free audio at fast modulation rates.** D-17 + D-18 explicitly forbid this. Zipper at audio-rate is a characterization output, not a failure. Any test that asserts "output is smooth" is wrong.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| LV2 plugin host | Custom lilv-based C host with port binding, activate/deactivate, block-loop, libsndfile wiring | `lv2apply` from `lilv-utils` | Already exists, is shipped on Debian/Ubuntu, is what LV2 devs test against, has correct port-binding discipline. Writing one is ~150 lines of subtle C. |
| Cross-correlation lag detection | Hand-rolled `for lag in range(-N, N): sum(x[i] * y[i+lag])` | `scipy.signal.correlate(x, y, mode='full', method='fft')` + `np.argmax` | O(N²) vs O(N log N). For N=88200 stereo-second, hand-rolled is seconds per pair; FFT is <100 ms. |
| Butterworth band-split | Hand-rolled biquad cascade in direct-form II | `scipy.signal.butter(N, Wn, btype=..., output='sos')` + `sosfiltfilt` | Direct-form II is numerically fragile at high orders; SOS is stable at 20+ biquads. Zero-phase via filtfilt is two filtering passes with reversed time; trivial for scipy, painful by hand. |
| Linear/log frequency sweep signal | Hand-rolled `np.sin(2π * f(t) * t)` with `f(t) = f0 + (f1-f0)*t/T` | `scipy.signal.chirp(t, f0, T, f1, method='linear'|'logarithmic')` | Correct phase continuity at endpoints, correct log-sweep math, one line. |
| SHA-256 hashing | Custom hash code | `hashlib.sha256` (Python) or `sha256sum` (shell) | Stdlib; portable; deterministic. |
| Timing measurement with warmup + stats | `time.perf_counter_ns` + manual percentile loop | `pytest-benchmark` | Already solved; gives min/max/median/mean/stddev/rounds/iterations/OPS; integrates with ctest via pytest collector; writes historical JSON. |
| Heap-syscall detection | Hand-rolled ptrace or LD_PRELOAD of `malloc` shim | `strace -e trace=brk,mmap,munmap,mremap -f ...` extension of Phase 5's `test_no_syscalls.sh` | ptrace has timing variance; LD_PRELOAD changes the binding surface. strace filter is surgical and already deployed. |
| Coverage-table parser | Full markdown AST via `mistune` or `markdown-it-py` | Regex over the `test:` column in a known table shape | Tables are simple; we control the shape; a regex + line-iterator is 30 lines. Real markdown parser is overkill + dependency creep. |
| Docker image-digest discovery | Shell scripting over `docker manifest inspect` | Set the digest explicitly in the Dockerfile at pin time; refresh via ADR + one-shot shell (`curl hub.docker.com/v2/...`) | The pin is static; the discovery is a one-time research act (which this doc did). No ongoing "latest digest" lookup belongs in CI. |

**Key insight:** Phase 7 is a harness phase. Every line of hand-rolled verification machinery is a line of machinery that *itself* needs verifying. Borrow standard tools aggressively; keep the glue code short.

## Runtime State Inventory

Phase 7 is verification-layer work. It does **not** rename, refactor, migrate, or rebrand any runtime-reflected string. The core `libspu94.so`, the Python binding's IntEnum, the CLI, the preset table, and the ADR log are all unchanged in *identity*; Phase 7 adds new files and appends to existing files, no renames.

- **Stored data:** None — phase adds no databases, stores no persistent user data. The committed artifacts (golden `.wav` + `.sha256`, baseline JSON, modulation report, COVERAGE / LEVERS-CATALOG / BIBLIOGRAPHY files) are version-controlled plain files, not runtime-mutable state.
- **Live service config:** None — no external services involved. CI config is in-repo (`.github/workflows/ci.yml`); Docker config is in-repo (`Dockerfile.repro`).
- **OS-registered state:** None — no OS-level daemons, no Task Scheduler entries, no systemd units, no launchd plists. The reproducibility CI job lives inside GitHub Actions' configured YAML and produces no OS-registered state.
- **Secrets/env vars:** None new. No secrets required for Phase 7; the only env vars referenced (`SPU94_LIB`, `LV2_PATH`, `SOURCE_DATE_EPOCH`, `LC_ALL`, `TZ`) are either already in Phase 5/6 usage or are pure determinism knobs set in-container.
- **Build artifacts / installed packages:** None stale. Phase 6's wheel + editable install are unaffected. Phase 7 adds new Python test files under `tests/` and `scripts/` but does not modify the installed `spu94` package layout.

**Nothing found in any category — verified by reviewing the phase description, all 22 D-XX decisions, and the existing repo structure.** If this finding is wrong, it's because Phase 7 accidentally grows a renaming requirement the CONTEXT.md does not acknowledge; such a discovery must trigger a pre-plan discussion revision.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|-------------|-----------|---------|----------|
| `lv2apply` (`lilv-utils`) | Witness-diff harness (Area B) | ✗ on host | — | Install via `apt-get install lilv-utils lv2-dev`; no reasonable fallback (all alternative LV2 hosts are heavier) |
| `lv2-dev` (for building lv2-psx-reverb from source) | Witness-diff harness builds lv2-psx-reverb fresh (D-05) | Partial — `liblilv-0-0` 0.24.26-1 present [VERIFIED]; `lv2-dev` not confirmed | — | `apt-get install lv2-dev` |
| `scipy` (Python) | Band-split filter + xcorr + chirp (Area B), modulation-rate FFT (Area E) | ✗ on host [VERIFIED: `ModuleNotFoundError`] | — | Install via `apt-get install python3-scipy` OR `pip install scipy>=1.11` |
| `pytest-benchmark` (Python) | Benchmark harness (Area F) | ✗ on host [VERIFIED: `ModuleNotFoundError`] | — | `apt-get install python3-pytest-benchmark` (5.1.0-1 available) OR `pip install pytest-benchmark>=5.2` |
| `strace` | Hot-path allocation gate (Area F) | ✓ on host | 6.16 [VERIFIED] | — |
| `sha256sum` | Golden-file sidecar gen + diff (Area C) | ✓ on host (coreutils) | 9.x [VERIFIED] | — |
| `docker` | Reproducibility container (Area D) | ✓ on host | 29.1.3 [VERIFIED] | — |
| `numpy` | All Python harnesses | ✓ on host | 2.2.4 [VERIFIED] | — |
| `pytest` | Test discovery for all Python harnesses | ✓ on host | 9.0.3 [VERIFIED] | — |
| Wayback snapshot access (`web.archive.org`) | One-time retrieval of pinned spec URL | ✓ [VERIFIED: HTTP HEAD 302 + follow returns HTML with reverb-section content] | 2026-01-14 capture exists | — |
| `lv2-psx-reverb` source (GitHub clone) | Witness-diff harness (D-05 fresh-build) | ✓ reachable | commit `424e1e8ee7f780106b005011b036386513c61db3` (2023-09-08) [VERIFIED: GitHub commits view] | — |
| `debian:bookworm-slim` image | Reproducibility container (D-14) | ✓ [VERIFIED: Docker Hub Registry API returned manifest digest] | multi-arch `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252` (2026-04-22) | — |

**Missing dependencies with no fallback:** *(none — all gaps have an apt or pip path)*

**Missing dependencies with fallback:**
- `scipy`, `pytest-benchmark`, `lilv-utils`/`lv2-dev` are all Wave 0 install steps. Planner should put these as the first task of the first plan (or as a dedicated "environment prep" plan if scope warrants).
- Inside the Docker container these install from apt deterministically; on host, Anthony installs once at the start of Phase 7.

## Common Pitfalls

### Pitfall 1: lv2 Output Is Float32, SPU-94 Output Is Int16
**What goes wrong:** lv2-psx-reverb is an LV2 plugin; LV2 audio ports are spec'd as float32 in [−1, +1). SPU-94 emits int16 as per the PS1 hardware contract. Naïvely diffing them compares apples to oranges.
**Why it happens:** The project has three WAV I/O paths: (1) dr_wav writing int16 on the SPU-94 side, (2) libsndfile writing float32 on the lv2 side (via `lv2apply`), (3) scipy.io.wavfile or dr_wav reading back. If any leg does an undocumented format conversion, the diff measures the conversion, not the algorithm.
**How to avoid:**
- Render both sides as **int16 WAV** on disk. Configure `lv2apply` to write int16 if possible (check `lv2apply --help` inside the container); if it only writes float32, accept that and convert with a *single, explicit* `(x * 32768).astype(int16).clip(-32768, 32767)` step on load.
- Document the conversion path in `witness_diff.py` header with an explicit "the lv2 side is float32 on disk, quantized to int16 at load time; the SPU-94 side is int16 on disk natively" comment.
- For the actual divergence math, convert **both** to float64 in [−1, 1) before band-split and xcorr. int16 → float64 is lossless; float32 → float64 is also lossless; so the divergence numbers are not polluted by quantization noise on the *metric* side.
**Warning signs:** Divergence numbers that change when you vary the output WAV's bit depth but not the algorithm.

### Pitfall 2: lv2-psx-reverb's Preset Port Mapping Is Unknown Until Build Time
**What goes wrong:** We don't know if lv2-psx-reverb exposes its factory presets as (a) a single integer `preset` control port with values 0..9, or (b) ten preset bundles selected via `lv2apply -p <preset_uri>`, or (c) 35 individual register control ports.
**Why it happens:** We decided not to read the source (D-08). We'll find out at build time by running `lv2info https://github.com/ipatix/lv2-psx-reverb` after building the bundle.
**How to avoid:**
- Treat the preset-selection mechanism as *discovery at build time*, not *specified at plan time*. The first task of the witness-diff plan should be: build lv2-psx-reverb, run `lv2info` or `lv2ls -v` on its URI, document the exposed ports in the task output.
- The planner should allocate a task early in the plan for "discover lv2 port layout; document in witness-diff harness docstring."
- If the preset selection is not a clean single integer port, write a thin Python helper that maps `spu94.Preset` → whichever lv2 interface actually exists. Document in a comment.
**Warning signs:** Harness exits 1 saying "unknown control port 'preset'"; `lv2apply -c preset 5` does nothing; lv2 output is identical across all preset IDs.

### Pitfall 3: int16 WAV Round-Trip via dr_wav Is Not Automatically Deterministic Across Locales
**What goes wrong:** dr_wav's WAV writer respects the caller's locale for some numeric formatting in metadata chunks (e.g., `LIST INFO`). If two machines have different locales, two .wav files with identical PCM data can differ in the header by one byte.
**Why it happens:** C stdio libc functions (`snprintf` family) honor `LC_NUMERIC` unless explicitly set to `"C"`.
**How to avoid:**
- Set `LC_ALL=C` and `TZ=UTC` at container startup AND at any host-side invocation of `spu94` CLI.
- Verify dr_wav (vendored at `vendor/dr_wav/`) does NOT write an `ICRD` (creation-date) or `ISFT` (software) metadata chunk — [ASSUMED] it does not, since Phase 6's CI proves reproducibility of the CLI output binary. Verify this assumption with a single sha256sum comparison across two runs.
- Keep the dr_wav version frozen (Phase 6's vendored copy); the D-15 ADR covers a future bump if ever needed.
**Warning signs:** Two golden-file regenerations differ in a 4-byte stretch near the start of the file but not in the PCM data.

### Pitfall 4: pytest-benchmark Autosave Creates an .benchmarks Directory That Isn't Gitignored
**What goes wrong:** `pytest-benchmark --benchmark-autosave` writes to `.benchmarks/<machine-id>/NNNN_<name>.json` in the repo root. If `.benchmarks/` isn't gitignored, developers commit their local measurements and pollute the git history.
**Why it happens:** pytest-benchmark's default behavior; easy to miss.
**How to avoid:**
- Add `.benchmarks/` to `.gitignore` in the Phase 7 plan.
- Put the *committed* baseline at `tests/benchmarks/baseline.json` (explicit path, not the autosave dir); refresh via `pytest --benchmark-json=tests/benchmarks/baseline.json tests/benchmarks/` run only when Anthony endorses a new baseline (D-21).
- CI writes a throwaway JSON artifact to the CI runner's workspace, uploaded as a build artifact for inspection, but NEVER compared automatically to the committed baseline (D-20: report-only).
**Warning signs:** `git status` after running tests shows `.benchmarks/` newly present.

### Pitfall 5: `strace -e trace=mmap` Misses anonymous mmaps on some Linuxes
**What goes wrong:** Phase 5's `test_no_syscalls.sh` traces *all* syscalls and filters; Phase 7's allocation gate filters at strace level via `-e trace=brk,mmap,munmap,mremap`. On some kernels glibc's malloc arena growth uses `mmap` with `MAP_ANONYMOUS` which strace reports as `mmap` — good. But some older glibcs use `brk`-then-`sbrk`; `sbrk` is a glibc function, not a syscall, and actually calls `brk` internally. So `brk` coverage catches it.
**Why it happens:** Heap growth can happen via multiple syscalls depending on glibc version.
**How to avoid:**
- Include ALL of `brk`, `mmap`, `munmap`, `mremap` in the filter. Optionally add `mmap2` for 32-bit compatibility (M1 targets are all 64-bit Linux so this is belt-and-suspenders).
- Keep Phase 5's broader trace-and-filter approach as a cross-check: the narrow filter is the CI gate; the broader trace runs periodically (say, in a separate job) to catch syscalls that should *also* not happen (futex, clock_gettime, read, write, etc.).
- Document in the hotpath_alloc_gate.sh docstring that the allowlist of heap syscalls is deliberately explicit; any new glibc variant that invents a new allocator syscall will need this file updated.
**Warning signs:** Production binary allocates but the gate returns zero hits — the test harness is broken before the DSP is.

### Pitfall 6: lv2apply Doesn't Render a Known-Length Output for a Known-Length Input
**What goes wrong:** LV2 plugins are not guaranteed to be causal or of zero latency. The `lv2apply` output file's length depends on the plugin's reported latency — and some plugins report wrong latency. If the harness expects `len(output) == len(input)` it may fail on a preset that reports a long latency.
**Why it happens:** LV2's latency-reporting contract is advisory, not mandatory.
**How to avoid:**
- Explicitly pad the input with ~2 seconds of trailing silence (equal to the longest expected reverb tail) before feeding to `lv2apply`. SPU-94's side mirrors this with an equal-length `spu94_flush` call that drains the tail.
- In the divergence calculation, trim both signals to the aligned overlap window after cross-correlation, not to a pre-agreed length.
**Warning signs:** "alignment lag" numbers from xcorr are large (> hundreds of samples) or the two signals have very different lengths.

### Pitfall 7: Modulation Harness Writes "catastrophic" For Every Address Register Without Explanation
**What goes wrong:** D-16 says `modulation_cost ∈ {free, sample-quantized, catastrophic}`. A naive classifier assigns `catastrophic` to every d-prefix and m-prefix register because changing them mid-stream causes an audible discontinuity. That's correct-but-useless labeling.
**Why it happens:** The labels are about the *character* of the discontinuity, not just its audibility.
**How to avoid:** Proposed classifier (documented in `modulation_harness.py` docstring):
- **free** — IMMEDIATE-policy gain register (the 12 v-prefix). Writing any value mid-block updates the next tick's gain; no buffer discontinuity; zipper onset is the only characterization axis.
- **sample-quantized** — TICK_LATCHED-policy delay-pointer register (dAPF1/2, dLSAME/dRSAME, dLDIFF/dRDIFF). Writing mid-block stages into the pending slot; the new delay position takes effect at the next 22.05 kHz tick (~45 µs granularity); a smooth sweep produces a staircase at 22.05 kHz, not a clean ramp.
- **catastrophic** — address-base register where the write relocates a memory region whose *past* contents the reverb is still reading from (mBASE, mLSAME/mRSAME, mLDIFF/mRDIFF, mLCOMB1..4/mRCOMB1..4, mLAPF1/2, mRAPF1/2). Changing mid-tail invalidates the reads; audible as a hard click/glitch. The distinguishing mark is that the *character* of the discontinuity is bounded-but-not-musical (it's a buffer re-origin, not a smooth sweep).
- The classifier detects category by combining (a) write policy (IMMEDIATE vs TICK_LATCHED from `spu94_write_policy_table`) and (b) register semantic class (v-prefix vs d-prefix vs m-prefix, derivable from the register name).
- A two-field emission per register: `modulation_cost` (category) + `zipper_onset_hz` (measured — see "zipper-onset metric" below).
**Warning signs:** All 23 u16 registers classified "catastrophic" with no differentiation.

### Pitfall 8: scipy's `sosfiltfilt` at Fs=44100 With Order 8 and Wn=10000/22050 Can Blow Up Transient Response
**What goes wrong:** 8th-order Butterworth has nonzero impulse response tails; filtfilt (forward-backward) doubles the transient. For an impulse input this can spread energy well beyond a sample.
**Why it happens:** Intentional phase-linearity tradeoff.
**How to avoid:**
- Pad the signal with zeros at both ends (`padlen=1024`) so the transient settles before the "real" signal starts.
- Remember this affects the *divergence* calculation's meaning: we're comparing the band-filtered SPU-94 output to the band-filtered lv2 output — both see the same transient. So the ratio is still meaningful even if the per-sample values near the edges are filter-artifact.
- If the impulse-input divergence comes out strange, it's a filter-transient artifact, not a real algorithm disagreement.

### Pitfall 9: GitHub Actions Docker Job Doesn't Cache The Pinned Image — Every Run Pulls 45 MB
**What goes wrong:** Default GHA docker-build doesn't cache. If the reproducibility job pulls bookworm-slim + apt-installs toolchain every push, we burn minutes and bandwidth.
**Why it happens:** Naïve CI.
**How to avoid:**
- Use `docker/setup-buildx-action` + `docker/build-push-action` with `cache-from: type=gha, cache-to: type=gha,mode=max`. OR keep it simple: accept the ~2-minute pull-and-install on every run; with matrix job cancellation concurrency settings already in place, this is acceptable for a small personal project.
- Document the choice in the ADR for D-13/D-14.
**Warning signs:** CI runs trending upward in wall time.

## Code Examples

Verified patterns. Where the `Source:` is [ASSUMED], the planner should validate against the referenced library's current docs before locking the plan.

### Example 1 — COVERAGE.md validator (Python, ~50 lines)

```python
#!/usr/bin/env python3
"""scripts/ci/check_coverage.py — CI gate for docs/COVERAGE.md (D-02).

Parses the three tables in COVERAGE.md, extracts every row's `test:`
column, and verifies (a) the file path exists, (b) the named test
passes under pytest or ctest. Exits non-zero on any gap or failure.
"""
import re
import subprocess
import sys
from pathlib import Path

ROW = re.compile(r'^\|\s*`?[^|]*`?\s*\|[^|]*\|\s*`([^`]+)`\s*\|')

def main() -> int:
    cov = Path("docs/COVERAGE.md").read_text()
    tests = ROW.findall(cov)
    if not tests:
        print("FAIL: no test: entries found in COVERAGE.md", file=sys.stderr)
        return 1
    failures = []
    for spec in tests:
        # Two forms:
        #   tests/unit/reverb/test_reverb_same_iir.c::test_same_iir_basic
        #   tests/python/test_X.py::test_fn
        path, test_name = spec.split("::", 1)
        if not Path(path).exists():
            failures.append((spec, "file not found"))
            continue
        if path.endswith(".c"):
            # ctest known pattern: test name is used as ctest -R arg
            r = subprocess.run(
                ["ctest", "--test-dir", "build", "-R", f"^{test_name}$"],
                capture_output=True
            )
        else:
            r = subprocess.run(
                ["pytest", f"{path}::{test_name}", "-q", "--no-header"],
                capture_output=True
            )
        if r.returncode != 0:
            failures.append((spec, r.stderr.decode()[:200]))
    if failures:
        for spec, why in failures:
            print(f"FAIL: {spec}: {why}", file=sys.stderr)
        return 1
    print(f"PASS: {len(tests)} COVERAGE.md rows all green")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

Source: pattern — [VERIFIED: mirrors the approach used by Phase 1's `scripts/ci/verify-flags.sh` and Phase 6's `verify-readme-sections.sh` — regex + shell-out + per-row pass/fail]

### Example 2 — Witness-diff split-band divergence skeleton

```python
#!/usr/bin/env python3
"""scripts/ci/witness_diff.py — measures SPU-94 vs lv2-psx-reverb.

D-05 fresh-build, D-07 split-band, D-06 measurement-only.
Writes witness_report.json; does NOT gate (tolerance ADR deferred).
"""
import json
import numpy as np
from pathlib import Path
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt, correlate

FS = 44100
SPLIT_HZ = 10000
BUTTER_ORDER = 8

_sos_lp = butter(BUTTER_ORDER, SPLIT_HZ/(FS/2), btype='low', output='sos')
_sos_hp = butter(BUTTER_ORDER, SPLIT_HZ/(FS/2), btype='high', output='sos')

def load_wav(path: Path) -> np.ndarray:
    fs, x = wavfile.read(path)
    assert fs == FS, f"expected {FS} Hz, got {fs} at {path}"
    # x may be int16 or float32 depending on writer; convert to float64 in [-1,1)
    if x.dtype == np.int16:
        x = x.astype(np.float64) / 32768.0
    elif x.dtype == np.float32:
        x = x.astype(np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)  # stereo → mono for divergence metric (L+R/2)
    return x

def aligned_divergence_dbfs(spu: np.ndarray, lv2: np.ndarray):
    # Cross-correlate broadband for lag; use FFT method for O(N log N)
    xc = correlate(spu, lv2, mode='full', method='fft')
    lag = int(np.argmax(xc) - (len(lv2) - 1))
    # Trim to aligned window
    if lag > 0:
        spu_a, lv2_a = spu[lag:], lv2[:len(spu)-lag]
    elif lag < 0:
        spu_a, lv2_a = spu[:len(lv2)+lag], lv2[-lag:]
    else:
        spu_a, lv2_a = spu, lv2
    n = min(len(spu_a), len(lv2_a))
    spu_a, lv2_a = spu_a[:n], lv2_a[:n]

    def rms_diff_dbfs(a: np.ndarray, b: np.ndarray) -> float:
        diff = sosfiltfilt  # placeholder; inline below
        return 0.0  # see full impl in plan task

    # Low band
    spu_lp = sosfiltfilt(_sos_lp, spu_a)
    lv2_lp = sosfiltfilt(_sos_lp, lv2_a)
    lp_diff_rms = np.sqrt(np.mean((spu_lp - lv2_lp)**2))
    lp_ref_rms  = np.sqrt(np.mean(lv2_lp**2)) + 1e-12
    lp_dbfs = 20 * np.log10(lp_diff_rms / lp_ref_rms)

    # High band
    spu_hp = sosfiltfilt(_sos_hp, spu_a)
    lv2_hp = sosfiltfilt(_sos_hp, lv2_a)
    hp_diff_rms = np.sqrt(np.mean((spu_hp - lv2_hp)**2))
    hp_ref_rms  = np.sqrt(np.mean(lv2_hp**2)) + 1e-12
    hp_dbfs = 20 * np.log10(hp_diff_rms / hp_ref_rms)

    return {"alignment_lag_samples": lag,
            "low_band_diff_dbfs": float(lp_dbfs),
            "high_band_diff_dbfs": float(hp_dbfs)}

if __name__ == "__main__":
    # Build lv2 fresh, render 50 pairs, emit witness_report.json
    # (full driver code goes here; skeleton elided for brevity)
    pass
```

Source: [CITED: scipy.signal docs — `butter`, `sosfiltfilt`, `correlate`]; [VERIFIED: scipy.io.wavfile is the stdlib-adjacent WAV reader; accepts int16 and float32 files]

### Example 3 — pytest-parametrized modulation harness over `spu94.Register`

```python
# tests/python/modulation_harness.py — TEST-05 per D-16, D-17, D-19.
import json
from pathlib import Path
import numpy as np
import pytest
from spu94 import SPU94, Register
from spu94._binding import _lib

RATES_HZ = [0.1, 1, 10, 100, 500, 1000, 2000, 5000, 11025]  # D-19 discretion
SWEEP = (0.1, 11025.0)
SEED = 0x1094_DADA
BLOCK = 1024
DURATION_SEC = 1.0

@pytest.fixture(scope="module")
def spu():
    s = SPU94()
    s.load_preset("hall")  # Hall is a known-audible baseline
    yield s

@pytest.fixture(scope="module")
def input_audio():
    t = np.arange(int(DURATION_SEC * 44100))
    return (np.sin(2*np.pi*440*t/44100) * 16000).astype(np.int16)

@pytest.mark.parametrize("mode", ["sine", "sweep", "random_walk"])
@pytest.mark.parametrize("reg", list(Register),
                         ids=[r.name for r in Register])
def test_register_under_modulation(spu, input_audio, reg, mode, tmp_path,
                                   request):
    """D-17 gate: stability + determinism at every modulation rate."""
    rng = np.random.default_rng(SEED)

    def modulation_stream(n_samples, mode, rate_hz):
        t = np.arange(n_samples) / 44100.0
        if mode == "sine":
            # Modulate reg across its valid int16 range at rate_hz
            return (16000 * np.sin(2*np.pi*rate_hz*t)).astype(np.int16)
        elif mode == "sweep":
            from scipy.signal import chirp
            s = chirp(t, SWEEP[0], DURATION_SEC, SWEEP[1], method='logarithmic')
            return (16000 * s).astype(np.int16)
        else:  # random_walk
            walk = np.cumsum(rng.integers(-500, 500, size=n_samples))
            return np.clip(walk, -16000, 16000).astype(np.int16)

    results = []
    for rate_hz in (RATES_HZ if mode != "sweep" else [None]):
        Lout = np.zeros_like(input_audio)
        Rout = np.zeros_like(input_audio)
        mod_values = modulation_stream(len(input_audio), mode, rate_hz)

        # Drive SPU-94 in 1024-sample blocks; write `reg` each block
        for block_start in range(0, len(input_audio), BLOCK):
            s = block_start
            e = min(s + BLOCK, len(input_audio))
            mv = int(mod_values[s])
            # Use the right typed setter based on register signedness
            # ... set_reg_{i16,u16}(reg, mv) ...
            spu.process(input_audio[s:e], input_audio[s:e],
                        Lout[s:e], Rout[s:e])

        # GATE 1: stability
        assert np.all(np.isfinite(Lout.astype(np.float64))), \
               f"{reg.name}/{mode}@{rate_hz}: NaN/inf in output"
        assert Lout.min() >= np.iinfo(np.int16).min
        assert Lout.max() <= np.iinfo(np.int16).max

        # GATE 2: determinism — second run with same seed produces identical output
        # (re-init state; replay; assert byte-exact equality)
        # ... (elided for skeleton) ...

        # Characterize (non-gating, written to report)
        # zipper_onset_hz: at what modulation rate does sample-to-sample
        # RMS delta exceed a fixed threshold (e.g., 0.01 full-scale)?
        # modulation_cost: derive from register's write-policy class +
        # name prefix (v/d/m).
        results.append({...})

    # Append per-register results to the modulation report
    report = Path("tests/python/modulation_report.json")
    existing = json.loads(report.read_text()) if report.exists() else {}
    existing.setdefault(mode, {})[reg.name] = results
    report.write_text(json.dumps(existing, indent=2, sort_keys=True))
```

Source: [VERIFIED: `spu94.Register` is a runtime-reflected IntEnum per Phase 6 CONTEXT.md D-06; iterating `list(Register)` yields all 35 registers]; [CITED: pytest.parametrize docs — standard parametrize-over-IntEnum pattern]

### Example 4 — Hot-path allocation gate via strace filter

```bash
#!/usr/bin/env bash
# scripts/ci/hotpath_alloc_gate.sh — D-20 hard gate.
# Extends tests/rt_safety/test_no_syscalls.sh with a narrow heap-syscall filter.
#
# Exit 0 = no heap syscalls in spu94_process steady state.
# Exit 1 = at least one heap syscall detected.

set -euo pipefail
: "${SYSCALLS_BIN:?SYSCALLS_BIN env var required}"
: "${STRACE_EXE:?STRACE_EXE env var required}"

LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT

# Targeted filter — catches all heap growth paths on 64-bit Linux glibc.
"$STRACE_EXE" -f -e trace=brk,mmap,munmap,mremap -o "$LOG" "$SYSCALLS_BIN"

MARKER_LINES=$(grep -n '\-\-\- SIGUSR1' "$LOG" | cut -d: -f1)
START=$(echo "$MARKER_LINES" | head -1)
END=$(echo   "$MARKER_LINES" | tail -1)

# Count heap syscalls in [START+1, END-1] window
HEAP_HITS=$(sed -n "$((START+1)),$((END-1))p" "$LOG" | \
    grep -cE '(brk|mmap|munmap|mremap)\(' || true)

if [ "$HEAP_HITS" -ne 0 ]; then
    echo "FAIL: $HEAP_HITS heap syscall(s) in spu94_process steady state" >&2
    sed -n "$((START+1)),$((END-1))p" "$LOG" | \
        grep -E '(brk|mmap|munmap|mremap)\(' | head -20 >&2
    exit 1
fi
echo "PASS: zero heap syscalls in spu94_process hot path"
```

Source: [VERIFIED: extends existing `tests/rt_safety/test_no_syscalls.sh` pattern; `-e trace=...` is standard strace syntax]

### Example 5 — pytest-benchmark timing harness (report-only)

```python
# tests/benchmarks/test_bench_process.py — BUILD-06 timing track.
import numpy as np
import pytest
from spu94 import SPU94

@pytest.fixture(scope="module")
def spu_hall():
    s = SPU94(); s.load_preset("hall"); return s

@pytest.fixture(scope="module")
def block1024():
    rng = np.random.default_rng(42)
    return rng.integers(-16000, 16000, size=1024, dtype=np.int16)

@pytest.mark.benchmark(group="process", min_rounds=100, warmup=True,
                       disable_gc=True)
@pytest.mark.parametrize("preset_name",
    ["off", "room", "studio_a", "studio_b", "studio_c",
     "hall", "half_echo", "space_echo", "echo", "delay"])
def test_bench_process_1024(benchmark, preset_name, block1024):
    s = SPU94(); s.load_preset(preset_name)
    Lout = np.zeros(1024, dtype=np.int16)
    Rout = np.zeros(1024, dtype=np.int16)
    benchmark(s.process, block1024, block1024, Lout, Rout)
```

Running:
```bash
pytest tests/benchmarks/ --benchmark-json=tests/benchmarks/baseline.json \
                         --benchmark-group-by=param:preset_name
```

Source: [CITED: pytest-benchmark 5.2.3 docs — `@pytest.mark.benchmark` decorator, `benchmark` fixture, `min_rounds`, `warmup`, `disable_gc`]

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hand-rolled LV2 host via lilv C API | `lv2apply` CLI from `lilv-utils` | ~2017 when lv2apply shipped as part of drobilla's utility split from `lilv` | One binary + two flags replaces ~150 lines of C host code |
| `numpy.random.RandomState(seed)` | `numpy.random.default_rng(seed)` | numpy 1.17 (2019) | Modern PRNG (PCG64); guaranteed reproducibility spec; recommended for deterministic seeding |
| `scipy.signal.filtfilt` on `ba=(b,a)` | `scipy.signal.sosfiltfilt` on SOS | scipy 0.16 (2015) | SOS form is numerically stable at order ≥ 8; `ba` form blows up |
| Pre-rendered witness WAVs committed to repo | Fresh-build on every CI run | D-05 decision | No silent staleness; lv2 pin is textual (commit SHA) not binary |
| `debian:bookworm` by tag | `debian:bookworm-slim@sha256:<digest>` | Reproducible-builds discipline standard since ~2020 | Tag drift can't silently change base layer |
| Coverage matrix as mental inventory | Coverage *document* with CI enforcement | D-02 decision | Gaps surface at CI time, not at release audit time |
| `timeit` ad-hoc benchmarking | `pytest-benchmark` integrated into test suite | pytest-benchmark 3.x (2016)+ | Stats + comparison + JSON artifacts as first-class outputs |

**Deprecated/outdated:**
- `numpy.correlate` for large arrays: replaced by `scipy.signal.correlate(..., method='fft')`.
- `random.Random().choice()` for test determinism: replaced by `numpy.random.default_rng(seed).choice()`.
- Pinning Docker images by tag alone: replaced by `image@sha256:<digest>`.
- Markdown tables parsed by AST libraries for simple validator scripts: replaced by regex over known-shape tables.

## Validation Architecture

### Test Framework

Phase 7 *is* the validation phase — every harness it ships is the test infrastructure for every prior phase. The Nyquist gate applies recursively here: each harness needs meta-tests that prove *the harness itself* is correct.

| Property | Value |
|----------|-------|
| Framework | pytest 9.0.3 (host) + ctest (C unit tests, unchanged from Phases 1–6) + pytest-benchmark 5.2.3 (new for Phase 7) |
| Config file | `pyproject.toml` (existing, add `[tool.pytest.ini_options]` extensions for new test dirs); `tests/CMakeLists.txt` (add `add_subdirectory(conformance); add_subdirectory(benchmarks)`) |
| Quick run command | `ctest --test-dir build -R "^(coverage_map\|modulation_harness\|bench_process)$" --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` + `pytest tests/python/ tests/conformance/ tests/benchmarks/ -q` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| TEST-01 | Every nocash-documented behavior has a test; coverage map is CI-enforced | integration (meta) | `python3 scripts/ci/check_coverage.py` | ❌ Wave 0 |
| TEST-03 | Witness-diff measurement produces per-preset divergence numbers | integration | `pytest tests/conformance/test_witness_diff.py::test_witness_report_fresh_build -v` (and the harness itself at `scripts/ci/witness_diff.py`) | ❌ Wave 0 |
| TEST-04 | Golden files exist for all 50 preset × input combos with matching SHA-256 sidecars | smoke | `bash scripts/ci/verify-goldens.sh` (iterates `tests/golden/**/*.wav` + `.sha256`) | ❌ Wave 0 |
| TEST-05 | All 35 registers pass stability + determinism gate in all three modulation modes | parametrized unit | `pytest tests/python/modulation_harness.py -q` (generates 35 × 3 = 105 test cases) | ❌ Wave 0 |
| TEST-08 | Goldens byte-identical across Docker container and host | e2e (CI-only) | `docker run --rm spu94-repro python3 scripts/regenerate_goldens.py --check` | ❌ Wave 0 |
| BUILD-06 | Benchmark timing reported + hot-path allocation gate hard fails on any hit | unit + CI gate | `bash scripts/ci/hotpath_alloc_gate.sh` (gate); `pytest tests/benchmarks/ --benchmark-json=...` (report) | ❌ Wave 0 |
| BUILD-08 | Docker repro container builds SPU-94 from committed source | e2e (CI-only) | `docker build -f Dockerfile.repro -t spu94-repro .` | ❌ Wave 0 |
| DOCS-02 | LEVERS-CATALOG.md has 35 rows; mechanical columns populated by harness; human columns present | structural | `pytest tests/conformance/test_levers_catalog_complete.py -v` (meta-test: every Register appears exactly once; no empty cells) | ❌ Wave 0 |
| DOCS-03 | BIBLIOGRAPHY.md references every ADR claim; polished tone | structural | `pytest tests/conformance/test_bibliography_crossref.py -v` (runs `scripts/ci/check_bib_crossref.py` under pytest) | ❌ Wave 0 |

### Meta-Tests (Validate-the-Validators)

Each Phase 7 harness is itself tested. These meta-tests prevent "the harness is green, so the code must be correct" false-positives.

| Harness | Meta-test |
|---------|-----------|
| `check_coverage.py` | Inject a row into a test copy of COVERAGE.md that names a non-existent file → validator must exit 1. Inject a row that names a real file but a fake test → validator must exit 1. |
| `witness_diff.py` | Run twice with the same pinned lv2 commit and the same SPU-94 build → must produce byte-identical `witness_report.json`. (Determinism of the metric itself.) |
| `modulation_harness.py` | Run twice with the same seed → must produce byte-identical `modulation_report.json`. One register's output manually corrupted by post-hoc writing → harness must detect the determinism failure. |
| `regenerate_goldens.py` | Run with `--check` on a committed golden set → must exit 0. Run with `--check` after flipping a single byte in one `.wav` → must exit 1. |
| `hotpath_alloc_gate.sh` | Link a dummy binary that calls `malloc` inside the SIGUSR1-bracketed region → gate must exit 1 (prove it detects a hit, not just that it runs clean on the real binary). |
| `check_bib_crossref.py` | Inject a fake ADR claim referencing `BIB-999` (nonexistent) → must exit 1. |

### Sampling Rate (Nyquist)

- **Per task commit:** `ctest --test-dir build -R "^<affected-test>$" --output-on-failure` + `pytest <affected-python-tests> -q` (< 30 seconds)
- **Per wave merge:** `ctest --test-dir build --output-on-failure` + `pytest tests/python/ tests/conformance/ -q --ignore=tests/benchmarks` (benchmarks take longer; run them on phase-gate only)
- **Phase gate:** Full ctest + full pytest + benchmark suite green; Docker reproducibility job green; before `/gsd-verify-work`

### Wave 0 Gaps (tests/scaffolding missing at phase start)

- [ ] `scripts/ci/check_coverage.py` — validator for COVERAGE.md (TEST-01 CI gate)
- [ ] `scripts/ci/witness_diff.py` — witness-diff harness driver (TEST-03)
- [ ] `scripts/regenerate_goldens.py` — golden regeneration + `--check` mode (TEST-04 / TEST-08)
- [ ] `scripts/ci/hotpath_alloc_gate.sh` — strace-filtered heap gate (BUILD-06)
- [ ] `scripts/ci/check_bib_crossref.py` — DECISIONS.md ↔ BIBLIOGRAPHY.md cross-reference check (DOCS-03)
- [ ] `tests/conformance/conftest.py` + `tests/conformance/CMakeLists.txt` — new pytest subtree
- [ ] `tests/conformance/test_coverage_map_integrity.py` — COVERAGE.md structure meta-test
- [ ] `tests/conformance/test_levers_catalog_complete.py` — LEVERS-CATALOG structure meta-test
- [ ] `tests/conformance/test_bibliography_crossref.py` — BIBLIOGRAPHY cross-ref meta-test
- [ ] `tests/conformance/test_witness_diff_determinism.py` — witness-diff meta-test (run twice, compare reports)
- [ ] `tests/conformance/test_goldens_present.py` — all 50 goldens + sidecars exist, sidecars match
- [ ] `tests/benchmarks/CMakeLists.txt` + `tests/benchmarks/test_bench_process.py` — pytest-benchmark harness (BUILD-06)
- [ ] `tests/python/modulation_harness.py` — pytest-parametrized modulation test (TEST-05)
- [ ] `tests/golden/<preset>/<input>.wav` + `<input>.wav.sha256` — 50 × 2 = 100 committed artifacts (TEST-04)
- [ ] `Dockerfile.repro` — pinned base image + toolchain install + build + `--check` entrypoint (BUILD-08)
- [ ] `.github/workflows/ci.yml` new jobs — `reproducibility`, `coverage-map-check`, `hotpath-alloc-gate`
- [ ] Framework install on host: `sudo apt-get install -y lilv-utils lv2-dev python3-scipy python3-pytest-benchmark` (or equivalent via pip)

*(Wave 0 is substantial — Phase 7 adds an entire verification layer. The planner should expect 5–7 plans.)*

## Security Domain

**Config default check:** `security_enforcement` is absent from `.planning/config.json` — this would normally mean enabled. However, Phase 7 has an explicit opt-out per CONTEXT.md `<domain>` section: *"Phase 7 has no UI and no external-untrusted-input surface."* This makes the STRIDE analysis essentially empty for this phase. The section is retained for completeness and to document the explicit opt-out.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | Phase 7 has no auth surface |
| V3 Session Management | no | Phase 7 has no sessions |
| V4 Access Control | no | Phase 7 has no access-control surface |
| V5 Input Validation | partial | COVERAGE.md, LEVERS-CATALOG.md, witness_report.json all read committed files under trusted paths; no user-submitted input. Path-validation in `check_coverage.py` (no `..` traversal tricks) is standard defensive programming; no security criticality. |
| V6 Cryptography | partial | SHA-256 (RFC 6234) for golden-file integrity. Not a security primitive (integrity-against-human-typo-and-toolchain-drift, not integrity-against-adversary). `sha256sum` coreutils + Python `hashlib.sha256` — both standard, non-handrolled. |
| V7 Error Handling | partial | Harness scripts emit stderr with actionable messages on any gap; exit codes distinguish gap-type failures (0=pass, 1=gap, other=tool error). |
| V8 Data Protection | no | No sensitive data |
| V9 Communication | no | No network I/O in Phase 7 runtime; `git clone` of lv2-psx-reverb is CI-time fetch, not runtime |
| V10 Malicious Code | partial | lv2-psx-reverb is third-party C code built inside CI. Pin-by-SHA (commit `424e1e8...`) mitigates the "supply-chain rug-pull" risk. SHA-pin the action `actions/checkout` is already done repo-wide. |
| V11 Business Logic | no | No business logic per se |
| V12 Files & Resources | partial | Dockerfile + `regenerate_goldens.py` touch the filesystem. All operations are inside the container or inside the repo tree; no writes to privileged locations. |
| V13 API & Web Services | no | Not applicable |
| V14 Configuration | partial | Docker base-image digest pin (D-14) is the primary supply-chain hardening knob. `SOURCE_DATE_EPOCH` + `LC_ALL=C` + `TZ=UTC` locked in Dockerfile. |

### Known Threat Patterns for {DSP verification harness}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Third-party C dependency (lv2-psx-reverb) runs with full process perms inside CI | T, I, E | Pin by commit SHA (D-05); fresh-build from source = no binary-from-elsewhere injection; run inside isolated `ubuntu-latest` GHA runner; no secrets mounted |
| Docker base image silently updated by upstream tag | T | Pin by manifest digest (D-14); ADR-gated bumps (D-15) |
| Malicious dr_wav or jsmn vendor update | T | Vendored copies are locked at Phase 6 commit; Phase 7 does not touch `vendor/`; any change would show in `git diff` |
| Harness script writes outside the expected output dir (path traversal) | T | `check_coverage.py` validates paths start with `tests/` or `docs/`; never calls `os.system` or shell strings on untrusted input |
| Benchmark numbers leaked from CI machine to public history | I | Baseline JSON is committed in-repo; every endorsement passes Anthony's eyes. No secret machine-id / hostname leakage in pytest-benchmark's metadata (it records CPU model + OS, which is fine). |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | scipy is an acceptable new test-time dependency for the project | Standard Stack — scipy row | Low; aligns with PROJECT.md "Python 3 + numpy + scipy + matplotlib + pytest" stated stack |
| A2 | `lv2apply` supports setting a `preset` control port with integer values 0..9 (the apparent lv2-psx-reverb preset-selection interface) | Pitfall 2 | Medium; lv2-psx-reverb's actual port names are unknown until we run `lv2info` on the built bundle. The plan must include a discovery task. |
| A3 | dr_wav does NOT write locale-dependent metadata chunks (`ICRD`, `ISFT`) | Pitfall 3 | Medium; if wrong, goldens would differ between runs with different locales. Fix: set `LC_ALL=C` at invocation, add a one-time sha256sum comparison across two runs as a sanity check. |
| A4 | Linux glibc's `malloc` heap growth uses `brk` or `mmap` (covered by the strace filter) | Pitfall 5 | Low; standard glibc behavior since ~2010. Musl differs — but Docker image is glibc. |
| A5 | The classifier proposed (free/sample-quantized/catastrophic) from write policy + name prefix is correct | Pitfall 7 | Medium-High; this is the proposed semantic interpretation of D-16's three-value enum. If measurements don't fit these three categories cleanly, a fourth category ("rate-limited sweep") or a continuous onset-frequency field is needed. The harness should allow the category to be overridden manually in LEVERS-CATALOG by an explicit HAND column. |
| A6 | Docker digest for `debian:bookworm-slim` published 2026-04-22 is the "right" pin right now | Standard Stack — debian:bookworm-slim row | Low; Docker Hub API confirmed live. Any subsequent security-patch release would trigger a D-15 ADR bump. |
| A7 | Wayback snapshot `20260114082525` has all reverb-relevant sections intact | Standard Stack — wayback snapshot row | Low; HTTP HEAD returned 200 and body has 25 hits for reverb-section tokens. Manual spot-check during plan-writing confirms section completeness. |
| A8 | pytest-benchmark 5.2.3 is pytest-9-compatible | Standard Stack — pytest-benchmark row | Low; release notes explicitly mention "support for pytest 9.0" (verified via PyPI page). |
| A9 | The lv2-psx-reverb commit `424e1e8...` compiles against an apt-installed `lv2-dev` on bookworm (2023-09-08 source compiled against 2023+ LV2 headers) | Standard Stack — lv2-psx-reverb row | Low; LV2 API has been stable; 3-year gap is small. Plan includes a sanity build step early. |
| A10 | Frequency sweep (the 5th standard input) should use `scipy.signal.chirp` with logarithmic sweep from ~20 Hz to ~20 kHz over ~2 seconds | Claude's Discretion — std input parameters | Low; D-11 explicitly leaves these parameters to Claude's discretion, as long as deterministic and committed. Exact numbers can be locked during planning. |
| A11 | The 10 kHz band-split cutoff matches "above 10 kHz" in ADR-Phase-4-I's frequency-response-exclusion phrasing | Code Examples — witness-diff skeleton | Low; ADR-Phase-4-I explicitly uses ~10 kHz as the exclusion threshold; CONTEXT D-07 repeats it. |
| A12 | Eight ADRs (ADR-Phase-7-A..H) is a reasonable split of the 22 D-XX decisions | ADR plan in § Recommended Project Structure | Low; the split is one ADR per work area (A–G) plus one dedicated ADR for the SC-4 reinterpretation (D-18) since it's semantically distinct. Planner has discretion to adjust; CONTEXT.md says "eight is a reasonable upper bound, fewer if groupings make sense." |

## Open Questions (RESOLVED)

1. **Does lv2-psx-reverb's LV2 bundle expose a clean `preset` integer port or does it require per-register control ports?**
   - What we know: built-once-and-compile-verified via `make`; port layout is discoverable via `lv2info <uri>` or `lv2ls -v`.
   - What's unclear: whether we can match SPU-94's Preset IntEnum 1:1 or need a mapping layer.
   - RESOLVED: First task in the witness-diff plan = build lv2-psx-reverb + run `lv2info` + pin the observed port layout into the harness docstring. If there's no preset port, fall back to setting 35 individual control ports matching SPU-94's preset register values (this requires that lv2-psx-reverb exposes them — which is the most likely case).

2. **Does `lv2apply` write int16 WAV or only float32?**
   - What we know: it uses libsndfile; libsndfile is format-flexible.
   - What's unclear: whether lv2apply passes a format spec to libsndfile on output, or defaults to a fixed format.
   - RESOLVED: Test empirically in the first witness-diff plan task; if float32-only, do the lossless float-to-int16 conversion at load time in `witness_diff.py` and document the conversion path.

3. **What's the right "zipper onset" metric?**
   - What we know: CONTEXT.md D-16 asks for "expected zipper behavior" but doesn't specify a measurement.
   - What's unclear: sample-to-sample delta RMS threshold for "audible stepping"; specific rate where each register first crosses that threshold; whether to report one number per register (the onset) or a curve.
   - RESOLVED: Propose "lowest modulation rate where sample-to-sample RMS delta exceeds 0.01 × input amplitude" as the onset metric. Report one onset-frequency per register per mode. If Anthony disagrees, the metric is hand-editable in LEVERS-CATALOG's AUTO column (overrides the harness output; harness detects user override and leaves it untouched).

4. **Does the tolerance-policy follow-up ADR land inside Phase 7 or as Phase 7.1?**
   - What we know: D-06 defers the tolerance ADR until measurements are reviewed.
   - What's unclear: how long Anthony wants to sit with the numbers before deciding.
   - RESOLVED: Plan Phase 7 with the measurement-only harness as the gate; treat the tolerance ADR as a separate small remediation or in-phase follow-up that lands after the first witness-diff run produces real numbers. The planner should produce a task "witness-diff numbers reviewed; decision: lock tolerance now vs defer to 7.1" as an explicit manual gate.

## Sources

### Primary (HIGH confidence)

- [VERIFIED: Docker Hub API] `https://hub.docker.com/v2/repositories/library/debian/tags/bookworm-slim/` — multi-arch manifest digest `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252` published 2026-04-22; per-arch `linux/amd64` digest `sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644`
- [VERIFIED: GitHub] `https://github.com/ipatix/lv2-psx-reverb/commits/master` — master tip `424e1e8ee7f780106b005011b036386513c61db3` (2023-09-08); no release tags exist on this repo; README confirms "this code doesn't downsample the reverb to 22050 Hz" (matches the ADR-Phase-4-I exclusion basis)
- [VERIFIED: `curl -sI`] `https://web.archive.org/web/20260114082525/https://psx-spx.consoledev.net/soundprocessingunitspu/` — HTTP 200, body contains 25 matches for "spu reverb|reverb examples|reverb formula" tokens; section is intact
- [CITED: Debian manpages] `https://manpages.debian.org/testing/lilv-utils/lv2apply.1.en.html` — lv2apply(1) official docs; `-i in_file`, `-o out_file`, `-c symbol value`, `LV2_PATH` env var
- [VERIFIED: PyPI] `https://pypi.org/project/pytest-benchmark/` — current version 5.2.3 released 2025-11-09; "support for pytest 9.0"
- [CITED: scipy.signal docs] `scipy.signal.butter`, `sosfiltfilt`, `correlate`, `chirp` — stable API since scipy 0.16
- [CITED: reproducible-builds.org] `SOURCE_DATE_EPOCH` spec
- [Existing project artifacts] `.planning/phases/07-verification-golden-files-witness-diff-modulation/07-CONTEXT.md` (22 locked decisions); `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-CONTEXT.md` (ADR-Phase-4-I); `.planning/phases/05-public-api-presets-integration/05-CONTEXT.md` (test_no_syscalls pattern); `tests/rt_safety/test_no_syscalls.sh` + `.c` + `bench_latency.py`; `docs/BIBLIOGRAPHY.md` (existing 146 lines); `include/spu94/spu94.h` + `spu94_registers.h`

### Secondary (MEDIUM confidence)

- [WebSearch + lv2apply manpage] lv2apply capabilities — verified via manpage ✓
- [WebFetch, verified at master commit date] lv2-psx-reverb README — acknowledges the no-downsample behavior
- [Host probe via `apt-cache show`] `lilv-utils` 0.24.26-1 available on Ubuntu 25.10 universe; `liblilv-0-0` present on host
- [Host probe via `dpkg -l`, `command -v`, `python3 -c "import …"`] environment availability for every listed tool

### Tertiary (LOW confidence)

- [ASSUMED, flagged in Assumptions Log] Exact dr_wav metadata-chunk behavior (A3); exact glibc malloc syscall coverage (A4); modulation-cost classifier semantics (A5).

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — every library pinned to a verified version on PyPI/Docker Hub/Git
- Architecture patterns: HIGH — all five patterns match existing Phases 1–6 practice
- Pitfalls: MEDIUM-HIGH — Pitfalls 1, 2, 3, 6 are real edge cases with documented mitigations; 4, 5, 7, 8, 9 are lower-risk but worth flagging
- Coverage map mechanics: HIGH — markdown-regex-validator pattern is proven
- Witness-diff mechanics: MEDIUM-HIGH — depends on A2 (lv2 preset port layout); plan includes discovery task
- Docker pin: HIGH — digest verified against live Docker Hub API
- Wayback snapshot: HIGH — snapshot verified live via HTTP HEAD
- Modulation-harness classifier: MEDIUM — proposed but not yet measured; A5 risk
- Validation architecture section: HIGH — enumerates concrete Wave 0 gaps

**Research date:** 2026-04-23
**Valid until:** 2026-05-23 for the Docker digest (likely re-pinned when a security-patch update lands); 2026-10-23 for everything else (stable-library versions + pinned commits)

---

*Phase 7 research complete. Planner can now produce PLAN.md files.*
