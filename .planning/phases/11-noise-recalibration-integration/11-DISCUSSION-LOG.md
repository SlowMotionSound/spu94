# Phase 11: Noise Recalibration + Integration — Discussion Log

**Date:** 2026-05-01
**Areas discussed:** 3

## Area 1: A/B Toggle Design (CMP-01)

**Options presented:**
1. New mode enum (SPU94_DAC_MODE_V12/V13) with dedicated API
2. Simple set/get toggle matching existing pattern

**User selection:** Option 2 — "just use existing toggles. keep it flush with everything else for now."

**Notes:** No further discussion needed. Clear preference for consistency over elaboration.

## Area 2: Noise at 352.8kHz (DSP-05)

**Options presented:**
1. Inject noise at 352.8kHz before decimation (hardware-faithful, filter shapes noise) [RECOMMENDED]
2. Run LFSR 8x but inject at 44.1kHz post-decimation (simpler, loses spectral shaping)

**User questions:**
- "What do you mean by 'DAC floor' character?" — Explained: quantization noise at oversampled rate gets shaped by reconstruction filter, giving each converter its characteristic noise signature
- "Injecting noise at the oversample rate is common practice right?" — Confirmed: standard practice for oversampling DAC modeling

**User selection:** Option 1 — inject at 352.8kHz, let the interpolation filter shape it.

## Area 3: Gain Compensation Check

**Options presented:**
1. Code-level revisit of <<3 compensation
2. Human listen gate — Anthony evaluates by ear

**User selection:** Option 2 — "This can be a human listen gate to sign it all off"

**Notes:** The listen gate covers both gain compensation correctness and overall DAC output quality.

## Claude's Discretion Items

- Latency calculation (DSP-07): purely technical, user deferred to Claude
