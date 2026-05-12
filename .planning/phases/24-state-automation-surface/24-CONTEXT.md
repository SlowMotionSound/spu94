# Phase 24: State & Automation Surface - Context

**Gathered:** 2026-05-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Wire `getStateInformation` / `setStateInformation` through the existing `.spu94` serializer with a binary version container. Expose exactly 9 host-automatable parameters for DAW automation, routed through the existing atomic-scalar + SPSC RegisterBridge pattern. Phase 23's UAT-only slider widen (PluginEditor.cpp:92-98 REVERT comment) is cleaned up — the 0..16 range is permanent and now backed by a native host parameter.

</domain>

<decisions>
## Implementation Decisions

### Automation display units
- **D-01:** Mix-level knobs (Dry Level, ADPCM Level, Reverb Level) display as **percent 0–100** in DAW automation lanes.
- **D-02:** Send knobs (Dry Send, ADPCM Send) display as **percent 0–100** in DAW automation lanes.
- **D-03:** Input Gain displays as **decibels, -∞ to +24 dB**, matching the 0..16 internal range established in Phase 23 (D-02).
- **D-04:** Morph Speed and Morph Grit display as **percent 0–100**.
- **D-05:** Morph Position displays as **percent 0–100**. (User explored a bipolar offset concept — anchor at current dial, automation swings ±100 around it — but deferred the design. Planner uses percent 0–100 as the default; bipolar offset is a deferred idea.)

### State container version policy
- **D-06:** When a v1 build encounters a state chunk with a version byte > 1, it refuses the chunk and leaves the engine at defaults. Standard fail-safe forward-compat.

### Phase 23 carryover (locked, not re-decided)
- Input Gain slider range 0..16 with unity-at-midpoint skew: **permanent** (signed off in Phase 23 UAT). Strip the REVERT comment at PluginEditor.cpp:92-98.
- D-03 engine register pinned at 0x7FFF on both paths: **permanent**.
- Pre-clamp float multiply placement: **permanent**.

### Claude's Discretion
- Parameter ID naming convention (internal strings for DAW save files) — pick a consistent scheme, document it, freeze it per PLUG-30.
- State container magic bytes, version byte encoding, body-length endianness — follow JUCE/industry convention.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements
- `.planning/milestones/v1.7-REQUIREMENTS.md` §"DAW Project State Persistence" (PLUG-22..27) and §"Host Automation Parameter Surface" (PLUG-28..31)
- `.planning/milestones/v1.7-ROADMAP.md` Phase 24 entry — goal, depends-on, provisional files

### Architecture
- `.planning/research/ARCHITECTURE-v1.7.md` §5 (threading/concurrency model, RegisterBridge, atomic-scalar pattern, pendingPresetBuf deferred-apply) and §6 (getStateInformation / setStateInformation pseudocode)

### Prior phase context
- `.planning/phases/23-float-int16-boundary/23-CONTEXT.md` — Input Gain decisions D-01/D-02/D-03, BoundaryConverter integration points, anti-patterns (float→int16 cast clamp, audible-range-needs-GUI)
- `.planning/phases/22-src-latency-reporting/22-CONTEXT.md` — SRC chain integration points
- `.planning/phases/21-build-skeleton-ci-matrix/21-CONTEXT.md` — plugin skeleton, build matrix

### Pitfalls
- `.planning/research/PITFALLS-v1.7.md` — RT-safety + saturation hazards

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_preset_save` / `spu94_preset_load` (C core, v1.4) — the `.spu94` serializer that the state container wraps.
- `pendingPresetBuf` + `pendingPresetSize` + `presetReady` atomic (PluginProcessor) — existing deferred-apply mechanism for bulk state loads. `setStateInformation` feeds into this same path.
- `RegisterBridge` SPSC queue (PluginProcessor) — existing lock-free GUI→audio register handoff. The 9 automation params route through this same bridge.
- `std::atomic<float>` members for mixer/morph/toggle values (PluginProcessor.h) — the existing atomic-scalar bridge that automation params wrap.
- `inputLevelKnob` (PluginEditor.cpp:89-98) — existing Input Gain rotary with 0..16 range and unity-midpoint skew. Phase 24 backs this with a native host parameter.

### Established Patterns
- **Atomic→engine sync per block** — processBlock already pushes UI atomics into engine registers each block (PluginProcessor.cpp:262-265). Automation params follow the same pattern.
- **NOT APVTS** — per PLUG-29 and ARCHITECTURE-v1.7.md §5.1, the plugin uses raw atomics + SPSC queue, not AudioProcessorValueTreeState.

### Integration Points
- `getStateInformation` / `setStateInformation` stubs at PluginProcessor.cpp:587-595 — currently empty; Phase 24 fills them.
- `processBlock` audio-thread loop — where automation param values are read each block and pushed to the engine.
- PluginEditor.cpp:92-98 — REVERT comment to strip; slider range stays.

</code_context>

<specifics>
## Specific Ideas

- Input Gain as dB in the automation lane came from Anthony directly — recording engineers think in dB for gain staging. The -∞ to +24 dB range matches the "outside the door, knob can drive into clip" creative intent from Phase 23.
- Mix levels and sends as percent came from Anthony directly — simpler than dB for balance controls.

</specifics>

<deferred>
## Deferred Ideas

- **Bipolar morph offset automation** — user explored a concept where the morph dial is an anchor and automation swings ±100 around it (negative = below current position, positive = above). Decouples the manual setting from automation. Interesting creative direction — would make the morph dial behave more like a performance controller. Deferred for now; morph ships as standard percent 0–100.
- **Visual clip indicator for int16 boundary** — carried from Phase 23 deferred ideas.
- **Hide preset Save/Load buttons in plugin formats** — carried from STATE.md deferred items.
- **Plugin GUI loses parameter state when window closed/reopened (Ardour LV2)** — APVTS attachment audit; may surface during Phase 24 if related to parameter wiring, otherwise separate follow-up.

</deferred>

---

*Phase: 24-state-automation-surface*
*Context gathered: 2026-05-12*
