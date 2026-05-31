# Phase 62: Voice-Count Selector - Context

**Gathered:** 2026-05-31
**Status:** Ready for planning

<domain>
## Phase Boundary

Add a user-facing **Voice Count** control (1–24) to the sampler GUI that drives the
already-built engine count live. The engine work is done: Phase 60 added
`setActiveVoiceCount` (atomic, clamped [1,24], default 24; lowering it lets held
notes ring out and shrinks future allocation) and Phase 61 made every per-voice
control (Level, Pan, INV, NON, PMON, shared ADSR) fan out across
`[0, activeVoiceCount)`. So this phase is **purely the GUI control + wiring + a
synced display** — no new audio behavior.

**In scope (VCOUNT-01, VCOUNT-03):** a 1–24 dropdown in the sampler's voice panel,
wired to `setActiveVoiceCount`, on the **standalone app**, with the selected value
as the live readout.

**Out of scope:** the **DAW-plugin surface** (deferred until a plugin-beta milestone —
user decision 2026-05-31); voice-count persistence to presets/system state (VCOUNT-04 →
Phase 63); any change to allocation/fan-out/mono-poly behavior (already shipped in
Phases 60/61); per-voice independent control values (deferred in Phase 61).

This is a **testing/utility control**, not a performance macro (user's explicit
framing) — keep it simple.

</domain>

<decisions>
## Implementation Decisions

### Control type
- **D-01:** A **dropdown selector** (`juce::ComboBox`) listing 1–24, NOT a knob,
  fader, or stepper. Rationale (user): "this isn't a performance macro — it's
  simply select how many voices you need," and it's primarily a testing control,
  so a plain discrete selector is the right fit. Clone the existing `recordModeBox`
  ComboBox pattern (add items, `setSelectedId`, `onChange`).

### Placement
- **D-02:** Place it in the **voice panel, next to the existing per-voice controls**
  (pitch / level / pan) — it governs those controls, so it belongs with them.

### Surface
- **D-03:** _(revised 2026-05-31)_ **Standalone app only** for Phase 62. The DAW-plugin
  surface is deferred until a plugin-beta milestone (user: "focus on the standalone
  until we need a plugin beta version — it isn't pertinent yet"). This supersedes the
  original "both standalone AND plugin" decision and removes the plugin-surface-parity
  research question. The per-voice control panel is already standalone-gated
  (`// Voice engine panel (standalone-only, Phase 31)`, `PluginEditor.cpp` ~line 88),
  so the Voice Count dropdown simply joins that existing standalone panel — no plugin
  wiring this phase.

### Label
- **D-04:** Label the control **"Voice Count"** (user's pick over "Voices" /
  "Polyphony"). Plain — no Eurorack/PS1 framing on the label.

### Readout & sync
- **D-05:** The dropdown's **selected value is the display** — no separate readout
  widget. In Phase 62 the dropdown is the sole driver of the count, so the shown
  value always equals the engine count by construction (satisfies the
  "displayed = actual" criterion). Default selection = **24** (matches the engine
  default; no audible change until lowered).

### Wiring & behavior
- **D-06:** `onChange` → call `setActiveVoiceCount(selectedValue)`. That is the
  entire behavioral contract — Phase 60 handles ring-out on decrease and shrinking
  allocation; Phase 61's fan-out makes the per-voice controls follow the new count.
  No retrigger, no culling, no extra audio logic in this phase.

### Claude's Discretion
- Exact widget layout/sizing, item-list construction (1..24), and tooltip text —
  implementer's call, following the `recordModeBox` precedent.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### The engine hook to drive (already built — just call it)
- `src/plugin/PluginProcessor.h:281` — `void setActiveVoiceCount(int n);` (public setter)
- `src/plugin/PluginProcessor.cpp:2650` — `setActiveVoiceCount` definition (clamps [1,24], release-store)
- `src/plugin/PluginProcessor.h:442` — `std::atomic<int> activeVoiceCount{24}` (the count the GUI sets)

### The dropdown pattern to clone
- `src/plugin/PluginEditor.h:90` — `juce::ComboBox recordModeBox;` (declaration precedent)
- `src/plugin/PluginEditor.cpp:59-63` — `recordModeBox` setup (`addAndMakeVisible`, `addItem`, `setSelectedId`, `setTooltip`)
- `src/plugin/PluginEditor.cpp:301` — `recordModeBox.onChange` lambda (the wiring precedent)

### Placement neighbors (the per-voice controls this sits beside)
- `src/plugin/PluginEditor.h:85,136,138` — `voicePitchKnob`, `voicePanKnob`, `voiceLevelKnob`
- `src/plugin/PluginEditor.cpp` ~line 88 — `// Voice engine panel (standalone-only, Phase 31)` (confirms the voice panel is already standalone-gated; the dropdown joins it there)

### Prior-phase decisions (locked inputs — do not re-open)
- `.planning/phases/60-engine-voice-count-allocation/60-CONTEXT.md` — count state, allocation, ring-out-on-decrease
- `.planning/phases/61-coherent-controls/61-CONTEXT.md` — control fan-out across `[0, activeVoiceCount)`
- `.planning/ROADMAP.md` (Phase 62 section) — goal + success criteria; VCOUNT-01, VCOUNT-03

No external specs/ADRs beyond the above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `recordModeBox` (`PluginEditor`): a working `juce::ComboBox` with item list, default
  selection, tooltip, and an `onChange` handler — the direct template for the Voice
  Count dropdown.
- `setActiveVoiceCount` / `activeVoiceCount`: the message-thread→audio-thread-safe
  count hook is already in place; the GUI only needs to call the setter.

### Established Patterns
- The per-voice controls (pitch/pan/level) already exist as a group in the voice
  panel — the new dropdown joins that group.
- Phase 61 fan-out means no per-voice GUI wiring is needed beyond setting the count.

### Integration Points
- `onChange` → `processor.setActiveVoiceCount(n)`.
- Standalone voice panel only (see revised D-03) — no plugin-surface work this phase.

</code_context>

<specifics>
## Specific Ideas

- User framing: "select how many voices you need" — a discrete, deliberate testing
  control, not a swept performance gesture. Keep it minimal; don't over-engineer
  coherence or polish in this phase.
- The standalone app is the testing bed for this control (plugin surface deferred — see revised D-03).

</specifics>

<deferred>
## Deferred Ideas

- **Voice Count on the DAW-plugin surface** (was D-03) — deferred until a plugin-beta
  milestone (user decision 2026-05-31: "focus on the standalone until we need a plugin
  beta version — it isn't pertinent yet"). When that milestone arrives, surface this
  same control on the plugin editor alongside the per-voice controls.
- Any future polish of this control is "a given," not tracked here (user note).

</deferred>

---

*Phase: 62-voice-count-selector*
*Context gathered: 2026-05-31*
