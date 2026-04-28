# Phase 3: I/O Layer - Research

**Researched:** 2026-04-27
**Domain:** CLI subcommand dispatch, VAG file format, Python ctypes bindings, JUCE GUI toggle
**Confidence:** HIGH

## Summary

Phase 3 wires the completed ADPCM codec (Phase 1) and pipeline integration (Phase 2) into four user-facing surfaces: CLI subcommands for WAV-to-VAG conversion and ADPCM roundtrip, a `--adpcm` flag for the existing reverb mode, Python ctypes bindings for 4 ADPCM + 3 VAG functions, and a JUCE standalone GUI toggle. No new DSP is written -- this is pure I/O plumbing.

The codebase already has all the patterns needed. The CLI uses `getopt_long` in a flat `main.c` which will be restructured into subcommand dispatch. Python bindings follow the established ctypes pattern in `_binding.py`. The JUCE standalone uses atomic stores for real-time-safe parameter passing (same pattern as the existing `wetDry` and `inputLevel` atomics). The only genuinely new code is the VAG reader/writer module (`src/spu94/vag.c`), which is a straightforward 48-byte big-endian header parser plus block-level data I/O with caller-allocated buffers.

**Primary recommendation:** Decompose into 3 plans: (1) VAG module + CLI subcommand dispatch, (2) Python bindings, (3) JUCE toggle. Each plan builds on the previous and has clean test gates.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Git-style subcommands: `spu94 reverb`, `spu94 adpcm-encode`, `spu94 adpcm-decode`, `spu94 adpcm-roundtrip`. No subcommand defaults to `reverb` for backward compatibility with existing scripts.
- **D-02:** `spu94 adpcm-roundtrip in.wav out.wav` encodes to ADPCM in memory then decodes back -- no intermediate .vag file. Shows ADPCM coloration in isolation.
- **D-03:** `spu94 --help` shows all subcommands with brief one-line descriptions. Each subcommand has its own `--help` for subcommand-specific flags.
- **D-04:** Stereo WAV input is processed as dual-mono (L and R encoded as separate ADPCM streams), matching PS1 convention. VAG output stores two consecutive mono streams.
- **D-05:** ADPCM toggle lives in the toolbar row, between the preset selector and the Input knob. Always visible, one click to flip.
- **D-06:** Lit toggle button labeled "ADPCM" -- lights up (amber/orange glow) when active. State change takes effect on the next process block (click-free, already handled cleanly by the C API).
- **D-07:** VAG reader/writer is a library module inside libspu94 (`src/spu94/vag.c`, public API in `spu94.h`). Reusable from CLI, Python, JUCE, and future callers -- peer module alongside the codec.
- **D-08:** Caller-allocated buffers, same pattern as the codec. Caller queries header for size, allocates, then calls read/write. Zero heap in the VAG module itself.
- **D-09:** Python bindings expose the 4 required ADPCM C functions: `adpcm_decode_block()`, `adpcm_encode_block()`, `set_adpcm_enabled()`, `get_adpcm_enabled()`. Matches existing binding style (raw ctypes wrappers).
- **D-10:** Python bindings also expose VAG read/write functions (`vag_read_header()`, `vag_read()`, `vag_write()`) since VAG now lives in libspu94. Enables scripted batch conversions.

