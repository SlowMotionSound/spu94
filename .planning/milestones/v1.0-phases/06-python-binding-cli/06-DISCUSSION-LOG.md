# Phase 6: Python Binding + CLI - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `06-CONTEXT.md` — this log preserves the alternatives considered.

**Date:** 2026-04-21
**Phase:** 06-python-binding-cli
**Areas discussed:** Python API shape, CLI implementation, Register name sync, numpy input contract, `--config` JSON format, Fuzz script migration, README tone + scope, Packaging

---

## Meta: User framing, mid-discussion

Partway through gray-area presentation (after area 1 was introduced), the user reiterated his communication style preference. Recording here because it shaped how the remaining 7 areas were presented:

> "I am not a coder. So much of the technical SWE jargon is lost on me. I also like to digest small bits of information at a time, more like a casual slow human-to-human conversation. It is the ideal pace for me to pick apart ideas and think of new ones. [...] I am a recording and broadcast engineer, with some decent experience with electronics, and high level concepts."

Memory updated: `user_profile.md` + `feedback_plain_language_short.md`. Areas 2–8 were presented one at a time in signal-flow / console / patch-bay analogies rather than as dense option blocks.

---

## Area 1 — Python API shape (PYBIND-01, PYBIND-04)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Raw panel (thin ctypes mirror) | Every C function exposed 1:1 — user passes state handle to each call. Most honest / transparent. Matches C-side posture. Existing fuzz scripts already use this shape. | |
| B. Tidy channel strip (class-based) | One `SPU94` class holds the state handle; methods forward to the underlying C functions. Shorter, more Pythonic. Wraps the state behind Python convention. | |
| C. Both (raw as primary, class as sugar) | Both A and B are public. The class is a few lines of Python forwarding into the raw layer — not a second implementation, just syntactic sugar. | ✓ |

**User's choice:** C (expose both). User noted: "I'm not sure this really has much effect on an end user."

**Notes:** User was right — the end user of the library barely notices this choice. It's a developer-ergonomics call. The choice of "expose both" is cheap (the class is ~8 lines of Python forwarding) and accommodates both the existing fuzz scripts (raw panel) and new users (class).

Mid-area: user asked "what they look like in terms of code?" — I produced full side-by-side code examples in response; user clarified he had been asking a yes/no confirmation question, not requesting the side-by-side. Memory updated: `feedback_confirm_before_producing.md`.

---

## Area 2 — CLI implementation language (CLI-01, CLI-02, CLI-03, CLI-04)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Native C binary | `spu94` is a small C program compiled via CMake, with dr_wav vendored in and linked to the binary only. No Python / numpy runtime dep. Standalone render box. | ✓ |
| B. Python entry_point | `spu94` is a Python command from `pip install`. Uses the Python binding for reverb; WAV I/O on the Python side (scipy / soundfile / stdlib `wave`). Requires Python + numpy to run. | |

**User's choice:** A (native C binary), with a small Python entry_point shim (~5 lines) that shells out to the binary for `pip install` users.

**Notes:** dr_wav is a C library; placing it on the C side has zero ceremony. The standalone render tool stays usable without Python, matching the long-term "C core portable, Python is tooling" posture. Anyone wanting to *script* (batch jobs, plots, JSON output) uses the Python binding directly, not a CLI.

---

## Area 3 — Register name sync + struct drift detection (PYBIND-03, PYBIND-05)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Runtime reflection | At `import spu94`, Python walks the C library via `spu94_reg_name(i)` + `spu94_reg_hw_offset(i)` for i in 0..SPU94_REG__COUNT-1. Builds the IntEnum dynamically. Live library is authoritative. | ✓ |
| B. Parse C header at build time | A codegen script reads the C enum block via regex, writes a Python file with literal values. Python source is statically readable; script is brittle to header style changes. | |
| C. Hardcode + assert-match | Type the 35 names/IDs into Python by hand. At startup, compare against runtime reflection; error out on mismatch. Self-contained but forces dual maintenance. | |

