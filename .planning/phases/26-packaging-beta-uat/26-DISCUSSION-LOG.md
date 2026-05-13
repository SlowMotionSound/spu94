# Phase 26: Packaging & Beta UAT - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-13
**Phase:** 26-packaging-beta-uat
**Areas discussed:** DAW test list, macOS package format, UAT process

---

## DAW Test List

### Which DAWs to test

| Option | Description | Selected |
|--------|-------------|----------|
| Reaper + Ardour + Logic | Covers personal setup + open-source + Apple native | |
| Big 4 + Ardour | Reaper + Ableton + Logic + FL Studio + Ardour | ✓ |
| Reaper only | Minimum viable — one host across 3 OSes | |

**User's choice:** Big 4 + Ardour
**Notes:** None

### DAW × OS matrix confirmation

| Option | Description | Selected |
|--------|-------------|----------|
| 9 combinations (as presented) | Reaper 3 + Ardour 1 + Logic 1 + Ableton 2 + FL Studio 2 | |
| Add Bitwig (Linux) | Strong CLAP support, popular on Linux — 10 combinations | ✓ |
| Trim Ableton or FL | Reduce to 7 combinations | |

**User's choice:** Add Bitwig (Linux)
**Notes:** Bitwig selected for native CLAP validation on Linux.

---

## macOS Package Format

| Option | Description | Selected |
|--------|-------------|----------|
| .pkg installer | Wizard, auto-places files in scan paths. Feels official. | |
| .dmg drag-install | Simpler to build, testers must know install paths. | |
| Both | .pkg for convenience + .dmg as fallback | ✓ |

**User's choice:** Both
**Notes:** .pkg is primary; .dmg is fallback for testers who don't trust installers.

---

## UAT Process

### Who tests

| Option | Description | Selected |
|--------|-------------|----------|
| You test personally | Full control, limited to your OSes | |
| Ship to testers first | Broader coverage, less structured | |
| You first, then testers | Validate on your setup, then ship | |

**User's choice:** (Freeform) "I can test on Linux and Mac, then ship to betas (Mac. Logic, protools -- will use wrapper for PT)"
**Notes:** Pro Tools coverage via VST3 wrapper (e.g., Blue Cat PatchWork). AAX not a build target.

### UAT checklist scope

| Option | Description | Selected |
|--------|-------------|----------|
| Load + audio + state | Plugin loads, processes audio, state round-trips | ✓ |
| Full feature sweep | All params + morph + presets + multi-instance | |
| Just loads and makes sound | Bare minimum | |

**User's choice:** Load + audio + state
**Notes:** Focused on the critical path, not exhaustive feature testing.

---

## Claude's Discretion

- Inno Setup config, Linux install.sh, .dmg layout, .pkg components
- Beta README structure and wording
- CI/CD release automation
- UAT checklist exact wording and pass/fail criteria

## Deferred Ideas

None — discussion stayed within phase scope.