### Claude's Discretion
No specific areas called out -- open to standard approaches following existing patterns.

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| ADPCM-IO-01 | CLI gains `spu94 adpcm-encode` (WAV->VAG), `spu94 adpcm-decode` (VAG->WAV), and `spu94 adpcm-roundtrip` (WAV->ADPCM->WAV) subcommands | CLI subcommand dispatch pattern, WAV I/O reuse, VAG module API |
| ADPCM-IO-02 | CLI gains `--adpcm` flag for the existing reverb processing mode | Existing getopt_long opts table, `spu94_set_adpcm_enabled()` API already exists |
| ADPCM-IO-03 | VAG file reader parses 48-byte big-endian header with explicit byte-order conversion (no ntohl), handles terminator blocks | VAG format specification, byte-swap macros, flag byte semantics |
| ADPCM-IO-04 | VAG file writer produces valid VAG v2 files (mono, big-endian header), zero-pads final block, sets end flag | VAG header structure, ADPCM block flag byte conventions |
| ADPCM-IO-05 | Python ctypes bindings expose 4 ADPCM functions | Existing `_binding.py` pattern, ctypes prototype declarations |
| ADPCM-IO-06 | JUCE standalone gains "ADPCM" toggle in the GUI | Existing atomic parameter pattern (`wetDry`), `ToggleButton` JUCE API, toolbar layout |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| VAG read/write | C library (libspu94) | -- | Library module per D-07; reusable across all callers |
| CLI subcommand dispatch | CLI binary | -- | Application-level concern in `src/cli/main.c` |
| ADPCM encode/decode CLI | CLI binary | C library | CLI orchestrates, library does the work |
| `--adpcm` flag for reverb | CLI binary | C library | CLI sets the flag, library processes |
| Python ADPCM bindings | Python ctypes layer | C library | Thin wrappers over existing C functions |
| Python VAG bindings | Python ctypes layer | C library | Thin wrappers over new VAG C functions |
| JUCE ADPCM toggle | Frontend (JUCE GUI) | C library | Atomic store in GUI, atomic load in audio thread |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libspu94 | 0.1.0 | ADPCM encode/decode, VAG I/O | Already built -- this phase only adds VAG module |
| dr_wav | 0.14.6 (vendored) | WAV read/write in CLI | Already vendored and used by `wav_io.c` |
| JUCE | 8.0.12 | GUI toggle button | Already FetchContent'd in CMakeLists.txt |
| ctypes | stdlib | Python bindings | Already the binding strategy -- no new deps |

### Supporting
No new libraries needed. Everything builds on existing infrastructure.

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Handwritten VAG parser | libvag / ffmpeg | Overkill; VAG header is 48 bytes + 16-byte blocks. Handwritten is simpler and avoids dependencies. |
| `ntohl` for byte swapping | Manual shift-based conversion | D-03 explicitly forbids `ntohl` (not available on all targets). Manual shifts are portable. |

## Architecture Patterns

### System Architecture Diagram

```
                        CLI (src/cli/main.c)
                        |
           +------------+------------+------------+
           |            |            |            |
     argv[1] dispatch   |            |            |
           |            |            |            |
    "reverb"     "adpcm-encode"  "adpcm-decode"  "adpcm-roundtrip"
    (--adpcm)    WAV->VAG         VAG->WAV        WAV->ADPCM->WAV
           |            |            |            |
           v            v            v            v
      spu94_set_    spu94_vag_   spu94_vag_   spu94_adpcm_
      adpcm_enabled write()      read_header  encode/decode
      + spu94_process             + read()     (in memory, no VAG)
           |            |            |            |
           +------+-----+-----+-----+-----+------+
                  |                        |
            libspu94.so              Python ctypes
            (C library)              (_binding.py)
                  |                        |
        +--------+--------+        spu94_adpcm_*()
        |        |        |        spu94_vag_*()
     adpcm.c  vag.c   process.c    spu94_set/get_adpcm_*()
                  |
            JUCE standalone
            (PluginEditor.cpp)
                  |
          ToggleButton "ADPCM"
          -> atomic store
          -> processBlock reads
          -> spu94_set_adpcm_enabled
```

### Recommended Project Structure (new files only)

```
src/spu94/
  vag.c                # VAG reader/writer (new)
include/spu94/
  spu94_vag.h          # VAG public API (new)
src/cli/
  main.c               # Restructured: subcommand dispatch (modified)
  cmd_reverb.c          # Extracted reverb pipeline (new)
  cmd_adpcm.c           # adpcm-encode/decode/roundtrip (new)
python/spu94/
  _binding.py           # Add ADPCM + VAG prototypes (modified)
src/standalone/
  PluginEditor.cpp      # Add ADPCM toggle (modified)
  PluginEditor.h        # Add toggle member (modified)
  PluginProcessor.cpp   # Read ADPCM toggle atomic (modified)
  PluginProcessor.h     # Add ADPCM atomic (modified)
```

### Pattern 1: CLI Subcommand Dispatch

**What:** argv[1] checked before getopt_long runs. If it matches a known subcommand, dispatch to that handler. Otherwise, fall through to `reverb` mode for backward compatibility.

