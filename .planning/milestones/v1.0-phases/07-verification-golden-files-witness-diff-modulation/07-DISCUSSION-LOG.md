# Phase 7: Verification — Golden Files, Witness Diff, Modulation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-23
**Phase:** 07-verification-golden-files-witness-diff-modulation
**Areas discussed:** Spec-conformance coverage map, Witness-diff harness, Golden files + standard input set, Reproducibility / Docker pin, Modulation harness + LEVERS-CATALOG, Benchmark + Bibliography polish

---

## Area 1 — Spec-conformance coverage map (TEST-01)

### 1a — Row unit of the coverage table

| Option | Description | Selected |
|--------|-------------|----------|
| Per-register | 35 rows, one per SPU register | |
| Per-behavior | ~15-20 rows, one per algorithmic behavior | |
| Per-spec-paragraph | One row per documented nocash section/paragraph | |
| All three | Three sections in one doc, all three row units | ✓ |

**User's choice:** All three row units.
**Notes:** User wants comprehensiveness. Subsequent confirmation locked in *one file with three sections*, not three separate docs and not a unified registers×behaviors×paragraphs matrix.

### 1b — CI enforcement

| Option | Description | Selected |
|--------|-------------|----------|
| Decorative / human-maintained | Markdown, no CI check; developer updates by hand | |
| CI-enforced | Machine-parseable format; CI verifies named test exists + passes | ✓ |
| Hybrid | Per-register + per-behavior enforced; per-spec-paragraph free-form prose | |

**User's choice:** CI-enforced across all three sections.

### 1c — Existing tests count vs new dedicated tests

| Option | Description | Selected |
|--------|-------------|----------|
| Existing tests count | Audit existing, write new only for genuine gaps | ✓ |
| Write fresh dedicated tests | New tests in `tests/conformance/`, even if behavior is already proven elsewhere | |
| Hybrid by section | Per-register + per-behavior reuse; per-spec-paragraph new | |

**User's choice:** Existing tests count toward coverage; new tests added only for genuine gaps.

### 1d — Spec citation source

| Option | Description | Selected |
|--------|-------------|----------|
| problemkaputt.de original | Nocash's own hosting; one big page | |
| psx-spx.consoledev.net + pinned wayback snapshot | Community render with stable anchors; archive.org snapshot frozen | ✓ |
| Own paraphrase in `docs/SPEC-SUMMARY.md` | Insulates from upstream drift; ongoing maintenance | |

**User's choice:** psx-spx.consoledev.net via pinned wayback snapshot.

---

## Area 2 — Witness-diff harness (TEST-03)

### 2a — How to acquire lv2-psx-reverb output

| Option | Description | Selected |
|--------|-------------|----------|
| Pre-rendered once, committed | "Tuning fork in the drawer forever" — render once, commit WAVs, never rebuild | |
| Built fresh each run | "New tuning fork at every rehearsal" — CI/harness builds lv2 from source on every run, against a pinned version | ✓ |
| Dockerized witness env | "Full calibration lab" — Docker image has lv2 + LV2 host pre-installed | |

**User's choice:** Built fresh each run.
**Notes:** Conversational detour about what "CI", "local dev", and "GitHub" mean in this context. Detour was mental-model-building only, not a decision point. The "fresh each run" decision stands; mechanics (where it runs, which LV2 host, version-pin format) are Claude's discretion.

### 2b — Per-preset divergence tolerance derivation

| Option | Description | Selected |
|--------|-------------|----------|
| Measured-then-frozen per preset | Run once, freeze the numbers in `tolerances.json`, gate against them | |
| Single threshold for all presets | One engineering threshold (e.g., "< -40 dBFS"); preset-agnostic | |
| Report-only, no pass/fail | Print numbers, human reads | |
| Defer until after measurement | Phase 7 ships measurement harness; tolerance policy lands in follow-up ADR after numbers are reviewed | ✓ |

**User's choice:** Defer.
**Notes:** User: "Can we wait to see what level of deviations we are seeing before deciding how strict to be?" Yes — strictness is a guess without data. Phase 7 ships measurement only; tolerance policy ADR follows once numbers are visible.

