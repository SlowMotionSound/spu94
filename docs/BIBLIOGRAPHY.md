# SPU-94 — Bibliography

Facts-only citations for external sources consulted in SPU-94 research
and implementation. Every entry is paraphrased in SPU-94's own docs;
numerical facts (coefficient integers, register addresses, bit-layout
tables, sample rates) are reproduced verbatim as uncopyrightable facts.
Per PROJECT.md licensing posture, GPL emulator source code is off-limits
as a primary input — GPL binaries appear here as behavioral witnesses
whose output is consulted, whose source is not.

Entries are clustered into four tiers that reflect how SPU-94 consumes
each source:

- **Primary Sources** — the spec and its direct anchors; where
  SPU-94's algorithm comes from.
- **Secondary Sources** — community paraphrases and structural
  corroboration; useful when the primary is silent or ambiguous.
- **Witness Binaries** — independent implementations whose output
  audio is diffed against SPU-94's; source code is never read.
- **Tooling References** — build, test, and CI machinery; facts-only
  anchors to external specifications SPU-94 interoperates with.

## Primary Sources

### BIB-001: nocash PSX SPU documentation
- **URL:** https://problemkaputt.de/psx-spx.htm (original author's hosting)
  + https://psx-spx.consoledev.net/soundprocessingunitspu/ (community render)
- **Author:** Martin "nocash" Korth + psx-spx community contributors
- **Used for:** SPU reverb formula (stage order, pseudocode, vIIR=-0x8000
  anomaly description, mBASE side-effect, BufferAddress wrap formula,
  22.05 kHz reverb rate, L/R time-multiplex, 39-tap FIR structure). The
  primary spec SPU-94 implements.
- **Caveat:** Treated as a factual reference (uncopyrightable facts).
  Full prose passages and table captions are paraphrased, never
  transcribed. Phase-7-era citations pin the wayback snapshot in BIB-015
  for citation stability.

### BIB-005: PSX-SPX Reverb Buffer Resampling coefficient table (published form)
- **URL:** https://psx-spx.consoledev.net/soundprocessingunitspu/#reverb-buffer-resampling
- **Used for:** The 39-tap half-band FIR coefficient integer values for
  the 44.1 ↔ 22.05 kHz reverb resampling boundary.
- **Provenance note:** psx-spx transcribes these values from the
  forums.bannister.org SCPH-5501 hardware readout (BIB-006). Every
  published source that lists the 39 integer values traces back to this
  single hardware reading. See Phase 4's coefficient provenance audit
  and ADR-Phase-4-I for the one-source-with-mirrors discipline.

### BIB-006: forums.bannister.org PS1 SPU FIR Coefficients thread
- **URL:** https://forums.bannister.org/ubbthreads.php?ubb=showflat&Number=71222
- **Used for:** Primary hardware readout of the 39-tap FIR coefficient
  values from an SCPH-5501 console — the reading that BIB-005
  transcribes.
- **Provenance note:** Single hardware reading from a single console
  revision. Cross-console confirmation from independent silicon
  revisions is deferred to Milestone 5 hardware validation.

### BIB-011: nocash PSX SPU "SPU Reverb Examples"
- **URL:** https://problemkaputt.de/psx-spx.htm (original author)
  + https://psx-spx.consoledev.net/soundprocessingunitspu/#spureverbexamples
  (community render)
- **Author:** Martin "nocash" Korth + psx-spx community contributors
- **Used for:** Register-value table for the 10 PS1 factory reverb
  presets (Off, Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo,
  Delay), 35 int16 registers per preset. Transcribed as uncopyrightable
  integer facts into `.planning/research/05-preset-values-audit-nocash.csv`
  on 2026-04-20 and cell-equality-verified against BIB-012 by
  `tests/python/verify_preset_sources.py`.
- **Retrieval date:** 2026-04-20.
- **Provenance note:** The nocash table is the primary modern render of
  the PS1 preset matrix; no hardware-readout annotation is present. The
  values are inferred to mirror the Sony LIBSND binary's baked-in
  constants. See the Plan 05 preset-sourcing ADR for the full
  one-source-with-mirrors lineage assessment.