**When to use:** When a single binary needs multiple modes (like git).

**Example:**
```c
// Pattern for subcommand dispatch in main()
int main(int argc, char **argv) {
    if (argc >= 2 && argv[1][0] != '-') {
        if (strcmp(argv[1], "reverb") == 0)
            return cmd_reverb(argc - 1, argv + 1);
        if (strcmp(argv[1], "adpcm-encode") == 0)
            return cmd_adpcm_encode(argc - 1, argv + 1);
        if (strcmp(argv[1], "adpcm-decode") == 0)
            return cmd_adpcm_decode(argc - 1, argv + 1);
        if (strcmp(argv[1], "adpcm-roundtrip") == 0)
            return cmd_adpcm_roundtrip(argc - 1, argv + 1);
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
            return print_global_help();
        // Unknown subcommand: error
        SPU94_ERROR("unknown command '%s' (try --help)", argv[1]);
        return 2;
    }
    // No subcommand or starts with '-': legacy reverb mode
    return cmd_reverb(argc, argv);
}
```

### Pattern 2: VAG Module (Caller-Allocated, Zero-Heap)

**What:** VAG header parse returns metadata (sample_rate, data_size, num_blocks). Caller allocates buffers. Read/write operate on caller buffers.

**When to use:** All VAG I/O.

**Example:**
```c
// [ASSUMED] -- API shape follows D-07/D-08 decisions
typedef struct {
    uint32_t version;
    uint32_t data_size;     /* bytes of ADPCM data after header */
    uint32_t sample_rate;
    char     name[16];
} spu94_vag_header;

/* Read and validate the 48-byte header. Returns 0 on success. */
int spu94_vag_read_header(const uint8_t header_buf[48],
                          spu94_vag_header *out);

/* Write a 48-byte VAG v2 header into header_buf. */
void spu94_vag_write_header(uint8_t header_buf[48],
                            const spu94_vag_header *hdr);

/* Caller computes block count from header: data_size / 16.
 * Then reads/writes ADPCM blocks using spu94_adpcm_decode/encode_block. */
```

### Pattern 3: JUCE Toggle (Atomic Handoff)

**What:** ToggleButton on GUI thread stores to `std::atomic<bool>`. Audio thread reads it in processBlock and calls `spu94_set_adpcm_enabled()`.

**When to use:** Any boolean parameter bridging GUI <-> audio thread.

**Example:**
```cpp
// PluginProcessor.h -- add member
std::atomic<bool> adpcmEnabled{false};

// PluginEditor.cpp -- toggle setup
adpcmToggle.onClick = [this] {
    processorRef.adpcmEnabled.store(
        adpcmToggle.getToggleState(),
        std::memory_order_relaxed);
};

// PluginProcessor.cpp -- in processBlock, before spu94_process
spu94_set_adpcm_enabled(spu,
    adpcmEnabled.load(std::memory_order_relaxed) ? 1 : 0);
```
[VERIFIED: existing codebase pattern -- `wetDry` and `inputLevel` atomics in PluginProcessor.h/cpp]

### Pattern 4: Python ctypes Bindings

**What:** Declare argtypes/restype for each new C function, following existing `_binding.py` conventions.

**Example:**
```python
# In _binding.py -- ADPCM functions
_lib.spu94_adpcm_decode_block.restype = ctypes.c_uint8
_lib.spu94_adpcm_decode_block.argtypes = [
    ctypes.POINTER(SomeAdpcmState),     # state
    ctypes.POINTER(ctypes.c_uint8),     # block[16]
    ctypes.POINTER(ctypes.c_int16),     # out[28]
]
```
[VERIFIED: existing pattern in `_binding.py` -- all C functions get argtypes/restype declarations]

### Anti-Patterns to Avoid