### 2c — Frequency-response exclusion operationalization

| Option | Description | Selected |
|--------|-------------|----------|
| Low-pass both signals before diff | Strip the spectral axis; equivalent of "test the clocks only at sea level" | |
| Envelope + decay metrics only | Skip sample-wise diff; compare RT60-style time-domain shape | |
| Split-band: gated low + informational high | Both bands measured; low gates, high reports | ✓ |

**User's choice:** Split-band.
**Notes:** User pushed back on initial recommendation (low-pass-only) and was right — split-band gives the same low-band gate insight *plus* a running record of high-band divergence for future inspection. Adopted; ADR captures the link to ADR-Phase-4-I.

---

## Area 3 — Golden files + standard input set (TEST-04, TEST-08)

### 3a — Golden file physical format

| Option | Description | Selected |
|--------|-------------|----------|
| `.wav` + `.sha256` sidecar | Playable; deterministic via vendored dr_wav | ✓ |
| Raw `.bin` int16 + `.sha256` | No header; nothing can drift | |
| Both `.wav` and `.bin` | Belt and suspenders | |

**User's choice:** `.wav` + `.sha256` sidecar.

### 3b — Golden file location

| Option | Description | Selected |
|--------|-------------|----------|
| In-repo `tests/golden/<preset>/<input>.wav` | Alongside the tests; 14 MB total fits easily | ✓ |
| Git LFS | Out-of-band large-file storage; overkill at 14 MB | |
| Separate repo pinned by commit | Cleanest separation, two repos to maintain | |

**User's choice:** In-repo.

### 3c — Standard input set

| Option | Description | Selected |
|--------|-------------|----------|
| Stick with the four (impulse, white noise, 1 kHz sine, silence) | Roadmap-aligned, smallest file count | |
| Add a short burst | Tests transient handling | |
| Add a frequency sweep | Tests time-varying frequency response | ✓ (added to the four) |
| Add real music-like input | Most musical, licensing concerns | |

**User's choice:** Four + frequency sweep (5 inputs total = 50 goldens).
**Notes:** User: "an impulse already seems similar enough to 'short burst drum like'" — agreed; both are transient-in-silence, one being a special case of the other. Frequency sweep adds a genuinely new axis (time-varying frequency) that neither white noise nor sine covers.

### 3d — Regeneration policy

| Option | Description | Selected |
|--------|-------------|----------|
| ADR-gated | Any golden regeneration requires an ADR explaining why | ✓ |
| Free regeneration | Commit message is the only paper trail | |
| Hybrid | Single-preset regeneration free; batch regeneration ADR-gated | |

**User's choice:** ADR-gated regeneration.

---

## Area 4 — Reproducibility / Docker pin (TEST-08, BUILD-08)

### 4a — Where the "second environment" lives

| Option | Description | Selected |
|--------|-------------|----------|
| Local Docker only | Pinned container on workstation; GitHub CI untouched for Phase 7's gate | |
| GitHub CI + Docker inside CI | New CI job in pinned container; runs automatically on every push | ✓ |
| Both | Local + CI | |

**User's choice:** GitHub CI + Docker inside CI.
**Notes:** User's reasoning: "I don't want to have to manually run commands in docker. so where does that leave us? github CI is our 'second environment'?" Confirmed yes. Earlier conversational detour about whether to involve GitHub at all was clarification-only — not a separate decision.

### 4b — Docker pin tightness

| Option | Description | Selected |
|--------|-------------|----------|
| Pin base OS image by digest | One line locks transitive packages | ✓ |
| Pin base + explicit apt versions | Same outcome, redundant lines | |
| Pin every package individually | Max rigor, max maintenance | |

**User's choice:** Pin base image by digest only.

---

## Area 5 — Modulation harness + LEVERS-CATALOG (TEST-05, DOCS-02)

### 5a — Coupling between modulation harness and LEVERS-CATALOG

