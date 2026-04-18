# Architecture Research — SPU-94

**Domain:** Bit-faithful DSP reimplementation of the Sony PS1 SPU reverb (C library + Python bindings, MCU/FPGA-portable)
**Researched:** 2026-04-18
**Confidence:** HIGH on algorithmic topology (Q1) and buffer model — both cited verbatim from nocash psx-spx via the psx-spx.consoledev.net mirror. MEDIUM on mid-stream register update semantics — nocash gives only a partial timing note; the rest is design space. HIGH on C-library layout and build-order — follows standard embedded-DSP practice.

---

## 1. The PS1 SPU Reverb Algorithm — Processing Topology

This section is the spec interpretation. Every non-trivial claim is attributable to nocash's psx-spx SPU chapter.

### 1.1 Per-Sample Algorithm (Verbatim from nocash psx-spx)

The reverb runs **once per 22050 Hz cycle**, producing both L and R on the same cycle. (nocash phrases this as "the reverb hardware spends one 44100h cycle on left calculations, and the next 44100h cycle on right calculations" — i.e., the unit is a 22050 Hz block of work internally time-multiplexed across two 44.1 kHz cycles. For a software reimplementation we model it as **one 22050 Hz tick produces one stereo output**; the time-multiplex detail is a hardware scheduling artifact, not algorithm-visible.)

The documented pseudocode (verbatim, nocash ["SPU Reverb Formula"]):

```
; ---- Input stage ----
Lin = vLIN * LeftInput
Rin = vRIN * RightInput

; ---- Same-side reflection (L-to-L, R-to-R) ----
[mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]
[mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]

; ---- Different-side reflection (L-to-R, R-to-L) ----
[mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]
[mRDIFF] = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]

; ---- Early Echo (4-tap comb filter) ----
Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2] + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
Rout = vCOMB1*[mRCOMB1] + vCOMB2*[mRCOMB2] + vCOMB3*[mRCOMB3] + vCOMB4*[mRCOMB4]

; ---- Late Reverb APF1 ----
Lout = Lout - vAPF1*[mLAPF1-dAPF1]; [mLAPF1] = Lout; Lout = Lout*vAPF1 + [mLAPF1-dAPF1]
Rout = Rout - vAPF1*[mRAPF1-dAPF1]; [mRAPF1] = Rout; Rout = Rout*vAPF1 + [mRAPF1-dAPF1]

; ---- Late Reverb APF2 ----
Lout = Lout - vAPF2*[mLAPF2-dAPF2]; [mLAPF2] = Lout; Lout = Lout*vAPF2 + [mLAPF2-dAPF2]
Rout = Rout - vAPF2*[mRAPF2-dAPF2]; [mRAPF2] = Rout; Rout = Rout*vAPF2 + [mRAPF2-dAPF2]

; ---- Output to mixer ----
LeftOutput  = Lout * vLOUT
RightOutput = Rout * vROUT

; ---- Advance buffer pointer ----
BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)
; then wait one 22050 Hz cycle and repeat
```

Per nocash: "The values written to memory are saturated to -8000h..+7FFFh. The multiplication results are divided by +8000h, to fit them to 16-bit range."

### 1.2 Processing Order — Precise Restatement for Implementers

Per 22050 Hz tick, in order:

1. **Resample input** — 44.1 kHz stereo `(L, R)` pair goes through a 39-tap FIR (see 1.4) to produce one 22050 Hz stereo sample `(LeftInput, RightInput)` consumed by the reverb.
2. **Input scale** — compute `Lin`, `Rin`.
3. **Same-side IIR** — read `[dLSAME]`, `[mLSAME-2]`, `[dRSAME]`, `[mRSAME-2]`; write back to `[mLSAME]` and `[mRSAME]`. Saturate before store.
4. **Different-side IIR** — read `[dRDIFF]`, `[mLDIFF-2]`, `[dLDIFF]`, `[mRDIFF-2]`; write to `[mLDIFF]`, `[mRDIFF]`. Saturate before store.
5. **Comb sum** — read four delay taps per channel; compute `Lout`, `Rout`. Each multiply divides by 8000h.
6. **APF1** — standard Schroeder all-pass on `Lout`/`Rout` with tap offset `dAPF1`. Writes to `[mLAPF1]`, `[mRAPF1]`. Saturate on write.
7. **APF2** — same structure with `dAPF2`. Writes to `[mLAPF2]`, `[mRAPF2]`.
8. **Output scale** — multiply by `vLOUT`, `vROUT`, saturate.
9. **Resample output** — 22050 Hz `(Lout, Rout)` goes through the 39-tap FIR (see 1.4) to produce two 44.1 kHz stereo samples.
10. **Advance** — `BufferAddress = MAX(mBASE, (BufferAddress+2) & 0x7FFFE)`.

**Register read timing.** Every register is consumed in the step where it appears above. There is no prefetch; all reads are "current value" at the step. (This is what makes mid-stream register writes a correctness question — see §4.)

### 1.3 LSAME/RSAME vs LDIFF/RDIFF — What They Are

Both are identical Schroeder-style **first-order IIR filters** parameterized by `vIIR` (feedback coefficient) and `vWALL` (wall-reflection coefficient applied to the delayed tap), operating on different taps of the reverb work buffer:

- **LSAME / RSAME**: L-to-L and R-to-R paths. Each channel's input feeds back into its own delay line. Creates per-channel diffusion.
- **LDIFF / RDIFF**: Cross-channel paths — `Lin` writes the `LDIFF` line (which feeds right output via `mRDIFF`), and vice versa. Creates stereo "width"/cross-feed. Note in the formula: the **cross-feed is through the `dRDIFF`/`dLDIFF` taps paired with `Lin`/`Rin` respectively** — the input source for `[mLDIFF]` is `Lin` but the wall-reflection tap is `[dRDIFF]`. This is deliberate and creates the cross-channel bleed.

Algorithmically identical IIR structure; different tap addresses and different pairings of input-to-wall-tap is what produces the stereo character.

### 1.4 The 22.05 kHz Question — Resolved via 39-tap FIR

Per nocash: "Input and output to/from the reverb unit is resampled using a 39-tap FIR filter with the following coefficients":

```
-0001h, 0000h, 0002h, 0000h, -000Ah, 0000h, 0023h, 0000h,
-0067h, 0000h, 010Ah, 0000h, -0268h, 0000h, 0534h, 0000h,
-0B90h, 0000h, 2806h, 4000h, 2806h, 0000h, -0B90h, 0000h,
 0534h, 0000h, -0268h, 0000h, 010Ah, 0000h, -0067h, 0000h,
 0023h, 0000h, -000Ah, 0000h, 0002h, 0000h, -0001h
```