- **Using `ntohl`/`htonl` for VAG byte-order:** Forbidden by ADPCM-IO-03. Use explicit shifts: `(buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]`.
- **Heap allocation in VAG module:** Violates D-08. All buffers are caller-provided.
- **Blocking the audio thread on file I/O:** VAG read/write is CLI-only. The JUCE toggle is a boolean flag, no I/O.
- **Breaking backward compatibility:** Running `spu94 --preset hall in.wav out.wav` (no subcommand) must still work per D-01.
- **Shared encoder/decoder state struct for VAG Python bindings:** Each ctypes caller manages their own `spu94_adpcm_state` -- do not create Python-side global state.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV read/write | Custom WAV parser | dr_wav (already vendored) | WAV format has edge cases (padding, metadata chunks); dr_wav handles them all |
| Byte-order conversion | ntohl/htonl | Inline shift macros | Portable, no header dependency, explicit per D-03 |
| JUCE thread-safe parameter | Mutex / lock | `std::atomic<bool>` | Lock-free, real-time-safe, already the established pattern |
| ADPCM encode/decode | Anything new | Existing `spu94_adpcm_encode_block` / `spu94_adpcm_decode_block` | Already implemented, tested, and verified in Phase 1/2 |

**Key insight:** This phase creates no new algorithms. Every piece of DSP already exists. The work is entirely plumbing and UI wiring.

## VAG File Format Reference

### Header Structure (48 bytes, big-endian)

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x00 | 4 | char[4] | magic | "VAGp" (0x56414770) |
| 0x04 | 4 | uint32_be | version | Writer produces v2 (0x00000002). Reader accepts any version per D-03. |
| 0x08 | 4 | uint32_be | reserved | Set to 0 on write |
| 0x0C | 4 | uint32_be | data_size | Total ADPCM data bytes after header (num_blocks * 16) |
| 0x10 | 4 | uint32_be | sample_rate | Hz (e.g. 44100) |
| 0x14 | 12 | uint8[12] | reserved2 | Zeroed |
| 0x20 | 16 | char[16] | name | Null-terminated ASCII, zero-padded |

[CITED: psx.arthus.net/code/VAG/vagsfx.c -- Sony SDK VAGhdr struct]
[CITED: vgmstream/src/meta/vag.c -- header parsing at offsets 0x00-0x30]

### ADPCM Block Flag Byte (block[1])

| Value | Meaning |
|-------|---------|
| 0x00 | Normal block -- continue decoding |
| 0x01 | End flag (last block of stream, no loop) |
| 0x02 | Loop region marker (ignored by SPU-94) |
| 0x03 | Loop end + jump to loop start (ignored) |
| 0x04 | Start of loop region (ignored) |
| 0x06 | Loop start marker (ignored) |
| 0x07 | Terminator -- stop decoding immediately |

[ASSUMED] -- flag values compiled from multiple sources; exact semantics vary by implementation. For SPU-94, the reader should stop on flag 0x07 (terminator) and flag 0x01 (end). Other flags are treated as normal blocks.

### Stereo VAG Convention (D-04)

Stereo is handled as dual-mono: two consecutive mono VAG streams in a single file. The first stream is Left, the second is Right. Each stream has its own VAG header. Total file = 48 bytes (L header) + L data + 48 bytes (R header) + R data.

Alternative: Some implementations use a single header with `version=0x00000020` indicating interleaved stereo. SPU-94 uses the simpler dual-sequential approach per D-04.

[ASSUMED] -- stereo VAG layout follows the common dual-mono convention; not standardized by Sony.

### Byte-Order Conversion Pattern

```c
/* Read a big-endian uint32 from a byte buffer. No ntohl. */
static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
           ((uint32_t)p[3]);
}

/* Write a uint32 as big-endian into a byte buffer. */
static inline void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v);
}
```
[VERIFIED: this is the standard portable byte-order conversion pattern used throughout embedded C codebases]

## Common Pitfalls

### Pitfall 1: Breaking Backward Compatibility in CLI
**What goes wrong:** Existing scripts that run `spu94 --preset hall in.wav out.wav` break because the new dispatcher expects a subcommand.
**Why it happens:** Overzealous refactoring of the argument parser.
**How to avoid:** The dispatch logic checks if argv[1] starts with `-` or matches a known subcommand. Unknown tokens that start with `-` fall through to `cmd_reverb`. Bare invocations without a subcommand default to reverb mode. Test the legacy invocation explicitly.
**Warning signs:** Existing CLI tests fail.

