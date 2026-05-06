# Phase 17: Morph Knob GUI - Discussion Log

**Date:** 2026-05-06
**Areas discussed:** 3 (Knob Placement, Waypoint Indicator Style, Knob Feel / Thread Safety)

## Area 1: Knob Placement

**Options presented:**
1. New zone above register panel (recommended)
2. Replace register panel header area
3. Floating overlay / sidebar
4. Enlarge the window

**User's response:** None of the above. Described a macro/advanced toggle pattern: register panel stays accessible via "Advanced" button. A "Macro" button creates an overlay that covers the advanced section with one big encoder. Referenced existing archived code on `archive/v1.5-v1.6-macro-approach` branch with this exact pattern already built.

**Follow-up:** Reviewed archived MacroPanel.cpp, MacroPanel.h, and PluginEditor.cpp from the archived branch. Confirmed the toggle pattern (advancedToggle button, setVisible swap, same bounds for both panels, macro as default view).

**Decision:** Reuse archived macro/advanced toggle pattern. Strip MacroPanel down to a single morph encoder.

## Area 2: Waypoint Indicator Style

**Options presented:**
1. Just dots, no labels (recommended)
2. Dots with abbreviated preset names
3. Dynamic label showing nearest preset

**User's response:** PS1 palette tick marks. One label zone with dynamic labeling. On a tick/detent: shows preset name. Between ticks: shows numerical value as you turn. Below the encoder.

**Decision:** Tick marks in PS1 palette. Dynamic label zone below knob -- preset name on detents, 0.0-100.0 numerical value between.

## Area 3: Knob Feel / Thread Safety

**Options presented:**
1. Single atomic float (recommended)
2. Command queue (like PresetCommandQueue)

**User's question:** "What would a queue pattern afford us?"

**Claude's analysis:** Queue gives event ordering guarantees, but irrelevant for a continuous position value -- overwritten positions between audio callbacks are inaudible. Queues earn their keep for discrete commands with side effects (preset loads), not latest-value floats.

**User's response:** Single atomic float is good.

**Additional decision on knob feel:** Detents at waypoints for now. Magnetic physics deferred to future enhancement.

## Deferred Ideas

- Magnetic physics / spring-back at waypoints
- Preset name labels permanently visible around arc