This is a symmetric half-band FIR (the `0000h` every other tap is the half-band signature; center tap is `4000h`). It is used **both ways**:

- **Decimation (44.1 → 22.05):** run FIR over last 39 input samples, take output, drop every other phase.
- **Interpolation (22.05 → 44.1):** zero-stuff, run same FIR, produces 2 output samples per 1 reverb sample.

SPU-94 implementation implication: input and output each need a 39-sample FIR delay line (stereo → 2 × 39 int16 per direction, or 2 × 78 bytes per direction). These live in the reverb handle, not in the main work buffer.

**Important (LOW confidence, flagged for witness verification):** ipatix's `lv2-psx-reverb` project README explicitly states it does **not** downsample the reverb to 22050 Hz — it runs the core at the host rate directly. This is a deliberate fidelity tradeoff on their part. SPU-94 must model the 22050 Hz core with the 39-tap FIR on both sides to match original hardware; this is the bit-faithfulness commitment. (Sources: [ipatix/lv2-psx-reverb README](https://github.com/ipatix/lv2-psx-reverb/blob/master/README.md), cross-referenced to nocash's "Reverb Buffer Resampling" section.)

### 1.5 Buffer Addressing

- Work buffer is a region of SPU RAM (original HW: 512 KiB at 16-bit word granularity, addresses 00000h..7FFFEh stepping by 2).
- Reverb region is `[mBASE .. 0x7FFFE]`. `mBASE` is written as a word address; size = `0x80000 - mBASE` bytes.
- `BufferAddress` advances by 2 (one 16-bit word) per 22050 Hz tick.
- Wrap: `BufferAddress = MAX(mBASE, (BufferAddress+2) AND 0x7FFFE)`. The `AND 0x7FFFE` wraps at SPU RAM top; the `MAX(mBASE, …)` clamps the wrap destination back to `mBASE`.
- All register taps (`mLSAME`, `dLSAME`, `[mLSAME-2]`, etc.) are **offsets added to `BufferAddress`, then wrapped the same way**.
- Per nocash: "Writing a value to mBASE does additionally set the current buffer address to that value." This is important: writing mBASE is not just setting the floor — it's a reset of the read/write head.

In SPU-94's software model, the "SPU RAM" abstraction can be collapsed: the reverb only touches its own region, so SPU-94 allocates a **user-supplied int16 buffer of `N` words where N ≥ the preset's working-set size** (largest documented preset is Chaos Echo / Delay at `0x18040` bytes = 49216 bytes ≈ 24608 int16 words).

### 1.6 Fixed-Point Math Details

- All registers holding coefficients are **signed 16-bit** treated as Q15 (one sign bit + 15 fractional bits), range `-1.0 ≈ -0x8000 .. +0x7FFF ≈ +0x7FFF/0x8000`.
- All delay-line samples are **signed 16-bit**, range `-0x8000..+0x7FFF`.
- Every `a * b` where both are 16-bit Q15 produces 32-bit Q30; the spec reduces by `/0x8000` (shift right by 15, sign-extended) back to Q15 range. **Truncation toward zero (or toward -∞ via arithmetic shift)** — this is a gray area: arithmetic right shift rounds toward -∞, which is *not* the same as "truncation toward zero." The PS1 almost certainly uses arithmetic right shift (1-cycle hardware op); this must be documented in `DECISIONS.md` and verified by witness diff.
- Saturation: **on write to the work buffer** (per nocash: "The values written to memory are saturated to -8000h..+7FFFh"). Intermediate accumulations in the comb sum (Step 5) can exceed 16-bit range; the 4-term sum is computed at wider width and clamped at the **output scale step** as a matter of design interpretation. *(Gray area — document resolution.)*
- Known hardware bug per nocash: "vIIR works only in range -7FFFh..+7FFFh. When set to -8000h, the multiplication by -8000h is still done correctly, but the final result gets negated." SPU-94 should reproduce this bug for bit-fidelity.

### 1.7 Register Reference (for Q2/Q3)

| Addr | Name | Width | Type | Used In |
|---|---|---|---|---|
| 1F801DA2 | mBASE | u16 | Base address (word-granular, implicitly ×8) | buffer init, BufferAddress reset |
| 1F801DC0 | dAPF1 | u16 | Displacement (×8) | APF1 tap: `[mLAPF1-dAPF1]` |
| 1F801DC2 | dAPF2 | u16 | Displacement (×8) | APF2 tap |
| 1F801DC4 | vIIR | s16 Q15 | Coefficient | SAME & DIFF IIR feedback |
| 1F801DC6 | vCOMB1 | s16 Q15 | Coefficient | Comb tap 1 |
| 1F801DC8 | vCOMB2 | s16 Q15 | Coefficient | Comb tap 2 |
| 1F801DCA | vCOMB3 | s16 Q15 | Coefficient | Comb tap 3 |
| 1F801DCC | vCOMB4 | s16 Q15 | Coefficient | Comb tap 4 |
| 1F801DCE | vWALL | s16 Q15 | Coefficient | Wall reflection in SAME/DIFF |
| 1F801DD0 | vAPF1 | s16 Q15 | Coefficient | APF1 gain |
| 1F801DD2 | vAPF2 | s16 Q15 | Coefficient | APF2 gain |
| 1F801DD4 | mLSAME | u16 | Write address (×8) | `[mLSAME]`, `[mLSAME-2]` |
| 1F801DD6 | mRSAME | u16 | Write address (×8) | `[mRSAME]`, `[mRSAME-2]` |
| 1F801DD8 | mLCOMB1 | u16 | Read address (×8) | `[mLCOMB1]` |
| 1F801DDA | mRCOMB1 | u16 | Read address (×8) | `[mRCOMB1]` |
| 1F801DDC | mLCOMB2 | u16 | Read address (×8) | `[mLCOMB2]` |
| 1F801DDE | mRCOMB2 | u16 | Read address (×8) | `[mRCOMB2]` |
| 1F801DE0 | dLSAME | u16 | Read address (×8) | `[dLSAME]` (wall tap) |
| 1F801DE2 | dRSAME | u16 | Read address (×8) | `[dRSAME]` |
| 1F801DE4 | mLDIFF | u16 | Write address (×8) | `[mLDIFF]`, `[mLDIFF-2]` |
| 1F801DE6 | mRDIFF | u16 | Write address (×8) | `[mRDIFF]`, `[mRDIFF-2]` |
| 1F801DE8 | mLCOMB3 | u16 | Read address (×8) | `[mLCOMB3]` |
| 1F801DEA | mRCOMB3 | u16 | Read address (×8) | `[mRCOMB3]` |
| 1F801DEC | mLCOMB4 | u16 | Read address (×8) | `[mLCOMB4]` |
| 1F801DEE | mRCOMB4 | u16 | Read address (×8) | `[mRCOMB4]` |
| 1F801DF0 | dLDIFF | u16 | Read address (×8) | `[dLDIFF]` |
| 1F801DF2 | dRDIFF | u16 | Read address (×8) | `[dRDIFF]` |
| 1F801DF4 | mLAPF1 | u16 | Write address (×8) | `[mLAPF1]`, `[mLAPF1-dAPF1]` |
| 1F801DF6 | mRAPF1 | u16 | Write address (×8) | `[mRAPF1]` |
| 1F801DF8 | mLAPF2 | u16 | Write address (×8) | `[mLAPF2]` |
| 1F801DFA | mRAPF2 | u16 | Write address (×8) | `[mRAPF2]` |
| 1F801DFC | vLIN | s16 Q15 | Coefficient | Left input scale |
| 1F801DFE | vRIN | s16 Q15 | Coefficient | Right input scale |
| 1F801D84 | vLOUT | s16 Q15 | Coefficient | Left output scale |
| 1F801D86 | vROUT | s16 Q15 | Coefficient | Right output scale |

