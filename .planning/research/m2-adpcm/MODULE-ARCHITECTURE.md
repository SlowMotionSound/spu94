# Module Architecture Research: Reusable Codec Modules for a Digital Patina Engine

**Domain:** C API design for a collection of historical audio codec modules
**Researched:** 2026-04-26
**Overall confidence:** HIGH (core recommendation) / MEDIUM (future-codec scaling)

---

## 1. Executive Summary

The question is whether to build a generic "codec framework" or keep codecs as independent, purpose-built C modules. After examining FFmpeg's libavcodec, CLAP/LV2 plugin APIs, CMSIS-DSP, Faust, and SoX -- and weighing them against SPU-94's constraints (C99, zero-heap hot path, MCU portability, single developer) -- the answer is clear:

**Use independent per-codec APIs with a shared naming convention, not a vtable/function-pointer dispatch layer.**

The reasoning: a vtable abstraction earns its keep when a *host* needs to manipulate codecs generically (FFmpeg decoding arbitrary formats, a DAW loading arbitrary plugins). SPU-94's "digital patina engine" is not a host -- it is a *curated collection* where the developer knows every codec at compile time. The chaining and cross-codec features are caller-side composition, not runtime polymorphism. A vtable here adds indirection, makes debugging harder on MCUs, and solves a problem that does not exist.

What *does* matter: consistent naming, consistent state patterns, consistent block-function signatures, and a build system that lets each codec compile independently. The "framework" is a convention, not a type hierarchy.

---

## 2. The Core Decision: Convention Over Abstraction

### 2.1 Prior Art Comparison

| System | Pattern | Why They Chose It | Relevant to SPU-94? |
|--------|---------|-------------------|---------------------|
| FFmpeg libavcodec | AVCodec descriptor + AVCodecContext vtable | Must handle 400+ codecs discovered at runtime; plugins loaded dynamically | NO -- SPU-94 has 8-10 codecs known at compile time |
| CLAP/LV2 | Function-pointer vtable + extension queries | Host-plugin boundary: host and plugin are separate binaries | NO -- codecs are statically linked into the same binary |
| CMSIS-DSP | Independent per-function APIs (`arm_fir_*`, `arm_biquad_*`) | Each DSP function has different state shape; no runtime polymorphism needed | YES -- exactly analogous to our situation |
| SoX | `sox_effect_handler_t` vtable (getopts/start/flow/drain/stop) | Effects are loaded from a registry, user names them on CLI | PARTIALLY -- the handler pattern is clean but over-specified for embedded |
| Faust | Code generation with `buildUserInterface` metadata | DSP is compiled, not composed at runtime | YES for parameter self-description pattern |
| libsndfile | Independent per-format readers behind a file-type enum | Format detection at file open time | NO -- file format dispatching, not processing |

### 2.2 The CMSIS-DSP Precedent

CMSIS-DSP is the closest analogue. It provides dozens of DSP building blocks (FIR, biquad, FFT, etc.) for ARM Cortex-M, and it does NOT use a common vtable. Each function family has:

- Its own instance struct (`arm_fir_instance_f32`, `arm_biquad_casd_df1_inst_f32`)
- Its own init function (`arm_fir_init_f32`)
- Its own process function (`arm_fir_f32`)
- Caller-allocated state arrays (the instance struct holds pointers, not storage)

This works because:
1. The caller always knows which DSP block it is using
2. State sizes vary wildly (FIR: numTaps * sizeof(float), biquad: 4 * numStages * sizeof(float))
3. Zero-heap: caller provides all buffers
4. No runtime dispatch overhead

SPU-94's codec modules have identical constraints. Use the CMSIS-DSP pattern.

### 2.3 When a Vtable WOULD Be Warranted

If SPU-94 ever needs: a "load codec by name at runtime" feature, a plugin-host boundary where codecs are separate shared libraries, or a scripting language binding that manipulates codecs generically -- THEN add a thin vtable wrapper around the existing per-codec APIs. This is trivially additive: wrap what exists, don't build the abstraction first.

---

## 3. Recommended Codec Interface Convention

### 3.1 Naming Convention

Every codec follows this pattern (using `ps1adpcm` as the example):