### BIB-013: Sony Psy-Q LIBSND documentation
- **URL:** https://psx.arthus.net/sdk/Psy-Q/DOCS/LibRef/LIBSND.PDF
  (archived mirror of the Psy-Q SDK LibRef PDF)
- **Author:** Sony Computer Entertainment / SN Systems (Psy-Q toolchain)
- **Used for:** Confirmation of the 10-preset count and preset-ID
  ordering (`SPU_REV_MODE_OFF=0` through `SPU_REV_MODE_DELAY=9`) plus
  preset-name mapping. Does not publish per-register values — those
  live in the `libsnd.lib` binary and are off-limits under the project
  licensing posture (GPL-posture-equivalent risk on Sony proprietary
  binaries).
- **Retrieval date:** 2026-04-20.
- **Caveat:** The PDF's prose is Sony copyrighted material; only the
  API-shape facts (preset constant names and numeric ids) are cited
  here, under fair-use interoperability doctrine analogous to reading
  Windows API documentation to implement a Windows program.

### BIB-015: Pinned psx-spx wayback snapshot (Phase 7 citation anchor)
- **URL:** https://web.archive.org/web/20260114082525/https://psx-spx.consoledev.net/soundprocessingunitspu/
- **Snapshot date:** 2026-01-14.
- **Used for:** Citation-stable anchor for Phase 7's `docs/COVERAGE.md`
  per-spec-paragraph section (D-04). Every COVERAGE.md row that points
  at a psx-spx anchor uses this wayback URL so the citations remain
  intelligible even if the live psx-spx site is reorganized, renamed,
  or retired.
- **Caveat:** Snapshot content is facts-only (register layouts, stage
  order, coefficient tables) — the same paraphrase-only discipline
  applied to BIB-001 applies here, because this is a pinned copy of the
  same source.

## Secondary Sources

### BIB-002: jsgroth.dev PS1 SPU series (Part 3 — Reverb)
- **URL:** https://jsgroth.dev/blog/posts/ps1-spu-part-3/
- **Author:** jsgroth
- **Used for:** Behavioral witness for the 22.05 kHz clock alternation,
  independent L/R FIR deque state, the structural note that the same
  39-tap FIR is reused for both decimate and interpolate, and the
  flagged-as-unresolved comb-sum intermediate precision question that
  Phase 3 ADR-0007 later closed.
- **Caveat:** Does not reproduce the 39 coefficient values — points
  readers to psx-spx. Cited as structural corroboration only, not as a
  coefficient source.

### BIB-007: jsgroth.dev PS1 SPU Part 3 — structural corroboration for the FIR
- **URL:** https://jsgroth.dev/blog/posts/ps1-spu-part-3/
- **Used for:** Cross-reference that the 39-tap filter is implemented
  in SPU silicon, that independent L/R deques are the correct state
  model (D-08), and that the same coefficient table is reused at both
  I/O boundaries.
- **Note:** Does not publish the 39 coefficient values. Cited as
  structural / implementation-pattern source, not as a coefficient
  source.

### BIB-012: hitmen c02 SPU documentation
- **URL:** http://hitmen.c02.at/files/docs/psx/spu.txt
  (archived at web.archive.org captures circa 2004 onward)
- **Author:** hitmen PSX demoscene group, ~1999 era
- **Used for:** Independent mid-1990s transcription of the 10 factory
  reverb presets' register values. Corroborating source for BIB-011;
  structurally independent lineage (demoscene community, not directly
  derived from the modern psx-spx doc line). Transcribed into
  `.planning/research/05-preset-values-audit-hitmen.csv` on 2026-04-20.
- **Retrieval date:** 2026-04-20.
- **Note:** Archived document, not actively maintained. Any preset cell
  disagreement between BIB-011 and BIB-012 is resolved via the priority
  chain Sony SDK > nocash > hitmen (see the Plan 05 ADR). Expected
  disagreements: zero. Actual: 16 cells on the Off preset (the m-prefix
  buffer-address registers) — BIB-011 publishes 0x0001 (a defensive
  minimum-offset), BIB-012 publishes 0x0000. Resolved per the priority
  chain in favor of BIB-011 and documented in
  `.planning/research/05-preset-values-audit-resolutions.md`.