That's **33 registers**. (Project spec says 24 — the discrepancy is because `mBASE`, `vLOUT`, `vROUT` live outside the `1F801DC0..DFE` block; and the SAME/DIFF/APF address pairs count as one 32-bit register on hardware but should be modeled as two 16-bit registers in software. Update project count during kickoff.)

---

## 2. Module / File Decomposition for the C Library

### 2.1 Public Header (`include/spu94.h`)

The public header exposes an **opaque handle** — caller never sees struct internals. This gives forward compatibility for M2/M3/M4 additions.

```c
#ifndef SPU94_H
#define SPU94_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle. Allocated and owned by caller; size fixed at compile time. */
typedef struct spu94_state spu94_state_t;

/* How big is spu94_state_t? Caller allocates this many bytes (aligned). */
size_t spu94_state_size(void);
size_t spu94_state_alignment(void);  /* typically 8 or 16 */

/* Required work buffer size for a given preset size in bytes. */
size_t spu94_work_buffer_size_bytes(uint32_t preset_work_size_bytes);

/* Init. work_buffer is caller-owned; must be at least spu94_work_buffer_size_bytes(...).
 * Register set starts zeroed; call spu94_load_preset or spu94_write_reg next.
 * Returns 0 on success, negative on error. */
int spu94_init(spu94_state_t *state,
               int16_t     *work_buffer,
               size_t       work_buffer_bytes);

/* Register write. register_id is spu94_reg_t enum (see below).
 * Mid-stream writes are safe; semantics are documented in spu94_reg_t comments. */
int spu94_write_reg(spu94_state_t *state, int register_id, uint16_t value);

/* Register read (for introspection, tests, preset save). */
uint16_t spu94_read_reg(const spu94_state_t *state, int register_id);

/* Bulk load: atomic, deterministic, glitch-free register snapshot apply. */
int spu94_load_preset(spu94_state_t *state, const uint16_t regs[33]);

/* Process N stereo samples at 44.1 kHz.
 *   in_l, in_r: N int16 samples each (host sample rate)
 *   out_l, out_r: N int16 samples each (host sample rate)
 * In-place (in_l == out_l) is supported. */
void spu94_process(spu94_state_t *state,
                   const int16_t *in_l, const int16_t *in_r,
                   int16_t       *out_l, int16_t       *out_r,
                   size_t         n_samples);

/* Reset: clears work buffer, resamplers, BufferAddress; preserves register values.
 * Call when audio stream discontinuity occurs (seek, stop/start). */
void spu94_reset(spu94_state_t *state);

/* Version / identity. */
const char *spu94_version_string(void);

#ifdef __cplusplus
}
#endif
#endif
```

**Rationale:**
- Opaque handle means `spu94_state_t` size/layout can evolve without breaking callers.
- `spu94_state_size() + caller-allocates` means zero heap use in the library. Suits MCU/FPGA/real-time hosts.
- Work buffer is caller-provided so static allocation and memory-mapped placement (e.g., SDRAM on Daisy) are both trivial.
- Process function is block-oriented (N samples) not sample-oriented: amortizes call overhead and matches JUCE / LV2 / PortAudio host models. Internally processes in 2-sample chunks (one 22050 Hz tick per 2 host samples).
- `load_preset` is the atomic-swap path for gray-area-safe preset changes; `write_reg` is the live-modulation path.

### 2.2 Private Source Files

```
src/
├── spu94_public.c       # Public API dispatch, handle size/align, init, process loop
├── spu94_reverb.c       # The per-tick reverb algorithm (§1.2 steps 2-8)
├── spu94_resample.c     # 39-tap FIR up/down (input & output paths)
├── spu94_registers.c    # Register write/read, validation, double-buffer latch
├── spu94_buffer.c       # BufferAddress arithmetic, tap-address math, wrap, mBASE reset
├── spu94_fixed.c        # Q15 saturating math: sat16(x), mul_q15(a,b), shift behavior
├── spu94_clip.c         # Hard clip stage on the mix bus feeding reverb input
└── spu94_presets.c      # The 10 factory preset register arrays (read-only data)

include/
└── spu94.h              # Public API (above)

src/internal/
├── spu94_state.h        # Full struct spu94_state definition (private)
├── spu94_math.h         # Inline Q15 helpers
└── spu94_reg_ids.h      # enum spu94_reg_t with all register IDs + doc comments
```

**Boundary rationale:**
- `spu94_reverb.c` contains only pure algorithmic code — one function, `spu94_tick_22khz(state)`, with no I/O. Easiest to audit against spec.
- `spu94_fixed.c` is isolated so truncation/rounding/saturation semantics can be swapped wholesale during DECISIONS.md gray-area resolution without touching the algorithm file.
- `spu94_buffer.c` owns tap-address math (`idx = (BufferAddress + offset) AND 0x7FFFE; MAX(mBASE, idx)`). Centralizing the wrap logic makes it trivial to prove correct and to swap for an FPGA-friendly modulo if needed.
- `spu94_registers.c` is the mid-stream-safety chokepoint: every write goes through validation (§4) here. Tests can target register writes without involving the DSP path.
- `spu94_clip.c` exists as a standalone file because (a) the hard-clip stage is on the *input* mix bus before reverb, not inside the reverb network, and (b) it's a first-class M1 deliverable separate from the reverb topology.
- `spu94_presets.c` is static data only — no code. MCU toolchains put this in `.rodata` / flash, which is exactly right.

