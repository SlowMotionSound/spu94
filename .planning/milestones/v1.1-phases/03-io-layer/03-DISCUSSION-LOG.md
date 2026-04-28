# Phase 3: I/O Layer - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-27
**Phase:** 03-io-layer
**Areas discussed:** CLI subcommand design, JUCE toggle placement, VAG module scope, Python API surface

---

## CLI Subcommand Design

### Q1: How should new ADPCM operations sit alongside existing reverb command?

| Option | Description | Selected |
|--------|-------------|----------|
| Git-style subcommands | spu94 reverb, spu94 adpcm-encode, etc. Current behavior stays as default when no subcommand given. | ✓ |
| Flat with prefix flags | New operations as flags: --adpcm-encode, --adpcm-decode. Simpler but gets crowded. | |
| Separate binary | spu94 for reverb, spu94-adpcm for codec ops. Clean separation but two binaries. | |

**User's choice:** Git-style subcommands
**Notes:** Backward compat — no subcommand defaults to reverb.

### Q2: What should roundtrip subcommand do?

| Option | Description | Selected |
|--------|-------------|----------|
| Encode then decode | In-memory encode→decode, writes WAV. Shows ADPCM coloration without reverb. No intermediate .vag. | ✓ |
| Encode then decode with sidecar | Same but also writes intermediate .vag file. | |
| Alias for piping | Two-step with temp file. Simpler but slower. | |

**User's choice:** Encode then decode (in-memory)

### Q3: Help output format?

| Option | Description | Selected |
|--------|-------------|----------|
| Brief descriptions | Each subcommand with one-liner. Per-subcommand --help for details. | ✓ |
| Minimal list | Just subcommand names, no descriptions. | |
| You decide | Claude picks. | |

**User's choice:** Brief descriptions

### Q4: Stereo WAV input handling?

| Option | Description | Selected |
|--------|-------------|----------|
| Process each channel independently | L and R as separate mono ADPCM streams. Standard PS1 convention. | ✓ |
| Error on stereo | Reject, require mono split first. | |
| Mix to mono | Downmix before encoding. Lossy. | |

**User's choice:** Dual-mono processing

---

## JUCE Toggle Placement

### Q1: Where should ADPCM toggle live?

| Option | Description | Selected |
|--------|-------------|----------|
| Toolbar row, near preset | Between preset selector and Input knob. Always visible. | ✓ |
| Own section above registers | "Signal Path" strip between toolbar and register panel. | |
| Inside RegisterPanel | First row in register panel among the 18 sliders. | |

**User's choice:** Toolbar row

### Q2: Toggle appearance and behavior?

| Option | Description | Selected |
|--------|-------------|----------|
| Lit toggle button | ToggleButton labeled "ADPCM", amber/orange glow when active. Click-free switching. | ✓ |
| LED + label | Round LED indicator next to clickable text. Hardware-console feel. | |
| You decide | Claude picks to match existing GUI style. | |

**User's choice:** Lit toggle button with amber glow

---

## VAG Module Scope

### Q1: Reusable library module or CLI-only?

| Option | Description | Selected |
|--------|-------------|----------|
| Library module in libspu94 | Public API in spu94.h, src/spu94/vag.c. Usable from CLI, Python, JUCE. | ✓ |
| CLI-only glue code | In src/cli/vag_io.c. Only CLI can read/write VAG. | |
| Separate static lib | libspu94_vag.a as own build target. | |

**User's choice:** Library module in libspu94

### Q2: Memory allocation pattern?

| Option | Description | Selected |
|--------|-------------|----------|
| Caller-allocated buffers | Same as codec. Query header for size, caller allocates, fill buffer. Zero heap. | ✓ |
| Internal malloc | VAG functions allocate and return. Simpler API but breaks zero-heap pattern. | |

**User's choice:** Caller-allocated buffers
**Notes:** User asked for clarification on what VAG format is — explained as Sony's PlayStation audio container format (48-byte big-endian header + raw ADPCM blocks). Decision confirmed after explanation.

---

## Python API Surface

### Q1: Raw C functions or convenience wrappers?

| Option | Description | Selected |
|--------|-------------|----------|
| Raw C functions only | 4 required functions: block-level encode/decode + toggle get/set. Matches existing style. | ✓ |
| Raw + file convenience | Also add encode_file/decode_file wrappers. | |
| You decide | Match existing binding pattern. | |

**User's choice:** Raw C functions only

### Q2: Expose VAG functions in Python?

| Option | Description | Selected |
|--------|-------------|----------|
| Expose VAG too | Wrap vag_read_header, vag_read, vag_write. Enables scripted batch conversions. | ✓ |
| Skip VAG in Python | Only the 4 required functions. VAG stays CLI-only. | |

**User's choice:** Expose VAG functions too

---

## Claude's Discretion

None — user made all decisions directly.

## Deferred Ideas

None — discussion stayed within phase scope.