**User's choice:** A (runtime reflection).

**Notes:** The `spu94_reg_name` and `spu94_reg_hw_offset` lookup functions were added in Phase 2 partly for this purpose. Single source of truth is the live library — Python has no parallel typed list that could drift. Same mechanism catches `PYBIND-05` struct drift: `spu94_state_size()` queried at import, used as the reference for any size-dependent code.

---

## Area 4 — numpy input contract (PYBIND-02)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Strict | Require int16 C-contiguous arrays. Wrong dtype or layout → clear error. Zero-copy always. No per-call conversion. | ✓ |
| B. Forgiving | Accept any array-like; silently convert to int16 / contig internally if needed. "Just works" but hides a conversion step. | |
| C. Hybrid | Strict fast path; off-spec input triggers a warning + auto-convert. | |

**User's choice:** A (strict).

**Notes:** User raised mid-discussion: "The original PS1 converted automatically?" — clarified during discuss that no, the PS1 SPU has no conversion layer because it has no non-int16 audio format to convert from. Everything inside the hardware is 16-bit signed integer, top to bottom. Strict binding is *more* faithful to the original hardware's posture, not less. A forgiving binding would invent a conversion the hardware never had — exactly the kind of "helpful magic" the rest of the project is designed to avoid. Clear error messages ("use `arr.astype(np.int16)` first") keep strict nearly as ergonomic as auto-convert without lying. Recorded in CONTEXT.md as D-11 for future reference.

User also asked a follow-up to make sure the decision applied to all use cases (file render and live realtime plugin alike), not just WAV processing. Confirmed: the decision is about the Python-to-C gateway, not about file-vs-live. The gateway is the same in all cases.

---

## Area 5 — `--config preset.json` JSON schema (CLI-02)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Flat register map | `{ "mBASE": "0x3F00", "vIIR": -8000, ... (35 entries) ... }`. Every register required. | |
| B. Preset + overrides | `{ "base": "hall", "overrides": { "vIIR": -8000, "dCOMB1": "0x1000" } }`. Start from a named preset, tweak only specific registers. | |
| C. Support both (auto-detect by `"base"` key) | JSON parser checks for a `"base"` key — presence → override shape, absence → flat map. Both work. | ✓ |

**User's choice:** C (support both).

**Notes:** README prioritizes the override shape as the everyday example; flat is documented for golden-file reproduction and exact specification use.

---

## Area 6 — Fuzz script migration

| Option | Description | Selected |
|--------|-------------|----------|
| A. Migrate all four | `fuzz_buffer.py`, `fuzz_reverb.py`, `fuzz_fir.py`, `fuzz_process.py` drop their hand-typed constants; import from the new binding. Single source of truth. | ✓ |
| B. Leave them alone | Working code stays working. Hand-typed lists and the binding's reflected list become two parallel truths. | |

**User's choice:** A (migrate all four).

**Notes:** Existing scripts literally contain comments saying "Phase 6 replaces this file's hand-synced enum IDs with ctypes IntEnum derived from the C header at import time." Phase 6 is when that happens. Struct-internal offsets that have no public C accessor (e.g., `PENDING_MASK_OFFSET = 160` in fuzz_process.py) remain hand-typed in those scripts per D-08.

---

## Area 7 — README tone + scope (DOCS-04)

### Tone dimension

| Option | Description | Selected |
|--------|-------------|----------|
| Polished | Confident product-doc voice. What the library does, matter-of-factly. No apologetic hedging. | ✓ |
| Honest / early-stage | "WIP, pre-1.0" framing. Taste-driven engineer vibe. | |
| Deep technical | Wall of text about fixed-point truncation and register semantics. For the dedicated reader. | |

### Scope dimension

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal (~1 screen) | What it is, build, usage, license. | |
| Medium (~2 screens) | Adds status banner, acknowledgments, links to DECISIONS / BIBLIOGRAPHY. | |
| Extensive (~4+ screens) | Adds roadmap, architecture overview, DSP-curious technical section, contributing guide. | ✓ |

