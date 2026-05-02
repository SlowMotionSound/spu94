# Phase 14: I/O Surfaces - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md -- this log preserves the alternatives considered.

**Date:** 2026-05-02
**Phase:** 14-i-o-surfaces
**Areas discussed:** CLI command design, Save dialog behavior, Preset selector after load

---

## CLI Command Design

### Q1: How should custom .spu94 presets integrate with the CLI?

| Option | Description | Selected |
|--------|-------------|----------|
| Flag on reverb | Add --load-preset to existing reverb command. One-shot workflow. | ✓ |
| Separate subcommands only | preset-dump and preset-load as their own subcommands. | |
| Both | --load-preset flag AND preset-load subcommand. | |

**User's choice:** Flag on reverb
**Notes:** Keeps the processing workflow unified under `reverb`. `preset-dump` stays standalone for exporting.

### Q2: preset-dump source options

| Option | Description | Selected |
|--------|-------------|----------|
| --preset only | Only accepts --preset <name> as source. | ✓ |
| --preset and --config | Accept either as source state. | |
| --preset, --config, and --load-preset | Full converter: any source re-exported. | |

**User's choice:** --preset only

### Q3: preset-dump metadata flags

| Option | Description | Selected |
|--------|-------------|----------|
| --name flag | Optional --name <text> fills name field. Description empty. | ✓ |
| Auto-fill from factory name | Automatically use factory preset name. | |
| No metadata flags | Name and description always empty. | |

**User's choice:** --name flag

### Q4: --load-preset flag override behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, flags override | Mixer/DAC flags override after preset file loads. | ✓ |
| No, file is final | --load-preset mutually exclusive with mixer/DAC flags. | |

**User's choice:** Yes, flags override

---

## Save Dialog Behavior

### Q1: How should the preset name be captured on Save?

| Option | Description | Selected |
|--------|-------------|----------|
| File dialog only | Filename becomes name field. No extra popup. | |
| Name prompt then file dialog | Text input for name/description, then file dialog. | ✓ |
| Inline name field in GUI | Persistent text field in toolbar. | |

**User's choice:** Name prompt then file dialog

### Q2: Name prompt pre-fill behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Pre-fill from factory | Pre-fill with factory preset name or loaded file's name. | ✓ |
| Always blank | Start empty every time. | |
| Pre-fill from last save | Remember last saved name within session. | |

**User's choice:** Pre-fill from factory

### Q3: Description field in prompt

| Option | Description | Selected |
|--------|-------------|----------|
| Name only | Just one text field. Description stays empty. | |
| Name + description | Two fields: name (required) and description (optional). | ✓ |

**User's choice:** Name + description

---

## Preset Selector After Load

### Q1: What should the preset dropdown show after loading a custom file?

| Option | Description | Selected |
|--------|-------------|----------|
| Show loaded name | Dynamic entry at top with loaded preset's name. | ✓ |
| Show "Custom" | Generic "Custom" entry. | |
| Deselect (no selection) | Clear dropdown selection entirely. | |

**User's choice:** Show loaded name

### Q2: Modified state indicator

| Option | Description | Selected |
|--------|-------------|----------|
| No indicator | Dropdown stays on whatever was loaded. No dirty tracking. | |
| Asterisk indicator | Append asterisk when state differs from loaded baseline. | ✓ |

**User's choice:** Asterisk indicator

### Q3: What resets the modified baseline?

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, resets baseline | Factory preset switch or file load both reset. | ✓ |
| Only file loads reset | Only .spu94 file loads reset the baseline. | |

**User's choice:** Yes, resets baseline

---

## Claude's Discretion

- JUCE AlertWindow styling and field layout for name/description prompt
- Modified-state comparison mechanism
- preset-dump help text and error messages
- CMake integration details
- Whether --list-presets is shared between preset-dump and reverb

## Deferred Ideas

None -- discussion stayed within phase scope