### 2.3 Register Validation

Lives in `spu94_registers.c::spu94_write_reg()`. Each register ID has a validator that runs on write:

```c
typedef struct {
    uint16_t mask;         /* bits that are legal (usually 0xFFFF) */
    uint16_t min, max;     /* range check after masking */
    uint8_t  scope;        /* SPU94_SCOPE_IMMEDIATE or SPU94_SCOPE_LATCHED */
    uint8_t  reset_buffer; /* 1 for mBASE */
} spu94_reg_spec_t;
```

A static array `spu94_reg_spec_t spu94_reg_specs[33]` drives validation.

### 2.4 Work Buffer Representation

```c
struct spu94_state {
    /* ---- Runtime state ---- */
    uint32_t buffer_address;          /* word offset into work_buf, always even */
    uint32_t m_base;                  /* cached mBASE in word units (/2) */

    /* ---- Register shadow (live values) ---- */
    uint16_t regs[SPU94_N_REGS];

    /* ---- Register pending (for latched writes, see §4 Option B/C) ---- */
    uint16_t pending_regs[SPU94_N_REGS];
    uint64_t pending_mask;            /* bit-per-register dirty flag */

    /* ---- Input resampler: 39-tap FIR per channel ---- */
    int16_t  in_fir_l[39];
    int16_t  in_fir_r[39];
    uint8_t  in_fir_pos;

    /* ---- Output resampler: 39-tap FIR per channel ---- */
    int16_t  out_fir_l[39];
    int16_t  out_fir_r[39];
    uint8_t  out_fir_pos;

    /* ---- Host-rate sample pairing ---- */
    uint8_t  host_phase;              /* 0 or 1 within a 22050 Hz cycle */
    int16_t  cached_out_l, cached_out_r;  /* held for odd host sample */

    /* ---- Work buffer (caller-owned pointer; size at init) ---- */
    int16_t *work_buf;                /* int16 words, size_words elements */
    uint32_t work_size_words;         /* >= preset_work_size_bytes/2 */
};
```