**User's choice:** Polished tone, extensive scope.

**Notes:** User initially said "polished tone, medium scope," then reconsidered with "I actually kind of want all three tones in there to some extent," then settled on "honestly, lets do polished and extensive scope." The extensive scope naturally accommodates all three earlier-mentioned tonal textures (polished hero + honest status block + deep-technical DSP-curious section) — but the *surrounding voice* is polished throughout. Memory updated: `feedback_user_facing_docs_polished.md`.

---

## Area 8 — Packaging (PYBIND-06)

Presented as "Claude picks, no taste dimension." User approved: "just pick and lets move on."

Picks (all locked):

| Decision | Value | Rationale |
|----------|-------|-----------|
| manylinux tag | manylinux_2_28 | Modern baseline (glibc 2.28+, Ubuntu 20.04+, Debian 11+, RHEL 8+). manylinux2014 is CentOS 7-based and EOL. |
| Python minimum | 3.10+ | Matches existing `find_package(Python3 3.10 REQUIRED)` in tests/python/CMakeLists.txt. |
| Wheel-per-platform | One, not per-Python-version | Binding is pure ctypes (no Python C API), so same wheel works across all 3.10+ minor versions. Tag: `py3-none-manylinux_2_28_x86_64`. |
| Wheel layout | `libspu94.so` + `spu94` binary inside `spu94/` package dir | Standard scikit-build-core convention. `__init__.py` finds library by relative path. |
| Build config location | `pyproject.toml` with `[tool.scikit-build]` + `[tool.cibuildwheel]` | Central, declarative, standard Python packaging. |

---

## Claude's Discretion (accumulated during discussion)

Planner has latitude on:
- Exact module function names (`process` vs `process_block` vs `run_block`) and class names (`SPU94` vs `Reverb`).
- Internal layout of `python/spu94/` — one monolithic `__init__.py` vs split into `_binding.py` / `api.py` / `reverb.py` / `presets.py` / `cli.py` (suggestion: split).
- CLI source path (`src/cli/main.c` vs `tools/spu94/main.c`).
- Whether `SPU94` class supports `__enter__` / `__exit__` context manager (recommendation: yes).
- ASCII signal-flow diagram vs prose for the README's architecture section.
- cibuildwheel test-command specifics and whether to ship a `[dev]` extra.
- Number and split of ADRs appended to `docs/DECISIONS.md`.

See `06-CONTEXT.md` § "Claude's Discretion" for the full list.

---

## Deferred Ideas (summary; full detail in CONTEXT.md)

- Windows / macOS / aarch64 / musllinux wheels — post-M1 platform support.
- Witness-diff harness, golden-file tests, modulation test per register — Phase 7.
- JUCE/VST3/AU/LV2 plugin, named musical levers, parameter smoothing, preset morph — Milestone 4.
- MCU cross-compile smoke test — Phase 8.
- Hardware validation against original PS1 silicon — Milestone 5.
- Direct exposure of internal Q15 helpers (`q15_mul_truncate`, `sat_s16`) — explicitly rejected; out of scope.

---

## Discussion-flow notes

- Eight decision areas completed without checkpoint interruption. `06-DISCUSS-CHECKPOINT.json` was not written (not needed — no session loss).
- Four main areas (1-4) were presented first via a single plain-text multi-area list (per user's prior `feedback_askuserquestion_cutoff.md` preference). User replied "We can discuss all" + reiterated communication style; remaining 7 areas presented one-at-a-time in signal-flow analogies.
- Four smaller items (5-8) were covered at a faster "let's run through them all quickly" pace per user's explicit request.
- Two tool-call habits corrected mid-discussion and recorded to memory (`feedback_confirm_before_producing.md`, `feedback_user_facing_docs_polished.md`).

---

*End of discussion log.*