| Option | Description | Selected |
|--------|-------------|----------|
| Harness writes catalog data mechanically | Modulation cost + zipper behavior columns auto-populated; subjective columns hand-written | ✓ |
| Fully independent | Harness is pass/fail check; catalog entirely hand-authored | |
| Hybrid evidence | Harness output is reference document; human writes all catalog columns | |

**User's choice:** Mechanical harness-to-catalog data flow for measurable columns.

### 5b — What the harness actually gates on

| Option | Description | Selected |
|--------|-------------|----------|
| Crash/corruption only | Stability gate only; everything else cataloged | |
| Stability + ROADMAP SC-4 quality (zipper-free on gain) | Two-tier gate per SC-4 wording | (initially picked, then revised) |
| Full report, no gates | All measurements catalogued; no CI gate | |
| Stability + determinism at every modulation rate (revised) | Final framing after Eurorack context surfaced | ✓ |

**User's choice (revised):** Gate = stability + determinism at every modulation rate (slow through audio-rate). Zipper / stepped behavior at high modulation rates is *character*, catalogued not gated.
**Notes:** User surfaced critical context: "It's mostly standard in the Eurorack format that all parameter control can handle up to audio rate modulation." The PS1 hardware has no parameter smoothing — audio-rate zipper is what the hardware would produce, so a "no zipper at fast modulation" gate would either be impossible to satisfy bit-faithfully or would smuggle smoothing into the core (which is M4/M5 layer work). ROADMAP SC-4's "free of zipper noise on gain-type registers" is *reinterpreted* as "no internal-tick zipper from write-policy violations" (which maps to the determinism gate). ADR captures this reinterpretation.

---

## Area 6 — Benchmark + Bibliography polish (BUILD-06, DOCS-03)

### 6a — Benchmark gating policy

| Option | Description | Selected |
|--------|-------------|----------|
| Gate both | Allocations fail CI; timing-regression-over-X% fails CI | |
| Split: allocations gate, timing report-only | Real bugs gate; noisy stats report | ✓ |
| Report-only everything | No CI gate at all | |

**User's choice:** Split — allocations gate, timing report-only.

### 6b — BIBLIOGRAPHY.md polish depth

| Option | Description | Selected |
|--------|-------------|----------|
| Additive only | New entries only; don't touch existing 146 lines | |
| Additive + cleanup pass | New entries + cross-ref check + tier clustering + tone polish | ✓ |
| Full rewrite as definitive citations ledger | Enterprise-grade restructure | |

**User's choice:** Additive + cleanup pass.

---

## Claude's Discretion (within the locked decisions)

Captured in CONTEXT.md `<decisions>` section. Summary: exact LV2 host mechanism, lv2 version pin, Debian image digest, split-band filter parameters, cross-correlation alignment algorithm, standard input set parameters (durations, seeds, amplitudes, sweep range), modulation harness rate sets, exact `docs/COVERAGE.md` machine-parseable format, harness script naming, ADR count + split, benchmark baseline file format, modulation harness invocation pattern (pytest-parametrized vs standalone).

## Deferred Ideas

Captured in CONTEXT.md `<deferred>` section. Summary:
- Per-preset divergence tolerance values (measure-first ADR after Phase 7 measurements visible)
- Parameter smoothing for smooth-under-audio-rate-modulation (M4/M5 work, not core)
- Named musical levers (M4 work; LEVERS-CATALOG suggests groupings)
- Eurorack CV smoothing layer (M5 work)
- Hardware-witness extension of witness-diff harness (M5 work)
- Windows/macOS/aarch64 reproducibility containers (post-M1)

## Conversational Notes (process)

- User reinforced the analogy protocol mid-conversation: analogies should be *simple, layperson-accessible, genuinely varied across source domains*; not school-monoculture, not studio-monoculture, not specialist (recording-engineer, luthier, ham radio, etc.) by default. Memory updated.
- User pushed back twice on overly purist recommendations (Area 2c split-band vs low-pass-only; Area 5b crash-and-zipper vs stability-and-determinism). Both were correct pushes; revisions adopted.
- User flagged tracking failures when Claude conflated a clarification detour about GitHub CI with a locked decision. Revised flow: clarification questions are not decisions unless explicitly locked.