```c
/* State */
typedef struct { ... } patina_ps1adpcm_state_t;

/* Sizing */
size_t patina_ps1adpcm_state_size(void);

/* Lifecycle */
patina_ps1adpcm_state_t *patina_ps1adpcm_init(void *buf, size_t buf_size);
void patina_ps1adpcm_reset(patina_ps1adpcm_state_t *state);

/* Encode: PCM -> compressed representation */
void patina_ps1adpcm_encode(patina_ps1adpcm_state_t *state,
                             const int16_t *pcm_in,
                             uint8_t *encoded_out,
                             uint32_t num_samples);

/* Decode: compressed representation -> PCM */
void patina_ps1adpcm_decode(patina_ps1adpcm_state_t *state,
                             const uint8_t *encoded_in,
                             int16_t *pcm_out,
                             uint32_t num_samples);

/* Color: PCM -> encode -> decode -> PCM (one-shot creative effect) */
void patina_ps1adpcm_color(patina_ps1adpcm_state_t *state,
                            const int16_t *pcm_in,
                            int16_t *pcm_out,
                            uint32_t num_samples);

/* Block size (for alignment/latency reporting) */
uint32_t patina_ps1adpcm_block_samples(void);

/* Parameter access (codec-specific) */
void patina_ps1adpcm_set_filter_mask(patina_ps1adpcm_state_t *state,
                                      uint8_t mask);
uint8_t patina_ps1adpcm_get_filter_mask(const patina_ps1adpcm_state_t *state);
```

### 3.2 The Three Levels of Each Codec

Every codec exposes three usage levels:

**Level 1: Block functions (pure, stateless per-call)**
```c
void patina_ps1adpcm_encode_block(const int16_t pcm[28],
                                   patina_ps1adpcm_hist_t *hist,
                                   uint8_t out[16]);
void patina_ps1adpcm_decode_block(const uint8_t block[16],
                                   patina_ps1adpcm_hist_t *hist,
                                   int16_t pcm[28]);
```
These are the atomic building blocks. Caller manages block alignment. Minimal state (just history samples). Useful for: unit tests, offline batch processing, embedded firmware with its own scheduler.

**Level 2: Stream functions (handle block alignment internally)**
```c
void patina_ps1adpcm_color(patina_ps1adpcm_state_t *state,
                            const int16_t *in, int16_t *out,
                            uint32_t num_samples);
```
Accept arbitrary sample counts. Internal double-buffering handles block alignment. Report latency. Useful for: integration into spu94_process, standalone plugin use.

**Level 3: Integration into SPU-94 (the current ARCHITECTURE-ADPCM.md approach)**
ADPCM state embedded in `spu94_state`, toggled via `spu94_set_adpcm_enabled()`. This is application-specific glue, not a reusable API.

The key insight: Level 1 and Level 2 are the reusable "codec module." Level 3 is per-application integration. The module architecture defines Levels 1 and 2; Level 3 is product-specific.

### 3.3 Naming Scheme Across Codecs

```
patina_ps1adpcm_*    -- PS1 SPU-ADPCM (28-sample blocks, 5 filters)
patina_snesbrr_*     -- SNES BRR (16-sample blocks, 4 filters)
patina_mulaw_*       -- mu-law companding (1-sample "blocks")
patina_alaw_*        -- A-law companding (1-sample "blocks")
patina_imaadpcm_*    -- IMA ADPCM (generic 4-bit ADPCM)
patina_gsm_*         -- GSM 06.10 RPE-LTP (160-sample frames)
patina_atrac1_*      -- ATRAC1 (512-sample MDCT blocks)
patina_cvsd_*        -- CVSD (1-bit delta, Bluetooth voice)
```

The `patina_` prefix namespaces the codec collection. Each codec gets its own header, its own source file(s), its own CMake target. No shared base type.

---

## 4. State Management Pattern

### 4.1 The Problem

State sizes vary by orders of magnitude:

| Codec | History State | Stream Buffer State | Total |
|-------|---------------|---------------------|-------|
| mu-law | 0 bytes | 0 bytes | ~4 bytes (just the enable flag) |
| PS1 ADPCM | 4 bytes (old/older) | ~228 bytes (double buffer for 28 samples) | ~250 bytes |
| SNES BRR | 4 bytes | ~132 bytes (double buffer for 16 samples) | ~150 bytes |
| IMA ADPCM | 6 bytes (predictor + step index) | ~8 bytes | ~20 bytes |
| GSM 06.10 | ~640 bytes (LPC state) | ~640 bytes (frame buffers) | ~1.3 KB |
| ATRAC1 | ~4 KB (MDCT windows, QMF state) | ~4 KB (block buffers) | ~8 KB |

