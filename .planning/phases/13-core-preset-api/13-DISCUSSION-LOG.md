# Phase 13: Core Preset API - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-01
**Phase:** 13-Core Preset API
**Areas discussed:** File format & readability, State coverage, Version tolerance

---

## File Format & Readability

### File Organization

| Option | Description | Selected |
|--------|-------------|----------|
| Sectioned | Group related keys under [headers] like an INI file | ✓ |
| Flat with comments | No sections, just key=value with # comment separators | |
| Completely flat | Just key=value lines, no grouping at all | |

**User's choice:** Sectioned (INI-style)
**Notes:** Previews shown for each option. User selected the sectioned format with [registers], [mixer], [dac] headers.

### Value Representation

| Option | Description | Selected |
|--------|-------------|----------|
| Hex everywhere | All 16-bit values as 0x0000-0xFFFF, toggles as 0/1 | ✓ |
| Hex for registers, decimal for mixer | Registers hex, mixer faders as decimal 0-32767 | |
| Accept both on load | Write as hex, parser accepts either hex or decimal | |

**User's choice:** Hex everywhere
**Notes:** Consistent with nocash spec convention.

### Inline Comments

| Option | Description | Selected |
|--------|-------------|----------|
| No comments | Clean key=value only | |
| Section-level comments only | Brief # comment at top of each section | ✓ |
| Per-key comments | Each key gets a trailing comment | |

**User's choice:** Section-level comments only
**Notes:** Orientation comments for each group, no per-key noise.

---

## State Coverage

### ADPCM Toggle

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, save it | ADPCM on/off is part of the sound | |
| No, leave it out | ADPCM is always-on infrastructure | ✓ |

**User's choice:** No — ADPCM is not saved
**Notes:** User corrected initial assumption. ADPCM is always-on with its own bus path. The patina_fader and patina_send mixer controls determine how much ADPCM coloration is heard. There is no user-facing ADPCM on/off toggle. This is a fixed part of the signal architecture, not a per-preset setting.

### Latency Compensation

**User's choice:** Yes — latency_comp is saved in presets
**Notes:** User clarified that latency comp is a completely separate and unrelated setting from ADPCM. It is a real user-facing toggle that should be saved. Claude incorrectly grouped it with ADPCM as "always-on infrastructure" and was corrected.

### Metadata Fields

| Option | Description | Selected |
|--------|-------------|----------|
| Name field only | Single 'name' key at the top | |
| No metadata | Filename IS the name | |
| Name + description | Both name and free-text description | ✓ |

**User's choice:** Name + description
**Notes:** Self-documenting for sharing and browsing preset collections.

---

## Version Tolerance

### Missing Keys

| Option | Description | Selected |
|--------|-------------|----------|
| Defaults for missing | Missing keys silently get engine defaults | ✓ |
| Warn then apply defaults | Load succeeds but returns warning code | |
| Reject version mismatch | Refuse to load if version doesn't match | |

**User's choice:** Defaults for missing
**Notes:** Old presets always load. Standard audio software behavior.

### Unknown Keys

| Option | Description | Selected |
|--------|-------------|----------|
| Ignore unknown | Skip unrecognized keys silently | ✓ |
| Warn on unknown | Load succeeds with warning | |
| Reject unknown | Any unrecognized key is a hard error | |

**User's choice:** Ignore unknown
**Notes:** Maximum tolerance. Newer presets load in older software (losing unrecognized fields). Tolerant of hand-edit typos.

---

## Claude's Discretion

- API signature details (return type, error codes, buffer sizing)
- Parser implementation approach
- Register write ordering during load
- Whether to expose factory presets through the new save format

## Deferred Ideas

None — discussion stayed within phase scope
