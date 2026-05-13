# Phase 25: Buses & Validator Gates - Context

**Gathered:** 2026-05-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Declare mono→mono, mono→stereo, and stereo→stereo bus layouts in `isBusesLayoutSupported`; handle mono I/O in `processBlock` (duplicate mono input into both SPU channels, sum stereo output back to mono for mono→mono); promote the four plugin validators (pluginval, auval, lv2lint, VST3 SDK validator) from advisory/absent to hard CI gates that fail the build on any warning or error.

</domain>

<decisions>
## Implementation Decisions

### Skip assessment
Phase 25 was assessed for gray areas and found to be fully specified by v1.7 requirements (PLUG-32..42). No audible, creative, or user-facing decisions remained undecided. User confirmed skip-to-planning.

### Claude's Discretion
- AU manufacturer code and subtype code selection (4-character codes for auval)
- Mono→mono output gain factor ((L+R)/2 standard summing vs alternatives)
- Validator CI step ordering and parallelization strategy
- Whether pluginval strictness-7 runs per-format or on a representative subset per OS
- lv2lint and sord_validate installation method (package manager vs build from source)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements
- `.planning/milestones/v1.7-REQUIREMENTS.md` §"Channel Bus Configuration" (PLUG-32..36) and §"Validation & Quality Gates (CI)" (PLUG-37..42)
- `.planning/milestones/v1.7-ROADMAP.md` Phase 25 entry

### Architecture
- `.planning/research/ARCHITECTURE-v1.7.md` — threading model, processBlock structure

### Current CI baseline
- `.github/workflows/plugins.yml` — existing 3-OS build matrix with pluginval strictness-1 smoke and strictness-7 advisory; Phase 25 promotes to hard gate and adds auval/lv2lint/VST3 validator

### Prior phase context
- `.planning/phases/24-state-automation-surface/24-CONTEXT.md` — parameter registration, state persistence (direct dependency)
- `.planning/phases/23-float-int16-boundary/23-CONTEXT.md` — BoundaryConverter, Input Gain placement
- `.planning/phases/22-src-latency-reporting/22-CONTEXT.md` — SRC chain integration (Phase 22 not yet executed but architecture is relevant)

### Pitfalls
- `.planning/research/PITFALLS-v1.7.md` — RT-safety hazards

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `BusesProperties()` constructor (`PluginProcessor.cpp:11-13`) — currently stereo-only; needs expansion to declare mono+stereo default with `isBusesLayoutSupported` override.
- `processBlock` (`PluginProcessor.cpp:225+`) — currently assumes stereo; mono path needs channel-count branching.
- `plugins.yml` CI workflow — already has pluginval install/smoke for all 3 OSes; needs promotion + new validator steps.

### Established Patterns
- **ScopedNoDenormals** already in processBlock (Phase 22, PLUG-16).
- **Atomic→engine sync per block** — bus layout doesn't affect this; runs regardless of channel count.
- **CI SHA-pinning discipline** — all third-party actions pinned to full commit SHA per Phase 21 convention.

### Integration Points
- `isBusesLayoutSupported()` — currently not overridden (JUCE defaults to accepting only the declared default). Phase 25 adds the override.
- `processBlock` mono handling — when input has 1 channel, duplicate into both L/R before the SRC sandwich + boundary converter + SPU core. When output is mono, sum the stereo SPU output with 0.5× gain factor.
- `plugins.yml` CI steps — add auval (macOS), lv2lint + sord_validate (Linux), VST3 SDK validator (all OSes), and promote pluginval to strictness-7 as a build-blocking gate.

</code_context>

<specifics>
## Specific Ideas

No specific requirements — phase is fully driven by the locked PLUG-32..42 requirements. User confirmed no creative or audible decisions needed.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 25-buses-validator-gates*
*Context gathered: 2026-05-12*