### 4.2 Recommendation: Per-Codec Size Query (CMSIS-DSP Pattern)

Each codec provides:

```c
/* Compile-time upper bound for static allocation */
#define PATINA_PS1ADPCM_STATE_SIZE_MAX  512u
#define PATINA_PS1ADPCM_STATE_ALIGN     16u

/* Runtime exact size (for callers that want minimal allocation) */
size_t patina_ps1adpcm_state_size(void);
```

Caller idiom:

```c
/* Option A: stack/static allocation with compile-time bound */
alignas(PATINA_PS1ADPCM_STATE_ALIGN) uint8_t buf[PATINA_PS1ADPCM_STATE_SIZE_MAX];
patina_ps1adpcm_state_t *codec = patina_ps1adpcm_init(buf, sizeof(buf));

/* Option B: heap allocation with exact size (desktop use) */
size_t sz = patina_ps1adpcm_state_size();
void *buf = malloc(sz);
patina_ps1adpcm_state_t *codec = patina_ps1adpcm_init(buf, sz);
```

This is exactly SPU-94's existing pattern (`spu94_state_size()` + `SPU94_STATE_SIZE_MAX`). Extend it uniformly to every codec.

### 4.3 Why NOT a Union of All States

A `patina_codec_state_t` union containing all codecs' states would:
- Waste 8 KB per instance (ATRAC1's size) even for mu-law (4 bytes)
- Break the "independently compilable" property -- the union requires all codec headers
- Create ABI instability -- adding a larger codec changes every caller's allocation

### 4.4 Why NOT a Common Base Struct

A `patina_codec_base_t` with a type tag and function pointers adds:
- Indirection on every process call (function pointer dereference)
- Header coupling (everyone includes the base definition)
- Complexity for embedded targets (indirect calls are expensive on Cortex-M0)
- No benefit when the caller always knows which codec it is using

---

## 5. Creative Parameter Exposure

### 5.1 The Problem

Each codec has different creative parameters:

| Codec | Creative Parameters |
|-------|-------------------|
| PS1 ADPCM | Filter mask (which of 5 filters to allow), force-shift override |
| SNES BRR | Filter mask (which of 4 filters), gauss interpolation toggle |
| mu-law | Compression exponent (mu value: 255 standard, but sweepable) |
| A-law | Segment count override |
| IMA ADPCM | Step table modification, predictor clamp range |
| GSM 06.10 | LPC order, quantization table |

### 5.2 Recommendation: Codec-Specific Typed API (No Generic Layer)

Each codec exposes its own parameter getters/setters with specific types and documented ranges:

```c
/* PS1 ADPCM parameters */
void    patina_ps1adpcm_set_filter_mask(patina_ps1adpcm_state_t *s, uint8_t mask);
uint8_t patina_ps1adpcm_get_filter_mask(const patina_ps1adpcm_state_t *s);

void    patina_ps1adpcm_set_force_shift(patina_ps1adpcm_state_t *s, int8_t shift);
int8_t  patina_ps1adpcm_get_force_shift(const patina_ps1adpcm_state_t *s);

/* mu-law parameters */
void     patina_mulaw_set_mu(patina_mulaw_state_t *s, float mu);
float    patina_mulaw_get_mu(const patina_mulaw_state_t *s);
```

### 5.3 Why NOT a Generic Parameter Map

Options considered and rejected:

**String-keyed parameter map** (`set_param(state, "filter_mask", 0x1F)`):
- Requires string comparison in the hot path or a hash table
- No type safety -- everything is a double or void*
- Heap allocation for the map structure

**Numbered parameter slots** (`set_param(state, PARAM_0, value)`):
- Requires a header defining slot assignments per codec
- Caller must look up slot meanings -- no self-documentation
- Still no type safety

**Descriptor-based self-describing** (CLAP/LV2 pattern):
- Over-engineered for a C library with 8-10 known codecs
- The descriptor infrastructure (param_info structs, count/get_info/get_value vtable) costs more code than the codecs themselves
- Designed for host-plugin boundaries where host and plugin are separate binaries

**The right place for generic parameter enumeration is the JUCE wrapper**, not the C library. The JUCE AudioProcessor already has `getNumParameters()`, `getParameter()`, `setParameter()`. The wrapper maps CLAP/VST3 parameter IDs to codec-specific typed calls. This is exactly what SPU-94 already does with `ParameterBridge.h`.

### 5.4 Optional: Self-Description for Python/Scripting

If a future Python binding wants to enumerate parameters generically, add a per-codec descriptor function (not a vtable -- a static data structure):

```c
typedef struct {
    const char *name;       /* "filter_mask" */
    const char *label;      /* "Filter Mask" */
    float       min_value;
    float       max_value;
    float       default_value;
    uint32_t    flags;      /* PATINA_PARAM_STEPPED, etc. */
} patina_param_desc_t;

uint32_t patina_ps1adpcm_param_count(void);
const patina_param_desc_t *patina_ps1adpcm_param_desc(uint32_t index);
```

This is additive -- build it when Python bindings need it, not before. The typed API is the primary interface.

---

## 6. Block Size Alignment

### 6.1 The Problem

| Codec | Block Size (samples) | Latency (samples) |
|-------|---------------------|-------------------|
| mu-law | 1 | 0 |
| A-law | 1 | 0 |
| CVSD | 1 | 0 |
| SNES BRR | 16 | 16 |
| PS1 ADPCM | 28 | 28 |
| IMA ADPCM | varies (often 505) | varies |
| GSM 06.10 | 160 | 160 |
| ATRAC1 | 512 | 512 |

### 6.2 Recommendation: Each Codec Handles Its Own Alignment

The Level 2 stream API (`_color` function) handles block alignment internally using the double-buffer pattern from ARCHITECTURE-ADPCM.md Section 5.4b. The caller never sees block boundaries:

```c
/* Caller writes arbitrary sample counts. Codec handles alignment internally. */
patina_ps1adpcm_color(state, in_buf, out_buf, 137);  /* works fine */
patina_atrac1_color(state, in_buf, out_buf, 137);     /* also works fine */
```

Each codec reports its latency:

```c
uint32_t patina_ps1adpcm_latency_samples(void);   /* returns 28 */
uint32_t patina_atrac1_latency_samples(void);      /* returns 512 */
uint32_t patina_mulaw_latency_samples(void);       /* returns 0 */
```

For codec chaining, total latency is the sum of individual latencies.

### 6.3 Block Size in Chains

When chaining codecs (mu-law -> PS1 ADPCM -> SNES BRR), each codec's stream API independently manages its own block alignment. The chain is just sequential calls:

```c
patina_mulaw_color(mulaw_state, in, temp1, n);      /* 0 latency */
patina_ps1adpcm_color(adpcm_state, temp1, temp2, n); /* 28 sample latency */
patina_snesbrr_color(brr_state, temp2, out, n);      /* 16 sample latency */
/* Total chain latency: 44 samples */
```

No framework needed. The caller manages temp buffers (stack-allocated, known at compile time). This is simple, debuggable, and cache-friendly.

---

## 7. Codec Chaining API

### 7.1 Recommendation: Caller-Managed Sequential Calls (No Pipeline Abstraction)

A graph/pipeline abstraction would add:
- A node struct with input/output port descriptors
- A graph struct with edge connections
- A scheduling algorithm to process nodes in dependency order
- Buffer management for inter-node data
- Generic "process N samples" dispatch through function pointers

For 2-4 codecs in a chain, this is absurd overhead. The caller knows the chain at compile time.

### 7.2 What the Caller Does

```c
/* SPU-94 integration: ADPCM + reverb chain */
static void process_with_codecs(spu94_state *spu,
                                 patina_ps1adpcm_state_t *adpcm,
                                 const int16_t *L_in, const int16_t *R_in,
                                 int16_t *L_out, int16_t *R_out,
                                 uint32_t n) {
    int16_t temp_L[512], temp_R[512];   /* stack buffer, process in chunks */
    for (uint32_t i = 0; i < n; i += 512) {
        uint32_t chunk = (n - i < 512) ? n - i : 512;
        patina_ps1adpcm_color(adpcm, L_in + i, temp_L, chunk);
        patina_ps1adpcm_color(adpcm, R_in + i, temp_R, chunk);
        spu94_process(spu, temp_L, temp_R, L_out + i, R_out + i, chunk);
    }
}
```

Or for the "inside spu94_process" integration, the existing ARCHITECTURE-ADPCM.md approach (Section 7.2) is correct: ADPCM runs inline in `chain_step_impl`, not as an external pre-processor.

### 7.3 When to Revisit

If a future "patina rack" product lets users drag-and-drop codec modules into arbitrary chains via a GUI, then add a lightweight pipeline abstraction. But design it as a *consumer* of the per-codec APIs, not as a replacement for them.

---

## 8. Cross-Codec Encode/Decode

### 8.1 The Concept

"Encode with codec A, decode with codec B" -- for example, encode with PS1 ADPCM (4-bit, 5 filters), decode with SNES BRR (4-bit, 4 filters). The compressed representation is reinterpreted by a different decoder.

### 8.2 Compatibility Constraints

This only works when the encoded format is structurally compatible:

| Encode | Decode | Compatible? | Why |
|--------|--------|------------|-----|
| PS1 ADPCM | SNES BRR | PARTIAL | Both are 4-bit nibble-per-sample, but block sizes differ (28 vs 16 samples), header bytes differ |
| mu-law | A-law | YES | Both are 8-bit companded, sample-at-a-time, byte-compatible |
| PS1 ADPCM | IMA ADPCM | NO | Different block structures, different nibble packing |
| PS1 ADPCM | PS1 ADPCM | YES (trivially) | Same codec, this is just the normal coloration path |

### 8.3 Recommendation: Expose Level 1 Block Functions, Let the Caller Compose

Cross-codec processing is a creative misuse, not a standard path. The API should make it *possible* without making it *easy* -- because the caller needs to understand the compatibility constraints.

```c
/* Caller-managed cross-codec: encode PS1, decode BRR */
uint8_t block[16];
patina_ps1adpcm_encode_block(pcm_28, &adpcm_hist, block);
/* Now reinterpret the first 9 bytes as a BRR block (header + 8 byte-pairs = 16 samples) */
int16_t brr_out[16];
patina_snesbrr_decode_block(block, &brr_hist, brr_out);
```

No framework needed. The Level 1 block functions are the right abstraction -- they give the creative user direct access to the compressed bytes without imposing any compatibility assumptions.

### 8.4 Anti-Pattern: Don't Build a Cross-Codec Dispatch Table

A table mapping "which codecs can cross-talk" would be:
- Incomplete (the interesting misuses are the unexpected ones)
- Maintenance burden (N^2 entries for N codecs)
- False authority (suggesting "compatible" pairs are "safe" when creative abuse is the point)

---

## 9. Build System Organization

### 9.1 Recommendation: Each Codec as a CMake OBJECT Library + Umbrella Target

```
src/
  patina/
    ps1adpcm/
      patina_ps1adpcm.c
      patina_ps1adpcm_internal.h
      CMakeLists.txt           # OBJECT library: patina_ps1adpcm_obj
    snesbrr/
      patina_snesbrr.c
      patina_snesbrr_internal.h
      CMakeLists.txt           # OBJECT library: patina_snesbrr_obj
    mulaw/
      patina_mulaw.c
      CMakeLists.txt           # OBJECT library: patina_mulaw_obj
    CMakeLists.txt             # umbrella: add_subdirectory for each codec

include/
  patina/
    patina_ps1adpcm.h          # public API
    patina_snesbrr.h           # public API
    patina_mulaw.h             # public API
```

Each codec's OBJECT library:

```cmake
# src/patina/ps1adpcm/CMakeLists.txt
add_library(patina_ps1adpcm_obj OBJECT
    patina_ps1adpcm.c
)
target_include_directories(patina_ps1adpcm_obj
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
```

The umbrella combines them:

```cmake
# src/patina/CMakeLists.txt
add_subdirectory(ps1adpcm)
add_subdirectory(snesbrr)
add_subdirectory(mulaw)

# Optional: combined static library
add_library(patina STATIC
    $<TARGET_OBJECTS:patina_ps1adpcm_obj>
    $<TARGET_OBJECTS:patina_snesbrr_obj>
    $<TARGET_OBJECTS:patina_mulaw_obj>
)
```

### 9.2 Compile-Time Codec Selection

For embedded targets that only need one codec:

```cmake
option(PATINA_ENABLE_PS1ADPCM "Build PS1 ADPCM codec" ON)
option(PATINA_ENABLE_SNESBRR  "Build SNES BRR codec"  ON)
option(PATINA_ENABLE_MULAW    "Build mu-law codec"     ON)

if(PATINA_ENABLE_PS1ADPCM)
    add_subdirectory(ps1adpcm)
endif()
```

Each codec has zero dependencies on other codecs. An MCU firmware can link `patina_ps1adpcm_obj` alone without pulling in BRR, mu-law, or anything else.

### 9.3 Relationship to SPU-94

SPU-94 links the codecs it needs:

```cmake
# src/spu94/CMakeLists.txt (modified)
target_link_libraries(spu94_obj PRIVATE patina_ps1adpcm_obj)
```

The ADPCM integration code inside `spu94_io_chain.c` calls `patina_ps1adpcm_encode_block()` and `patina_ps1adpcm_decode_block()` (Level 1 functions). The `spu94_state` embeds the ADPCM history and buffer state directly (not via the Level 2 state struct) because SPU-94 manages its own block alignment in `chain_step_impl`.

### 9.4 Where Codecs Live Relative to SPU-94

Two viable options:

**Option A: Codecs inside the SPU-94 repo (recommended for now)**
```
PSX Reverb/
  src/spu94/          # reverb engine
  src/patina/         # codec modules
  include/spu94/      # reverb public API
  include/patina/     # codec public APIs
```
Simple. Single repo. Single CI pipeline. Extract to a separate repo later if needed.

**Option B: Codecs as a separate repo/submodule**
Premature. Wait until there is a second consumer (e.g., a standalone "Patina Rack" plugin).

---

## 10. Practical Transition: M2 ADPCM as First Module

### 10.1 What Changes from ARCHITECTURE-ADPCM.md

The existing ARCHITECTURE-ADPCM.md is excellent and mostly unchanged. The module architecture adds:

1. **Namespace change:** `spu94_adpcm_*` becomes `patina_ps1adpcm_*` for the standalone functions. The integration points inside `spu94_state` keep their `spu94_*` / `adpcm_*` naming.

2. **File location:** Block functions move from `src/spu94/spu94_adpcm.c` to `src/patina/ps1adpcm/patina_ps1adpcm.c`. The public header moves from `include/spu94/spu94_adpcm.h` to `include/patina/patina_ps1adpcm.h`.

3. **Integration unchanged:** `spu94_io_chain.c` still calls the block functions directly. The state embedding in `spu94_state` is unchanged. The `spu94_set_adpcm_enabled()` API is unchanged.

4. **Build integration:** `spu94_obj` gains a link dependency on `patina_ps1adpcm_obj`.

### 10.2 What Does NOT Change

- The double-buffer block alignment design (ARCHITECTURE-ADPCM.md Section 5.4b)
- The signal flow insertion point (before FIR decimator)
- The state sizing (well within SPU94_STATE_SIZE_MAX)
- The test strategy (standalone unit tests + integration tests)
- The phased build order

### 10.3 The Pattern for Codec #2 (SNES BRR)

When BRR arrives, the developer:
1. Creates `src/patina/snesbrr/` with the same file structure
2. Creates `include/patina/patina_snesbrr.h` with the same API shape
3. Writes `patina_snesbrr_encode_block()` and `patina_snesbrr_decode_block()`
4. Adds a `patina_snesbrr_color()` stream wrapper
5. No changes to PS1 ADPCM code. No changes to any "framework." No registration step.

That is the test of a good module architecture: adding codec #2 touches zero lines of codec #1.

---

## 11. Anti-Patterns to Avoid

### 11.1 Don't Build the Framework Before the Second Codec

The M2 ADPCM implementation should use the naming convention and file layout described here, but should NOT create any shared "patina framework" code (no `patina_codec.h` base type, no registration system, no pipeline graph). The convention IS the framework.

If after implementing BRR (codec #2), there is genuine shared code (e.g., a common double-buffer utility), extract it then. Not before.

### 11.2 Don't Genericize the `_color` Function Signature

It is tempting to make all `_color` functions take identical arguments so they can be stored in a function pointer. Resist this. Codecs that need extra parameters (ATRAC1 needs a quality setting per call) should have different signatures. The typed API is more valuable than uniformity.

### 11.3 Don't Share State Between Encode and Decode

Each codec's encoder and decoder maintain independent history. Even in the `_color` path (encode + decode), the encoder and decoder each have their own `old/older` (or equivalent). This is already correct in ARCHITECTURE-ADPCM.md Section 9.2.

### 11.4 Don't Put Codec State Inside a Generic Wrapper

Bad:
```c
typedef struct {
    patina_codec_type_t type;
    union {
        patina_ps1adpcm_state_t adpcm;
        patina_snesbrr_state_t brr;
    } impl;
} patina_codec_t;  /* DON'T */
```

Good:
```c
patina_ps1adpcm_state_t *adpcm;  /* caller knows what it has */
```

---

## 12. Summary of Recommendations

| Question | Answer |
|----------|--------|
| Common interface? | NO. Per-codec independent API with shared naming convention. |
| State management? | Per-codec `_state_size()` + `_STATE_SIZE_MAX` macro. Caller-allocated. |
| Parameter exposure? | Codec-specific typed getters/setters. Generic descriptors deferred to when needed. |
| Block alignment? | Each codec handles internally via double-buffer. Reports latency. |
| Chaining? | Caller-managed sequential calls. No pipeline abstraction. |
| Cross-codec? | Expose Level 1 block functions. Caller composes. No compatibility table. |
| Build system? | Per-codec OBJECT library. Optional umbrella static lib. Compile-time selection via CMake options. |
| Source tree? | `src/patina/<codec>/` + `include/patina/patina_<codec>.h` |
| Vtable dispatch? | Not now. Additive later if runtime polymorphism is needed. |

---

## 13. Confidence Assessment

| Area | Confidence | Rationale |
|------|------------|-----------|
| Independent-vs-vtable decision | HIGH | CMSIS-DSP precedent is directly analogous; FFmpeg/CLAP vtable pattern clearly serves a different use case (runtime polymorphism across binary boundaries) |
| State management pattern | HIGH | Directly extends SPU-94's existing caller-allocated pattern |
| Parameter exposure | HIGH | Typed API is standard practice for C libraries; generic descriptors are an additive concern |
| Block alignment | HIGH | Double-buffer pattern from ARCHITECTURE-ADPCM.md is well-understood and proven |
| Build organization | MEDIUM | File layout is straightforward; the `patina_` namespace and directory structure may evolve as the collection grows |
| Cross-codec processing | MEDIUM | The "let the caller compose" approach is correct but the creative use cases are speculative |
| Chaining API | HIGH | Sequential caller-managed calls are simpler and more debuggable than a pipeline abstraction for 2-4 stages |

---

## Sources

- [FFmpeg libavcodec architecture (DeepWiki)](https://deepwiki.com/FFmpeg/FFmpeg/3.2-libavcodec-codec-library) -- codec registration, AVCodec vs AVCodecContext
- [CLAP plugin API](https://github.com/free-audio/clap) -- extension-based parameter exposure
- [CLAP params.h](https://github.com/free-audio/clap/blob/main/include/clap/ext/params.h) -- clap_param_info descriptor struct
- [CLAP tutorial (nakst)](https://nakst.gitlab.io/tutorial/clap-part-1.html) -- minimal CLAP implementation
- [LV2 plugin specification](https://lv2plug.in/ns/lv2core) -- LV2_Descriptor function pointer pattern
- [LV2 programming guide](https://drobilla.net/files/lv2_plugin_guide/guide.html) -- instantiate/connect_port/run lifecycle
- [CMSIS-DSP](https://github.com/ARM-software/CMSIS-DSP) -- independent per-function instance structs, caller-allocated state
- [CMSIS-DSP biquad reference](https://arm-software.github.io/CMSIS-DSP/main/group__BiquadCascadeDF1.html) -- state array organization, coefficient sharing
- [Faust DSP language](https://faustdoc.grame.fr/manual/syntax/) -- declare metadata, buildUserInterface parameter exposure
- [SoX effects chain](https://fossies.org/dox/sox-14.4.2/sox_8h.html) -- sox_effect_handler_t vtable pattern
- [Embedded HAL patterns](https://blog.mbedded.ninja/programming/design-patterns/abstraction-layers/) -- function pointer abstraction layers in C
- [SPU-94 ARCHITECTURE-ADPCM.md](../ARCHITECTURE-ADPCM.md) -- existing ADPCM integration design (local)