### Pitfall 2: VAG Reader Assuming v2 Only
**What goes wrong:** Reader rejects VAG files with version != 2.
**Why it happens:** Writer produces v2, developer assumes reader should match.
**How to avoid:** ADPCM-IO-03 says "accepts any version on read." Version field is informational -- the block structure is the same regardless. Only validate the "VAGp" magic.
**Warning signs:** Valid VAG files from real PS1 games fail to load.

### Pitfall 3: Stereo VAG Misalignment
**What goes wrong:** Reader assumes single-header stereo and reads garbage for the right channel.
**Why it happens:** Stereo VAG layout is not standardized.
**How to avoid:** Per D-04, SPU-94 writes dual-sequential (two headers, two mono streams). The reader should detect this by checking for a second "VAGp" magic after the first stream's data. For single-channel files, treat as mono.
**Warning signs:** Right channel output is noise or silence.

### Pitfall 4: JUCE Toggle Not Taking Effect
**What goes wrong:** Clicking the ADPCM toggle does nothing to the audio.
**Why it happens:** Missing the step where processBlock reads the atomic and calls `spu94_set_adpcm_enabled()`.
**How to avoid:** Follow the exact same pattern as the existing `wetDry` atomic: store in onClick, load in processBlock.
**Warning signs:** Audio sounds the same with toggle on vs off.

### Pitfall 5: Python ctypes Struct Alignment for spu94_adpcm_state
**What goes wrong:** ctypes struct has wrong padding, causing memory corruption on encode/decode calls.
**Why it happens:** ctypes default packing differs from C compiler.
**How to avoid:** `spu94_adpcm_state` is 4 bytes (two int16). Use `ctypes.Structure` with `_fields_ = [("old", c_int16), ("older", c_int16)]`. No padding issues because 4 bytes is naturally aligned. Verify with `ctypes.sizeof(SomeStruct) == 4`.
**Warning signs:** Segfaults or garbled audio from Python ADPCM calls.

### Pitfall 6: Forgetting End Flag in VAG Writer
**What goes wrong:** VAG decoder reads past the end of the file because no terminator block exists.
**Why it happens:** Writer packs all ADPCM blocks but forgets to set flag byte on the last block.
**How to avoid:** The last ADPCM block's flag byte (block[1]) must be set to 0x01 (end) or 0x07 (terminator). SPU-94 uses 0x01. ADPCM-IO-04 explicitly requires this.
**Warning signs:** Decoders read garbage after the audio data.

### Pitfall 7: JUCE Layout Collision with ADPCM Toggle
**What goes wrong:** ADPCM toggle overlaps with preset selector or Input knob.
**Why it happens:** Hardcoded pixel positions in `resized()` don't account for the new control.
**How to avoid:** The CONTEXT.md notes that the preset selector ends at ~575px and Input knob starts at 590px. In the current layout, Input starts at 590 and Wet/Dry at 680. The ADPCM toggle needs to be inserted before Input, which means shifting Input and Wet/Dry rightward, or placing the toggle in the gap. Current window width is fixed at 800px -- may need to widen slightly.
**Warning signs:** Controls overlap visually or click targets are wrong.

## Code Examples

### Example 1: VAG Header Read (Pure C, Zero-Heap)

```c
// [ASSUMED] -- follows D-07/D-08 pattern
int spu94_vag_read_header(const uint8_t buf[48], spu94_vag_header *out) {
    /* Validate magic: "VAGp" = 0x56 0x41 0x47 0x70 */
    if (buf[0] != 'V' || buf[1] != 'A' || buf[2] != 'G' || buf[3] != 'p')
        return -1;  /* not a VAG file */

    out->version     = read_be32(buf + 0x04);
    out->data_size   = read_be32(buf + 0x0C);
    out->sample_rate = read_be32(buf + 0x10);
    memcpy(out->name, buf + 0x20, 16);
    out->name[15] = '\0';  /* ensure null termination */
    return 0;
}
```

### Example 2: VAG Header Write (v2)

```c
// [ASSUMED] -- follows D-04/D-08 pattern
void spu94_vag_write_header(uint8_t buf[48],
                            uint32_t data_size,
                            uint32_t sample_rate,
                            const char *name) {
    memset(buf, 0, 48);
    buf[0] = 'V'; buf[1] = 'A'; buf[2] = 'G'; buf[3] = 'p';
    write_be32(buf + 0x04, 2);           /* version 2 */
    write_be32(buf + 0x0C, data_size);
    write_be32(buf + 0x10, sample_rate);
    if (name) {
        size_t len = strlen(name);
        if (len > 15) len = 15;
        memcpy(buf + 0x20, name, len);
    }
}
```

