# SPU-94 — Bibliography

Facts-only citations for external sources consulted in SPU-94 research.
All prose in SPU-94's own docs paraphrases the sources listed here;
transcribed values are uncopyrightable facts (coefficient integers,
register addresses, bit-layout tables, sample rates). Per PROJECT.md
licensing posture: nocash's and community-rendered psx-spx facts are
usable verbatim as numerical facts; their prose is paraphrased; GPL
emulator source code is off-limits as a primary source (their output
audio is witness material for Phase 7 diff harness and the Phase 4
empirical witness pass).

## Primary Sources

### BIB-001: nocash PSX SPU documentation
- **URL:** https://problemkaputt.de/psx-spx.htm (original author's hosting)
  + https://psx-spx.consoledev.net/soundprocessingunitspu/ (community render)
- **Author:** Martin "nocash" Korth + psx-spx community contributors
- **Used for:** SPU reverb formula (stage order, pseudocode, vIIR=-0x8000
  anomaly description, mBASE side-effect, BufferAddress wrap formula,
  22.05 kHz reverb rate, L/R time-multiplex, 39-tap FIR structure).
- **Caveat:** Treated as a factual reference (uncopyrightable facts).
  Never transcribes full prose passages or table captions.

### BIB-002: jsgroth.dev PS1 SPU series (Part 3 — Reverb)
- **URL:** https://jsgroth.dev/blog/posts/ps1-spu-part-3/
- **Author:** jsgroth
- **Used for:** Behavioral witness (22.05 kHz clock alternation,
  independent L/R FIR deques, structural note that the same 39-tap
  FIR is reused for both decimate and interpolate, comb-sum
  intermediate precision flagged as unresolved).
- **Caveat:** Does NOT reproduce the 39 coefficient values — points
  readers to psx-spx. Cited as structural corroboration only.

## Coefficient Sources (Phase 4)

### BIB-005: PSX-SPX Reverb Buffer Resampling coefficient table (published form)
- **URL:** https://psx-spx.consoledev.net/soundprocessingunitspu/#reverb-buffer-resampling
- **Used for:** The 39-tap half-band FIR coefficient integer values
  for 44.1 <-> 22.05 kHz reverb resampling.
- **Provenance note:** psx-spx transcribes these values from the
  forums.bannister.org SCPH-5501 hardware readout (BIB-006). Every
  published source that lists the 39 integer values traces back to
  this single hardware reading. See Phase 4 research document
  Coefficient provenance audit for the one-source-with-mirrors
  finding honored in ADR-Phase-4-I.

### BIB-006: forums.bannister.org PS1 SPU FIR Coefficients thread
- **URL:** https://forums.bannister.org/ubbthreads.php?ubb=showflat&Number=71222
- **Used for:** Primary hardware readout of the 39-tap FIR coefficient
  values from a SCPH-5501 console (the reading that BIB-005
  transcribes).
- **Provenance note:** Single hardware reading. Single-console
  (SCPH-5501) sourcing. Cross-console confirmation from independent
  revisions is deferred to Milestone 5 hardware validation.

### BIB-007: jsgroth.dev PS1 SPU Part 3 — structural corroboration for the FIR
- **URL:** https://jsgroth.dev/blog/posts/ps1-spu-part-3/
- **Used for:** Cross-reference that the 39-tap filter is implemented
  in the SPU silicon, that independent L/R deques are the correct
  state model (D-08), and that the same coefficient table is reused
  at both I/O boundaries.
- **Note:** Does not publish the 39 coefficient values. Cited as
  structural / implementation-pattern source, not as a coefficient
  source.

## Witness Sources (output-only; source code NOT read as a primary input)

### BIB-008: lv2-psx-reverb
- **URL:** https://github.com/ipatix/lv2-psx-reverb
- **License:** GPLv3.
- **Used for:** OUT-OF-AXIS witness on the frequency-response axis
  (confirmed by the project's own README stating it does not
  downsample to 22050 Hz and acknowledging the resulting "additional
  brightness of the higher frequencies"). Valid IN-AXIS witness for
  reverb-network structural behavior (register semantics, comb/APF
  topology).

### BIB-009: Mednafen
- **URL:** https://mednafen.github.io/
- **License:** GPLv2.
- **Used for:** IN-AXIS or OUT-OF-AXIS classification on
  frequency-response pending the Phase 4 execution-pass empirical
  protocol (see 04-RESEARCH Empirical Mednafen/DuckStation
  investigation protocol). Source code is NOT read as a primary
  research input.

### BIB-010: DuckStation
- **URL:** https://github.com/stenzek/duckstation
- **License:** CC-BY-NC-ND (as of September 2024).
- **Used for:** Same as BIB-009. Source code is NOT read as a
  primary research input; the CC-BY-NC-ND no-derivatives clause
  makes any source reading a licensing hazard.