## Witness Binaries

Independent implementations whose output audio is consulted as
behavioral evidence. Source code is not read as a primary input; only
rendered WAVs enter SPU-94's diff harnesses and research notes. Phase 7
Plan 03's witness-diff harness consumes BIB-014 (lv2-psx-reverb) as a
built-fresh-each-run binary at a pinned commit SHA.

### BIB-008: lv2-psx-reverb (Phase 4 axis classification)
- **URL:** https://github.com/ipatix/lv2-psx-reverb
- **License:** GPLv3.
- **Used for:** OUT-OF-AXIS witness on the frequency-response axis —
  confirmed by the project's own README, which states the plugin does
  not downsample to 22050 Hz and acknowledges the resulting additional
  brightness above ~10 kHz. Remains a valid IN-AXIS witness for
  reverb-network structural behavior (register semantics, comb/APF
  topology). The Phase 4 context that established this exclusion.

### BIB-009: Mednafen
- **URL:** https://mednafen.github.io/
- **License:** GPLv2.
- **Used for:** IN-AXIS or OUT-OF-AXIS classification on the
  frequency-response axis pending Phase 4's empirical protocol (see
  `04-RESEARCH` — Empirical Mednafen/DuckStation investigation
  protocol). Source code is not read as a primary research input.

### BIB-010: DuckStation
- **URL:** https://github.com/stenzek/duckstation
- **License:** CC-BY-NC-ND (as of September 2024).
- **Used for:** Same axis-classification role as BIB-009. Source code
  is not read as a primary research input; the CC-BY-NC-ND
  no-derivatives clause makes source reading an independent licensing
  hazard on top of the GPL posture.

### BIB-014: lv2-psx-reverb (Phase 7 witness binary, pinned SHA)
- **URL:** https://github.com/ipatix/lv2-psx-reverb
- **Pinned commit:** `424e1e8ee7f780106b005011b036386513c61db3`
  (2023-09-08; re-verified at Phase 7 execute time via `git ls-remote`).
- **License:** GPLv3.
- **Used for:** Behavioral witness binary for Phase 7's witness-diff
  harness (D-05, D-08). Built fresh each CI run at the pinned commit
  SHA; output WAVs consumed via a minimal ctypes LV2 host (described in
  Plan 07-03). The frequency-response axis above ~10 kHz stays excluded
  per ADR-Phase-4-I and ADR-Phase-7-B; Phase 7's witness-diff harness
  uses split-band divergence so the exclusion is honored
  algorithmically, not by convention.
- **Caveat:** Source code is never read as a primary input per
  PROJECT.md. Only the compiled plugin's output WAVs are consulted; the
  ctypes host is written against the public LV2 C API headers, not
  against lv2-psx-reverb's source. The pinned commit SHA is the
  supply-chain gate — any drift fails the build loudly rather than
  introducing a silent witness shift.

## Tooling References

Build, test, and CI tooling that SPU-94's verification infrastructure
depends on. Facts-only — cited for interoperability surface (command-
line flags, attribute names, digest syntax, RFC-specified behavior),
never for prose. Every entry here is a stable anchor whose existence
predates SPU-94 by years or decades.

### BIB-003: Clang UndefinedBehaviorSanitizer reference
- **URL:** https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
- **Used for:** Defining the `integer` check group referenced by
  ADR-0003 — the group that covers signed-overflow, unsigned-overflow,
  shift, integer-divide-by-zero, implicit-truncation, and sign-change.
  SPU-94's UBSan policy is keyed to Clang's named check groups so the
  `no_sanitize` surgical-attribute approach stays legible against the
  upstream taxonomy.
- **Caveat:** Tooling reference, not a content source. Check-group
  names are the stable surface; Clang's explanatory prose is not
  transcribed.

### BIB-004: GCC `no_sanitize` function attribute documentation
- **URL:** https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html
  (search for `no_sanitize`)
- **Used for:** Syntax anchor for ADR-0003's per-function UBSan opt-
  outs. The attribute is read as an interoperability surface shared by
  GCC and Clang; SPU-94 uses the common subset so either compiler
  compiles the core unchanged.