### Example 3: ADPCM Roundtrip (In-Memory, No VAG)

```c
// Pattern for adpcm-roundtrip subcommand (D-02)
// Load WAV -> process all samples through encode+decode -> write WAV
spu94_adpcm_state enc_state = {0, 0};
spu94_adpcm_state dec_state = {0, 0};

for (uint64_t i = 0; i < num_frames; i += SPU94_ADPCM_BLOCK_SAMPLES) {
    uint32_t block_len = (num_frames - i < SPU94_ADPCM_BLOCK_SAMPLES)
                       ? (uint32_t)(num_frames - i)
                       : SPU94_ADPCM_BLOCK_SAMPLES;

    int16_t padded[SPU94_ADPCM_BLOCK_SAMPLES];
    memset(padded, 0, sizeof padded);
    memcpy(padded, &samples[i], block_len * sizeof(int16_t));

    uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
    spu94_adpcm_encode_block(&enc_state, padded, 0, block);
    spu94_adpcm_decode_block(&dec_state, block, padded);

    memcpy(&output[i], padded, block_len * sizeof(int16_t));
}
```

### Example 4: JUCE ToggleButton with Amber Glow (D-06)

```cpp
// [ASSUMED] -- JUCE ToggleButton with custom look or colour override
adpcmToggle.setButtonText("ADPCM");
adpcmToggle.setClickingTogglesState(true);
adpcmToggle.setColour(juce::ToggleButton::tickColourId,
                      juce::Colour(0xFFD4A017));  // amber
addAndMakeVisible(adpcmToggle);

adpcmToggle.onClick = [this] {
    processorRef.adpcmEnabled.store(
        adpcmToggle.getToggleState(),
        std::memory_order_relaxed);
};
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Flat CLI with single mode | Git-style subcommands | This phase | Users get `spu94 adpcm-encode` etc. alongside existing `spu94` reverb |
| VAG as external tool concern | VAG module inside libspu94 | This phase (D-07) | VAG read/write is reusable from any caller, not just CLI |

**Deprecated/outdated:**
- `ntohl`/`htonl`: Explicitly forbidden by ADPCM-IO-03. Use shift-based conversion.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | VAG header is 48 bytes with the documented field layout | VAG File Format Reference | VAG files from real PS1 games would fail to parse. LOW risk -- this is well-documented across multiple sources. |
| A2 | Stereo VAG uses dual-sequential mono streams | VAG File Format Reference | Stereo encode/decode would produce wrong output. MEDIUM risk -- not standardized, but matches D-04 decision. |
| A3 | VAG flag byte 0x01 = end, 0x07 = terminator | VAG File Format Reference | Reader might stop too early or too late. LOW risk -- consistent across all reference implementations. |
| A4 | JUCE ToggleButton with custom amber colour works via setColour | Code Examples | Might need a custom LookAndFeel instead. LOW risk -- basic JUCE API. |
| A5 | Python spu94_adpcm_state is 4 bytes with no padding | Pitfall 5 | ctypes struct mismatch would cause memory corruption. LOW risk -- two int16 is naturally packed. |

## Open Questions (RESOLVED)

1. **JUCE toolbar layout space**
   - What we know: Current layout has preset selector ending at ~575px, Input at 590, Wet/Dry at 680. Window is 800x750 fixed.
   - What's unclear: Whether an ADPCM toggle fits between preset and Input without pushing controls off-screen, or whether the window needs to grow.
   - Recommendation: During implementation, measure the toggle width (likely ~80px for "ADPCM" text + indicator) and shift Input/Wet/Dry rightward. If they overflow 800px, widen the window slightly (e.g. to 880px). Update `setResizeLimits` accordingly.

2. **Python VAG API shape**
   - What we know: D-10 says expose `vag_read_header()`, `vag_read()`, `vag_write()`. The C-side API operates on byte buffers (caller-allocated).
   - What's unclear: Whether Python callers should get a higher-level wrapper (accepting file paths) or just raw buffer operations matching C.
   - Recommendation: Start with raw ctypes wrappers in `_binding.py` matching C API exactly. A higher-level convenience function (read VAG file from path, return numpy array) can go in `api.py` if desired, but is not required by ADPCM-IO-05.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest + Unity (C), pytest (Python) |
| Config file | `tests/CMakeLists.txt` (C), `pytest.ini` or pyproject.toml (Python) |
| Quick run command | `cd build && ctest -R adpcm --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| ADPCM-IO-01 | CLI subcommands produce correct output | integration (CLI) | `pytest tests/cli/test_cli_adpcm.py -x` | Wave 0 |
| ADPCM-IO-02 | `--adpcm` flag enables coloration in reverb mode | integration (CLI) | `pytest tests/cli/test_cli_adpcm.py::test_reverb_adpcm_flag -x` | Wave 0 |
| ADPCM-IO-03 | VAG reader parses big-endian header, handles terminators | unit (C) | `ctest -R vag_read --output-on-failure` | Wave 0 |
| ADPCM-IO-04 | VAG writer produces valid v2 files | unit (C) | `ctest -R vag_write --output-on-failure` | Wave 0 |
| ADPCM-IO-05 | Python ctypes ADPCM bindings work | unit (Python) | `pytest tests/python/binding/test_binding_adpcm.py -x` | Wave 0 |
| ADPCM-IO-06 | JUCE toggle enables/disables ADPCM | manual | Build standalone, click toggle, listen | manual-only (no headless JUCE test infra) |