- **int16 storage** in the work buffer — matches SPU RAM, saturation on write enforces 16-bit semantics.
- **int32 intermediate** in the reverb algorithm for products and sums (so `vCOMB1*tap + vCOMB2*tap + ...` doesn't overflow before saturation).
- **Alignment**: 8-byte alignment for the state struct; 2-byte alignment for the int16 work buffer. Not SIMD-aligned — no SIMD in core. FPGA HLS doesn't care.

---

## 3. Data Representation Choices

### 3.1 Sample Format

- **Storage (work buffer, register-held samples):** `int16_t` (Q15 signed). Matches SPU RAM exactly. Saturated on store.
- **Compute (intermediate):** `int32_t` for a×b products (32-bit result of two 16-bit operands) and for sums (comb filter sums four products). Always narrowed back via `sat_q15(int32)` on store.
- **Coefficient registers:** stored as `uint16_t` (raw register bits) but interpreted as `int16_t` Q15 at the multiply. This matches the fact that the SPU is written via MMIO as u16 but semantically treats the values as s16.

Citation: nocash "Reverb Configuration Area" — all volume registers are annotated as signed 16-bit; all address/displacement registers are unsigned 16-bit scaled by 8.

### 3.2 Register Storage

**Recommendation: flat `uint16_t regs[33]` array indexed by `spu94_reg_t` enum.**

```c
typedef enum {
    SPU94_REG_MBASE = 0,
    SPU94_REG_DAPF1, SPU94_REG_DAPF2,
    SPU94_REG_VIIR,
    SPU94_REG_VCOMB1, SPU94_REG_VCOMB2, SPU94_REG_VCOMB3, SPU94_REG_VCOMB4,
    SPU94_REG_VWALL,
    SPU94_REG_VAPF1, SPU94_REG_VAPF2,
    SPU94_REG_MLSAME, SPU94_REG_MRSAME,
    SPU94_REG_MLCOMB1, SPU94_REG_MRCOMB1, SPU94_REG_MLCOMB2, SPU94_REG_MRCOMB2,
    SPU94_REG_DLSAME, SPU94_REG_DRSAME,
    SPU94_REG_MLDIFF, SPU94_REG_MRDIFF,
    SPU94_REG_MLCOMB3, SPU94_REG_MRCOMB3, SPU94_REG_MLCOMB4, SPU94_REG_MRCOMB4,
    SPU94_REG_DLDIFF, SPU94_REG_DRDIFF,
    SPU94_REG_MLAPF1, SPU94_REG_MRAPF1, SPU94_REG_MLAPF2, SPU94_REG_MRAPF2,
    SPU94_REG_VLIN, SPU94_REG_VRIN,
    SPU94_REG_VLOUT, SPU94_REG_VROUT,
    SPU94_N_REGS
} spu94_reg_t;
```

**Trade-off accepted:** Slight loss of type safety (can write vIIR value to mBASE slot by mistake) traded for:
- Trivial preset save/restore: `memcpy(dst, state->regs, sizeof(state->regs))`.
- Trivial preset load: `spu94_load_preset(state, preset_array)`.
- Trivial Python binding: numpy `uint16` array of shape `(33,)`.
- Trivial introspection for tests and the future levers catalog.
- Tiny binary, perfect for flash-resident data on MCU.

Named struct fields would be more type-safe but hostile to bulk operations and bindings. Validation in `write_reg()` is sufficient safety.

### 3.3 Reverb Work Buffer

- Type: `int16_t[]` — matches SPU RAM word width, zero-copy to/from register reads.
- Size: caller-determined. SPU-94 provides `spu94_work_buffer_size_bytes(preset_size)` helper. Max documented preset (Chaos Echo / Delay, 0x18040 bytes) = 49216 bytes = 24608 int16 words. For maximum portability, SPU-94 should also support the full hardware region: `(0x80000 - mBASE_min) / 2` words — but in practice an app-defined cap is fine.
- Allocation: caller-owned, static or heap-allocated by caller. Library never allocates. Matches MCU and real-time constraints cleanly.

---

## 4. Mid-Stream Register Update Strategy — Options & Tradeoffs

This is a flagged gray area. nocash says little beyond: *"The SPU seems to process written values at 44100Hz rate (so it may take 1/44100 seconds (300h clock cycles) until it has actually realized the new value)."* This statement applies to the general SPU, not specifically to reverb registers; the reverb-specific latency is undocumented.

Four options, each with concrete implications:

### Option A — Fully Immediate

Every register write takes effect on the next 22050 Hz tick.

- **Pros:** Simplest model; matches pseudocode literally. Minimum latency for modulation. Easy to implement.
- **Cons:** Writing an `m*` address register mid-block makes the write position of the just-completed SAME/DIFF/APF step inconsistent with the read position of the next step. For delay-length registers (`dAPF1`, `dAPF2`, `dLSAME`, `dRSAME`, `dLDIFF`, `dRDIFF`, and any `m*` that changes tap distance), this creates click/pop/discontinuity. For pure gain registers (`vIIR`, `vWALL`, `vCOMB*`, `vAPF*`, `vLIN`, `vRIN`, `vLOUT`, `vROUT`), this is completely benign (at worst a 1-sample gain step). Writing `mBASE` additionally resets `BufferAddress` per hardware behavior — which is a full buffer-position discontinuity and will audibly glitch unless the buffer is also cleared.
- **Who uses this:** Closest to "raw hardware" behavior. DuckStation-style bit-accurate emulators.

### Option B — Fully Latched at 22050 Hz Tick Boundary

All writes during a host block queue into `pending_regs`; applied atomically at the start of each 22050 Hz tick.

- **Pros:** Deterministic, easy to reason about. A burst of writes (e.g., preset change = 33 writes) produces exactly one discontinuity rather than up to 33 glitches.
- **Cons:** Adds 1 tick (22.7 µs) of modulation latency — inaudible. Requires a pending-mask and double-buffer.
- **Who uses this:** Custom modern reimplementations where modulation safety is a goal. Good default for SPU-94.

### Option C — Per-Register Policy (Hybrid)

Each register has a policy in its spec:
- `SCOPE_IMMEDIATE` — gain registers (`vIIR`, `vWALL`, `vCOMB1-4`, `vAPF1-2`, `vLIN/RIN`, `vLOUT/ROUT`). Update on next tick.
- `SCOPE_LATCHED` — address/displacement registers (`m*`, `d*`). Buffered until caller invokes `spu94_commit_latched(state)` OR at the start of each tick (configurable).
- `SCOPE_RESET` — `mBASE`. Takes effect immediately AND resets `BufferAddress`, matching hardware; caller should `spu94_reset(state)` around this to avoid audible glitch.

- **Pros:** Matches the actual risk profile per-register. Gain modulation is free; delay-length modulation is explicitly acknowledged as a distinct kind of update. Allows M4 levers layer to know the cost of each register change.
- **Cons:** More complex API; more code paths to test.
- **Who uses this:** What "bit-faithful but glitch-free" actually looks like in practice.

### Option D — Parameter Smoothing / Interpolation in Core

Smooth coefficient changes over K samples to de-zipper modulation.

- **Pros:** Eliminates zipper noise on fast modulation of gain coefficients.
- **Cons:** **Breaks bit-faithfulness.** Smoothed coefficients are not what the hardware does. This behavior belongs in M4's lever abstraction, layered on top of the bit-faithful M1 core.
- **Recommendation:** Do NOT put in M1 core. Document it as the M4 lever-layer concern.

### Witness Behavior (LOW confidence — not read from source)

- **Mednafen:** Believed to process the reverb block on each SPU cycle, which effectively makes all writes immediate at the hardware-cycle level. Not verified from source (licensing posture precludes reading).
- **lv2-psx-reverb (ipatix):** Runs reverb at host rate with float math; is not bit-faithful by design; its register-timing semantics are not relevant as a reference.
- **DuckStation:** Targets bit-accurate emulation, likely Option A equivalent at hardware clock granularity.

### Recommended M1 Approach

**Adopt Option C (per-register policy) as the public behavior, with the following M1 choices:**
- All `v*` registers: `SCOPE_IMMEDIATE`. Zipper noise tolerated in core; M4 layer handles smoothing.
- All `d*` registers: `SCOPE_LATCHED` at 22050 Hz tick boundary (cheap; one extra write per tick; no click).
- All `m*` address registers except `mBASE`: `SCOPE_LATCHED`.
- `mBASE`: special — writing resets `BufferAddress` (matches HW), caller is expected to call `spu94_reset()` before/after. `DECISIONS.md` records whether this also zeros the work buffer (matches `spu94_reset`) or leaves stale content (matches HW).

This policy is **the most useful behavior given the modulation-first design goal**; gain modulation is glitch-free at the cost of 22.7 µs latency; delay-length changes are sample-aligned (no click from mid-tick address change) at the cost of being quantized to tick boundaries. Both acceptable.

**This specific choice must be recorded in `DECISIONS.md` and witness-diffed against at least lv2-psx-reverb on static (non-modulated) inputs where behavior converges regardless of option chosen.**

---

## 5. Python Binding Layout

### 5.1 Repository Layout

Yes — `libspu94.so` and the Python package live in the same repo:

```
repo/
├── include/spu94.h
├── src/spu94_*.c
├── CMakeLists.txt              # builds libspu94.so / libspu94.a / libspu94.dylib
├── bindings/
│   └── python/
│       ├── pyproject.toml
│       ├── spu94/
│       │   ├── __init__.py     # ctypes loader, Python-friendly wrappers
│       │   ├── _ffi.py         # raw ctypes bindings, not for end-user
│       │   ├── constants.py    # auto-generated from spu94_reg_ids.h
│       │   ├── presets.py      # loads presets/*.json
│       │   └── py.typed
│       └── tests/              # see §6
├── presets/
│   ├── room.json
│   ├── studio_small.json
│   ├── studio_medium.json
│   ├── studio_large.json
│   ├── hall.json
│   ├── half_echo.json
│   ├── space_echo.json
│   ├── chaos_echo.json
│   ├── delay.json
│   └── off.json
├── cli/
│   └── spu94_cli.c             # thin CLI wrapping public API + dr_wav
└── tests/
    ├── c/                      # pure-C unit tests (no Python dependency)
    │   ├── test_fixed.c
    │   ├── test_buffer.c
    │   ├── test_registers.c
    │   └── test_resample.c
    └── fixtures/
        └── ...
```

### 5.2 numpy Exchange via ctypes

```python
import ctypes
import numpy as np

_lib = ctypes.CDLL("libspu94.so")

_lib.spu94_process.argtypes = [
    ctypes.c_void_p,                                    # state
    ctypes.POINTER(ctypes.c_int16),                     # in_l
    ctypes.POINTER(ctypes.c_int16),                     # in_r
    ctypes.POINTER(ctypes.c_int16),                     # out_l
    ctypes.POINTER(ctypes.c_int16),                     # out_r
    ctypes.c_size_t                                     # n
]
_lib.spu94_process.restype = None

def process(state, in_l: np.ndarray, in_r: np.ndarray):
    assert in_l.dtype == np.int16 and in_r.dtype == np.int16
    assert in_l.flags["C_CONTIGUOUS"] and in_r.flags["C_CONTIGUOUS"]
    assert in_l.shape == in_r.shape
    out_l = np.empty_like(in_l)
    out_r = np.empty_like(in_r)
    _lib.spu94_process(
        state,
        in_l.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        in_r.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        out_l.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        out_r.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        len(in_l)
    )
    return out_l, out_r
```

- Zero copy — ctypes passes numpy buffer pointers directly.
- No cffi, no pybind11: the ctypes surface is all we need and matches the project's minimal-tooling preference.

### 5.3 Register Constants in Python

Use `enum.IntEnum`, auto-generated from `spu94_reg_ids.h` at build time (a small Python script that parses the enum):

```python
from enum import IntEnum
class Reg(IntEnum):
    MBASE = 0
    DAPF1 = 1
    DAPF2 = 2
    VIIR = 3
    # ...
```

Benefits: tab-completion in REPLs, type-checked usage, integer-compatible with the ctypes call.

### 5.4 Preset Storage — JSON

JSON chosen because both C (via a minimal parser or preprocess-to-C step) and Python can consume it trivially. Format:

```json
{
  "name": "Hall",
  "work_size_bytes": 44512,
  "registers": {
    "MBASE": "0xEE00",
    "DAPF1": "0x01A5",
    "VIIR": "0x6400",
    ...
  }
}
```

Presets are checked in as authoritative test fixtures; the C side imports them via a generator script that emits `spu94_presets.c` at build time (so the C lib has no runtime JSON parser — important for MCU).

---

## 6. Testing Architecture

### 6.1 Organization

```
tests/
├── c/                          # pure C, compiled via CMake, no Python
│   ├── test_fixed.c            # Q15 math: mul, saturate, shift edge cases
│   ├── test_buffer.c           # BufferAddress arithmetic, wrap, mBASE-reset
│   ├── test_registers.c        # register_write validation, enum coverage
│   ├── test_resample.c         # 39-tap FIR: impulse response, DC passthrough
│   └── test_conformance_vIIR_bug.c  # the -0x8000 edge case
└── python/
    ├── test_topology.py        # full-pipeline sanity: silence→silence, DC→DC
    ├── test_presets.py         # all 10 presets load, process, have expected tail
    ├── test_modulation.py      # sine/sweep/random-walk each register; stable, bounded
    ├── test_bit_faithful.py    # golden-file diffs; regression detection
    ├── test_witness_diff.py    # witness diff against lv2-psx-reverb WAV outputs
    ├── conformance/            # one file per nocash section
    │   ├── test_same_reflection.py
    │   ├── test_diff_reflection.py
    │   ├── test_comb_early_echo.py
    │   ├── test_apf_late_reverb.py
    │   ├── test_buffer_wrap.py
    │   └── test_saturation.py
    ├── witnesses/              # WAV fixtures
    │   └── lv2-psx-reverb/
    │       ├── input_white_noise.wav
    │       ├── output_hall.wav
    │       ├── output_room.wav
    │       └── ... (per-preset)
    └── golden/                 # signed-off SPU-94 outputs
        ├── hall_white.wav
        ├── hall_white.sha256
        ├── room_impulse.wav
        ├── room_impulse.sha256
        └── ... (per-test)
```

### 6.2 Witness Fixtures

Generated once, signed off, checked in:
- **Input:** deterministic signals (impulse, white noise seeded, sine at 1 kHz, DC, silence) — about 10-20 short WAV files.
- **Output:** for each witness (lv2-psx-reverb) × each preset × each input = a WAV. Maybe ~100 files, each a few seconds; not huge. Git-LFS optional but not necessary.
- **Diff tolerance:** SPU-94 should *not* bit-match lv2-psx-reverb (it doesn't downsample to 22.05 kHz, and uses floats). Tolerance is envelope similarity + spectral RMSE under a threshold; tests define the threshold per-preset.

### 6.3 Golden Files

Produced once by SPU-94 itself and manually signed off. Subsequent runs must bit-match. Sidecar `.sha256` file for quick CI pass.

### 6.4 CI Pipeline

```
stage 1 — build C
  cmake -B build -DSPU94_BUILD_TESTS=ON
  cmake --build build
  ctest --test-dir build          # runs tests/c/*

stage 2 — build Python wrapper
  pip install -e bindings/python
  pytest bindings/python/tests/   # fast tests (topology, presets)

stage 3 — heavy tests
  pytest tests/python/ -m "modulation or witness or golden"

stage 4 — cross-compile smoke
  cmake --preset cortex-m7        # Daisy target
  cmake --build build-cortex-m7   # must compile libspu94.a
                                  # no runtime check, just prove portability
```

---

## 7. Future-Proofing for M2 / M3 / M4

### 7.1 M2 — ADPCM Input Stage

ADPCM decode produces int16 samples at 44.1 kHz — exactly the M1 `spu94_process` input type. **The M2 ADPCM codec is a separate library (`libspu94adpcm`) whose output feeds `spu94_process` unchanged.**

To make this trivial:
- M1's input path MUST accept int16 at 44.1 kHz (already does).
- M1 MUST expose a mix-bus-input variant or allow the caller to pre-mix multiple int16 streams before passing to process (caller-side mix; library doesn't need to grow).
- `spu94_clip.c` (hard clip on the mix bus) is structured so M2 can reuse it — export `spu94_hard_clip_int32_to_int16(int32_t)` as a public utility.

**No M1 API surface needs to change for M2.** ADPCM lives parallel to, not inside, M1.

### 7.2 M3 — DAC Reconstruction Output Stage

Same pattern: M3 is a post-processor on `spu94_process` output. The DAC stage is modeled as an int16-in / int16-out filter (or int16 → float32 for analog-stage modeling) and plugged in after `spu94_process`.

To make this trivial:
- M1's output path produces int16 at 44.1 kHz (already does).
- M1 MUST NOT assume anything about downstream filtering or rate conversion.
- A separate handle (`spu94_dac_state_t`) parallels the reverb handle in M3.

**No M1 API surface needs to change for M3.**

### 7.3 M4 — JUCE Plugin / Lever Layer

The JUCE plugin is C++, but it wraps the plain C core. Because the core is C with an opaque handle and extern "C" linkage, C++ wrapping is mechanical:

```cpp
class SPU94Plugin {
    std::vector<uint8_t> state_storage_;
    std::vector<int16_t> work_buffer_;
    spu94_state_t *core_;
public:
    void prepare(...) {
        state_storage_.resize(spu94_state_size());
        work_buffer_.resize(MAX_WORK_SIZE / 2);
        core_ = reinterpret_cast<spu94_state_t *>(state_storage_.data());
        spu94_init(core_, work_buffer_.data(), work_buffer_.size() * 2);
    }
    void process(juce::AudioBuffer<float> &buffer) {
        // convert float→int16, call spu94_process, convert back
    }
};
```

**To make this trivial, M1's C API MUST:**
- Use `extern "C"` guards in `spu94.h` (already planned).
- Not return C structs by value — only pointers to opaque types or scalar returns (already planned).
- Not use macros or inline functions in the public header — only declarations (planned).
- Provide enough granular control for M4's lever layer — specifically the per-register `write_reg` with `IMMEDIATE`/`LATCHED` scope hinting (Option C from §4).

### 7.4 M4 Lever Layer — What M1 Exposes To Make It Easy

M4 will implement named musical levers (Room Size, Pre-Delay, Damping, Width, etc.) that map to register sets with smoothing. For M4 to be implementable cleanly:

- M1's `spu94_write_reg` must be safe to call at host sample rate (already designed).
- M1 must expose introspection: `spu94_read_reg` to seed lever values from a preset.
- M1 must support atomic multi-register update: `spu94_load_preset` supplied already. Optionally a `spu94_write_reg_multi(state, ids[], values[], count)` that latches all changes to the next tick — useful for a lever that maps to 3-4 registers simultaneously.
- `LEVERS-CATALOG.md` (built during M1 implementation) tags each register with:
  - `LEVER_CANDIDATE: yes/no`
  - `MUSICAL_ROLE: damping/size/pre-delay/coloration/cross-bleed/in-gain/out-gain`
  - `MODULATION_COST: free (gain) / sample-quantized (delay) / catastrophic (mBASE)`
  - `SMOOTHING_STYLE: linear / exponential / stepped`
  - `SUGGESTED_CV_RANGE: ...`

This catalog is M1's concrete deliverable to M4.

### 7.5 Build Order (Module Dependencies)

Build-order implications for the roadmap:

```
spu94_fixed.c         ◄── NO DEPS         (Q15 math — foundation)
      │
      ▼
spu94_buffer.c        ◄── spu94_fixed     (BufferAddress math)
spu94_registers.c     ◄── spu94_fixed     (register write with validation)
      │  │
      │  ▼
      │  spu94_clip.c      ◄── spu94_fixed     (hard clip; independent of reverb network)
      │
      ▼
spu94_resample.c      ◄── spu94_fixed     (39-tap FIR)
      │
      ▼
spu94_reverb.c        ◄── spu94_buffer + spu94_registers + spu94_fixed
      │
      ▼
spu94_presets.c       ◄── (static data only; independent, built any time)
      │
      ▼
spu94_public.c        ◄── ALL OF THE ABOVE (dispatch + init + process loop)
      │
      ▼
bindings/python + cli/spu94_cli.c  ◄── spu94_public
```

**Suggested implementation order (suggested phase decomposition for roadmap):**

1. `spu94_fixed.c` + full tests for Q15 math and saturation. (Foundation — build wrong, everything is wrong.)
2. `spu94_buffer.c` + tests for tap addressing, wrap, `mBASE`. (Addressing bugs are the most subtle; lock them down before DSP.)
3. `spu94_registers.c` + tests for validation and the latch mechanism. (Gray-area policy committed here.)
4. `spu94_clip.c` + tests. (Independent, low risk; easy win.)
5. `spu94_resample.c` + tests for 39-tap FIR. (Independent of reverb algorithm; can be tested with known impulse response.)
6. `spu94_reverb.c` — the algorithm. (All foundations in place; bugs are in the algorithm or the spec interpretation, not the infra.)
7. `spu94_public.c` — glue. (Small file; integrates everything.)
8. `spu94_presets.c` — preset data + preset-loading integration test.
9. CLI + Python bindings. (External surface.)
10. Witness-diff harness + golden-file regressions. (Validation pass.)
11. Cross-compile smoke test to Cortex-M.

This ordering **maximizes parallelizable work after step 3** (clip, resample, presets can proceed independently), **puts the gray-area decisions in front** (steps 1-3), and **delays the algorithm** until all infrastructure is trustworthy.

---

## 8. Architectural Patterns Used

### Pattern 1 — Opaque Handle + Caller-Allocated Storage

Caller queries state size, allocates, passes pointer. Library owns the struct layout privately.

**When:** Real-time, no-heap C libraries (audio, DSP, embedded).
**Trade-off:** Extra init step vs. zero heap allocation + ABI-stable public header.

### Pattern 2 — Flat Register Array with Enum Index

One `uint16_t regs[N]` indexed by enum.

**When:** Register-file emulation, configurable DSP blocks with many parameters.
**Trade-off:** Slight type-safety loss (validation handles it) vs. trivial bulk ops, bindings, preset save/restore.

### Pattern 3 — Double-Buffered Register Latch

Writes land in `pending_regs`; applied atomically at block boundaries.

**When:** Real-time audio where mid-block parameter changes must be consistent across a block.
**Trade-off:** Adds one tick of modulation latency (22.7 µs here) vs. eliminates mid-block inconsistency.

### Pattern 4 — Split Rate Processing via Half-Band FIR

Host sample rate (44.1 kHz) ↔ internal DSP rate (22.05 kHz) via 39-tap half-band FIR on I/O.

**When:** Bit-faithful emulation of hardware that runs internal DSP at a lower rate than the host.
**Trade-off:** +39 sample latency per side, cost of FIR vs. preserving the exact frequency response of the original.

### Pattern 5 — Static Preset Data in `.rodata`

Presets as compile-time arrays, not runtime-parsed.

**When:** MCU/FPGA targets where flash is plentiful but RAM and runtime parsers are not.
**Trade-off:** Recompile to change presets vs. zero runtime parsing cost.

---

## 9. Anti-Patterns to Avoid

### Anti-Pattern 1 — Using floats in the core

**What people do:** Port the pseudocode to `float` because it's cleaner.
**Why wrong:** Loses bit-faithfulness. The character of the reverb is in the Q15 truncation and saturation. lv2-psx-reverb does this and explicitly documents the fidelity loss.
**Instead:** int16 storage, int32 compute, explicit Q15 helpers in `spu94_fixed.c`.

### Anti-Pattern 2 — Heap allocation inside the library

**What people do:** `malloc()` the work buffer inside `spu94_init`.
**Why wrong:** Blocks MCU port; blocks real-time safety; adds a failure path to the init.
**Instead:** Caller-allocated storage for both state and work buffer.

### Anti-Pattern 3 — Exposing the struct definition in the public header

**What people do:** Put `struct spu94_state { ... }` in `spu94.h` so callers can embed it on the stack.
**Why wrong:** Any future field added to the struct is a binary ABI break. Callers will assume layout details.
**Instead:** Opaque handle + `spu94_state_size()` query.

### Anti-Pattern 4 — Sample-by-sample process function

**What people do:** `int16_t spu94_process_sample(state, l_in, r_in)` returning one sample at a time.
**Why wrong:** JUCE, LV2, PortAudio, ALSA all work in blocks. Per-sample calls multiply call overhead and defeat compiler optimization.
**Instead:** Block-oriented `spu94_process(state, in_l, in_r, out_l, out_r, n)`.

### Anti-Pattern 5 — Parameter smoothing baked into the core

**What people do:** "We should smooth coefficient changes to avoid zipper noise!" — add a lerp to every register write.
**Why wrong:** The core's job is bit-faithfulness. Smoothing is a lever-layer concern. Mixing the two destroys the testability of either.
**Instead:** Core is bit-faithful (Option C per §4); M4 lever layer adds smoothing on top.

### Anti-Pattern 6 — Reading GPL emulator source as primary reference

**What people do:** "Let me peek at Mednafen to see what they do here."
**Why wrong:** Licensing posture. Final product loses MIT/Apache licensing flexibility.
**Instead:** Specs are the primary source. Witnesses are output-only diffs. Any necessary source consultation is logged in DECISIONS.md with scope statement.

---

## 10. Integration Points

### External Services / Dependencies

| Dependency | Integration | Scope | Notes |
|---|---|---|---|
| numpy | ctypes buffer-pointer passing | Python tests only | Not a core dep |
| dr_wav (single-header C) | WAV I/O in `cli/spu94_cli.c` | CLI only | Not in libspu94 |
| pytest | Test runner | Dev-only | — |
| CMake | Build system | Dev-only | — |
| JUCE | M4 plugin wrapper | M4 only | — |

### Internal Boundaries

| Boundary | Communication | Notes |
|---|---|---|
| spu94_public ↔ spu94_reverb | Direct C function call on private `struct spu94_state *` | Owns state access rules |
| spu94_reverb ↔ spu94_buffer | Inline or direct call; tap addressing is hot path | Consider `inline` attribute |
| spu94_reverb ↔ spu94_fixed | Inline Q15 ops | `static inline` in `spu94_math.h` |
| spu94_public ↔ caller (C / Python / CLI / JUCE) | Opaque handle + extern "C" API | This is the public contract |
| libspu94 ↔ bindings/python/spu94 | ctypes over SO | Must match `spu94.h` signatures |
| libspu94 ↔ cli/spu94_cli | Direct link to libspu94.a | CLI is a thin wrapper |

---

## 11. Scaling Considerations (DSP Context)

Not "users" — for a DSP library, scaling means signal/context growth.

| Scale | Architecture Adjustments |
|---|---|
| 1 instance, stereo | Current design handles trivially |
| N parallel instances | N state structs, N work buffers. Core is reentrant and has no globals. Trivial. |
| Per-voice instance (e.g., 24 voices each with own reverb) | Same N-instance pattern. Memory: N × (state + work buffer). Chaos preset is 48 KiB × 24 = 1.15 MiB — fine on desktop, tight on Cortex-M4. |
| FPGA HLS | Core has no pointer chasing (work_buf access is offset math) and no recursion. HLS-friendly. The 39-tap FIR maps to a DSP slice. |
| Real-time audio thread | No allocations, no locks, no syscalls, no variable-latency ops — check. Worst-case path is deterministic by construction. |

### First Bottleneck

**Cache behavior on the work buffer.** The work buffer is accessed at up to ~12 different offsets per 22050 Hz tick; with 48 KiB buffers on a Cortex-M4 with 16 KiB D-cache, this can thrash. Mitigation: buffer layout is already cache-line-friendly (contiguous int16 array); no further action unless measurement shows a problem. On FPGA this is a non-issue (BRAM).

---

## Sources

- nocash psx-spx via [psx-spx.consoledev.net — Sound Processing Unit (SPU)](https://psx-spx.consoledev.net/soundprocessingunitspu/) — verbatim source for §1.1 reverb formula, §1.4 39-tap FIR coefficients, §1.5 buffer addressing, §1.6 saturation rule and vIIR bug, §1.7 register map. **HIGH confidence** (primary spec, cited verbatim).
- [problemkaputt.de — PSX specs root](https://problemkaputt.de/psx-spx.htm) — canonical mirror cross-reference, **HIGH confidence**.
- [ipatix/lv2-psx-reverb README](https://github.com/ipatix/lv2-psx-reverb) — explicit statement that lv2-psx-reverb does NOT downsample to 22050 Hz. **MEDIUM** (witness-behavior only; source not read).
- [jsgroth's blog — PlayStation: The SPU, Part 1 (ADPCM)](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) — independent corroboration of general SPU architecture. **MEDIUM**.
- [jsgroth's blog — PlayStation: The SPU, Part 4 (Everything Else)](https://jsgroth.dev/blog/posts/ps1-spu-part-4/) — independent corroboration of reverb half-rate characteristic. **MEDIUM**.
- C99 / C11 standard — opaque-handle idiom is standard practice for no-heap C libraries.

---

*Architecture research for: SPU-94, Milestone 1 (reverb network + hard clip)*
*Researched: 2026-04-18*