- **Caveat:** Tooling reference; facts-only (attribute name, accepted
  check-group arguments). GCC's explanatory prose is not transcribed.

### BIB-016: pytest-benchmark
- **URL:** https://pypi.org/project/pytest-benchmark/ +
  https://pytest-benchmark.readthedocs.io/
- **Version:** 5.2.3 (verified at Phase 7 research time, 2026-04-22).
- **Used for:** Phase 7's timing harness
  (`tests/benchmarks/test_benchmark.py`, BUILD-06, D-20) in report-only
  mode. The harness times 10 presets × 2 block sizes with warmup, gc
  disabled, min-rounds pinned; summary-stats JSON is committed as
  `benchmark_baselines.json` for human-endorsed drift comparison.
- **Caveat:** Timing data is informational, not gated — CI runner
  jitter would produce noisy false positives. The sibling allocation
  gate (`hotpath_alloc_gate.sh`) is what hard-fails on real regressions;
  pytest-benchmark is the numbers, not the signal.

### BIB-017: strace
- **URL:** https://strace.io/ +
  https://man7.org/linux/man-pages/man1/strace.1.html
- **Used for:** Phase 5's `tests/rt_safety/test_no_syscalls.sh` (no
  syscalls inside `spu94_process`) and Phase 7's
  `tests/rt_safety/hotpath_alloc_gate.sh` (hard-fail CI on any heap
  syscall in the `spu94_process` call tree). Filter
  `brk,mmap,mmap2,munmap,mremap` — mmap2 kept belt-and-suspenders per
  Plan 07-05 Pitfall 5 even on 64-bit hosts.
- **Caveat:** Tooling reference. The filter syntax is the stable
  interop surface; strace's prose is not transcribed.

### BIB-018: LV2 plugin specification
- **URL:** https://lv2plug.in/
- **Used for:** Background for Phase 7's witness-diff harness
  infrastructure. The Plan 07-03 ctypes host is written against the
  public LV2 C API (`LV2_Descriptor`, `LV2_URID_Map`, `lv2/core/lv2.h`
  shapes) — the standard URI-based plugin interface — because
  `lv2apply` from `lilv-utils` does not provide the `urid:map` host
  feature that lv2-psx-reverb requires.
- **Caveat:** SPU-94 itself is not an LV2 plugin in Milestone 1. LV2 is
  consumed here as witness-host-side tooling only; a future M4 plugin
  build may ship an LV2 wrapper alongside the JUCE/VST3/AU variants,
  but that decision is out of scope for the M1 bibliography.

### BIB-019: SHA-256 (RFC 6234)
- **URL:** https://datatracker.ietf.org/doc/html/rfc6234
- **Used for:** Golden-file sidecar integrity hashes (D-09). Every
  `tests/golden/<preset>/<input>.wav` carries a paired `.sha256`
  sidecar produced by GNU coreutils `sha256sum` (on the host) and
  Python's `hashlib.sha256` (in the regenerate / check script); both
  implementations follow RFC 6234 and produce identical digests.
- **Caveat:** Integrity here is against human typo and toolchain drift,
  not adversarial tampering. SHA-256's cryptographic properties are
  stronger than the use case demands; the simplicity of the pair
  (`.wav` + `.sha256`) is what earns its place, not the collision
  resistance.

### BIB-020: Docker (image digest pinning)
- **URL:** https://docs.docker.com/ (specifically
  https://docs.docker.com/reference/cli/docker/image/pull/ for the
  `image@sha256:<digest>` syntax referenced by `Dockerfile.repro`)
- **Used for:** Phase 7's reproducibility container (D-14). The
  `FROM debian:bookworm-slim@sha256:<digest>` syntax pins the exact
  image bytes the container starts from, so the 50-golden corpus
  reproduces byte-for-byte on CI and on Anthony's host. Digest bumps
  require a D-15 successor ADR plus golden regeneration.
- **Caveat:** Tooling reference, not a content source. Brief entry —
  Docker itself is orthogonal to the PS1 SPU algorithm; it is cited
  here because Phase 7's reproducibility claim depends on the digest-
  pin semantic, and that semantic is a Docker surface fact.