### Sampling Rate
- **Per task commit:** `ctest -R "adpcm|vag" --output-on-failure` (C tests) + `pytest tests/cli/test_cli_adpcm.py -x` (CLI integration)
- **Per wave merge:** `ctest --output-on-failure` (full C suite) + `pytest tests/ -x` (full Python suite)
- **Phase gate:** Full suite green before verification

### Wave 0 Gaps
- [ ] `tests/unit/vag/test_vag_read.c` -- covers ADPCM-IO-03
- [ ] `tests/unit/vag/test_vag_write.c` -- covers ADPCM-IO-04
- [ ] `tests/cli/test_cli_adpcm.py` -- covers ADPCM-IO-01, ADPCM-IO-02
- [ ] `tests/python/binding/test_binding_adpcm.py` -- covers ADPCM-IO-05

## Sources

### Primary (HIGH confidence)
- Existing codebase: `src/cli/main.c`, `src/cli/wav_io.c`, `src/standalone/PluginEditor.cpp`, `src/standalone/PluginProcessor.cpp`, `python/spu94/_binding.py` -- all patterns are established
- `include/spu94/spu94.h` -- ADPCM API already exists (`spu94_set_adpcm_enabled`, `spu94_get_adpcm_enabled`)
- `include/spu94/spu94_adpcm.h` -- standalone encode/decode API with documented contracts
- `.planning/research/ARCHITECTURE-ADPCM.md` -- integration architecture, signal flow, state design

### Secondary (MEDIUM confidence)
- [Sony SDK VAGhdr struct](https://psx.arthus.net/code/VAG/vagsfx.c) -- 48-byte header layout
- [vgmstream VAG parser](https://github.com/vgmstream/vgmstream/blob/master/src/meta/vag.c) -- header parsing reference, version handling
- [psxsdk vag2wav.c](https://github.com/ColdSauce/psxsdk/blob/master/tools/vag2wav.c) -- flag byte interpretation, block iteration pattern

### Tertiary (LOW confidence)
- VAG stereo layout (dual-sequential vs interleaved) -- no single authoritative source; chosen per D-04 decision

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new libraries, all patterns established in codebase
- Architecture: HIGH -- follows existing patterns exactly (CLI dispatch, ctypes bindings, JUCE atomic)
- VAG format: HIGH -- well-documented across multiple reference implementations
- Pitfalls: HIGH -- all pitfalls are straightforward integration concerns, not algorithmic
- JUCE layout: MEDIUM -- exact pixel positions need measurement during implementation

**Research date:** 2026-04-27
**Valid until:** 2026-05-27 (stable domain, no external dependencies changing)
