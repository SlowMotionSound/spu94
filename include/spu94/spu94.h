#ifndef SPU94_H
#define SPU94_H

#ifdef __cplusplus
extern "C" {
#endif

/* SPU-94 public API umbrella header.
 * Phase 1: Q15 helper surface (spu94_q15.h).
 * Phase 2 (Plan 01): opaque spu94_state type, size/alignment macros,
 *                    spu94_result_t enum, lifecycle API (init/reset/destroy).
 * Later Phase 2 plans: register I/O, write-timing policy, buffer arithmetic.
 * Later phases: spu94_tick(), spu94_process(), presets, Python bindings.
 *
 * C99 freestanding conformance (API-07, API-09):
 *   This header compiles clean under -std=c99 -pedantic -Werror and through
 *   an extern "C" C++ consumer. Only <stdint.h>, <stddef.h>, <limits.h>,
 *   <stdbool.h> (freestanding C99) and <stdalign.h> (freestanding C11) may
 *   appear here; no <stdlib.h>, <string.h>, or <stdio.h>.
 *
 * Thread-safety:
 *   A spu94_state is NOT thread-safe. Concurrent access from multiple
 *   threads requires external synchronization. The library performs no
 *   locking in the hot path (PROJECT.md constraint).
 */

#include <stddef.h>
#include <stdint.h>
#include <spu94/spu94_q15.h>

/* spu94_result_t MUST be declared before <spu94/spu94_registers.h> is
 * included: the engine-layer setter declarations in spu94_registers.h
 * (Plan 03) return spu94_result_t. Reordering this include makes the
 * type visible to those signatures without forcing spu94_registers.h
 * to take a transitive dependency on the umbrella header. */

/* ------------------------------------------------------------------------- */
/* Result codes (D-07)                                                       */
/* ------------------------------------------------------------------------- */

/* Return-code enum for the register-write API (Plan 03 onward). Declared in
 * Plan 01 so downstream plans can reference it without header churn.
 *
 * Contract:
 *   - SPU94_OK == 0; callers may write `if (spu94_set_* (...))` to detect any
 *     non-OK outcome.
 *   - New codes are APPEND-ONLY. Existing names and numeric values are stable
 *     across minor-version bumps. Callers that ignore the return value
 *     continue to work when new codes are added.
 *   - Data behavior is bit-faithful regardless of the return code (D-08):
 *     clamping/wrapping happens per PS1 hardware; the code describes it.
 */
typedef enum {
    SPU94_OK                 = 0,
    SPU94_CLAMPED            = 1, /* value was saturated to fit the register */
    SPU94_UNKNOWN_REG        = 2, /* register id out of range — no-op write   */
    SPU94_TYPE_MISMATCH      = 3, /* signed/unsigned accessor mismatch        */
    /* Appended 2026-04-24 (ADR-0022): mutation-time argument validation.
     * Existing codes 0..3 remain stable in both name and numeric value. */
    SPU94_INVALID_STATE      = 4, /* state == NULL on a mutation call          */
    SPU94_WORK_BUF_TOO_SMALL = 5, /* caller's work_buf is too small for preset */
    SPU94_INVALID_ARG        = 6  /* argument out of range (e.g., preset id)   */
} spu94_result_t;

#include <spu94/spu94_registers.h>
#include <spu94/spu94_register_facade.h>

/* ------------------------------------------------------------------------- */
/* State sizing and alignment                                                */
/* ------------------------------------------------------------------------- */

/* Upper bound for static/stack-allocated spu94_state storage (MCU / embedded
 * callers). The internal struct is guarded by
 *   _Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX, ...)
 * in the internal header that owns the struct definition
 * (src/spu94/spu94_state_internal.h), so growing the private struct past this
 * bound fails the build. Chosen generously (RESEARCH.md A7) — modest future
 * field additions in Plans 02-05 do not force a bump.
 *
 * Typical caller idiom (D-12):
 *   alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
 *   spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
 *                               work_buf, work_buf_size);
 */
#define SPU94_STATE_SIZE_MAX  16384u

/* Alignment requirement for the caller-provided state buffer. 16 bytes covers
 * int64_t, future SIMD, and every target toolchain in the ADR-0001 matrix
 * (MCU 8-byte alignment + headroom). See 02-RESEARCH.md Pitfall 2. */
#define SPU94_STATE_ALIGN_MAX 16u

/* ------------------------------------------------------------------------- */
/* Opaque state handle (D-12)                                                */
/* ------------------------------------------------------------------------- */

/* The opaque spu94_state type is forward-declared in <spu94/spu94_registers.h>
 * (included above). Callers may only hold spu94_state * (not by value); the
 * real struct is private to src/spu94/spu94_state.c. We do NOT re-typedef it
 * here — duplicate typedefs of the same name break -std=c99 -pedantic
 * (allowed in C11+ but not C99). API-07 requires this header to compile under
 * C99-pedantic, so the typedef has a single home. */

/* spu94_result_t is declared above (before <spu94/spu94_registers.h>) so the
 * Plan 03 engine-layer setter signatures can refer to it without circular
 * includes. The duplicate typedef previously lived here in Plan 01; moved
 * upward in Plan 03 with no API change. */

/* ------------------------------------------------------------------------- */
/* Lifecycle API (D-14)                                                      */
/* ------------------------------------------------------------------------- */

/* Return the exact number of bytes the caller must provide for state storage.
 * Deterministic: repeated calls in the same process return the same value.
 * Guaranteed to be <= SPU94_STATE_SIZE_MAX. */
size_t        spu94_state_size(void);

/* Initialize a spu94_state in caller-allocated memory.
 *
 * Arguments:
 *   state_buf       Caller-owned state storage, at least SPU94_STATE_SIZE_MAX
 *                   bytes, aligned to SPU94_STATE_ALIGN_MAX.
 *   state_buf_size  Usable size of state_buf. Must be >= spu94_state_size().
 *   work_buf        Caller-owned reverb work buffer (D-13). May be NULL if
 *                   work_buf_size == 0 (zero-sized "don't-care" configuration:
 *                   no tick/advance will be issued).
 *   work_buf_size   Size of work_buf in bytes.
 *
 * Returns:
 *   Non-NULL spu94_state* on success — identical to state_buf cast to the
 *     opaque type. Internal fields are zeroed; BufferAddress = mBASE = 0.
 *   NULL if any of the following hold (the library does NOT mutate caller
 *     memory on failure):
 *       state_buf == NULL
 *       state_buf_size < spu94_state_size()
 *       state_buf is not aligned to SPU94_STATE_ALIGN_MAX
 *       work_buf == NULL && work_buf_size > 0  (caller bug)
 *
 * This function performs NO heap allocation. */
spu94_state  *spu94_init(void *state_buf, size_t state_buf_size,
                         void *work_buf,  size_t work_buf_size);

/* Restore the state to post-init invariants without re-running init.
 * Per the state-allocation pattern (02-RESEARCH.md § State-Allocation Pattern):
 * zeros all internal registers, clears pending_mask, restores BufferAddress
 * to mBASE (= 0), and zeroes the caller's work buffer. The work-buffer
 * POINTER is preserved. Safe to call with state == NULL (no-op). */
void          spu94_reset(spu94_state *state);

/* Zero the state's internal bytes (security hygiene per T-02-03). Does NOT
 * release memory — the caller owns state_buf and work_buf. Safe to call
 * with state == NULL (no-op). After spu94_destroy, the state pointer is
 * invalid until the caller re-runs spu94_init on the underlying buffer. */
void          spu94_destroy(spu94_state *state);

/* ------------------------------------------------------------------------- */
/* Per-tick processing entry point (D-19, ADR-0004)                          */
/* ------------------------------------------------------------------------- */

/* The atomic per-22.05 kHz-tick processing entry point. Phase 3 implements
 * the reverb algorithm inside this function. Phase 5's spu94_process is
 * built as a loop calling spu94_tick per stereo tick.
 *
 * Plan 02 ships a no-op stub body. The observability contract is:
 * between two spu94_tick calls, any public accessor (snapshot_registers,
 * buffer_address, etc.) observes a consistent instantaneous state.
 *
 * Null-safe: spu94_tick(NULL) is a no-op. */
void          spu94_tick(spu94_state *state);

/* ------------------------------------------------------------------------- */
/* BufferAddress observability (CORE-03, D-23, ADR-0006)                     */
/* ------------------------------------------------------------------------- */

/* Return the current BufferAddress — byte offset into the reverb work
 * buffer that the wrap-formula advance reads/writes (per ADR-0006). The
 * value is updated once per spu94_tick() (advance), and additionally
 * snapped to the new mBASE on any successful write to mBASE
 * (snap-on-write). NULL state returns 0. The accessor is read-only;
 * mutation happens only through spu94_tick (advance) or via writing
 * mBASE (snap). */
uint32_t      spu94_get_buffer_address(const spu94_state *state);

/* ------------------------------------------------------------------------- */
/* FIR chain latency contract (CORE-06/07, Phase 4, D-09)                    */
/* ------------------------------------------------------------------------- */

/* Total round-trip FIR group delay at the 44.1 kHz reference rate.
 *   decimator:   19 samples at 44.1 kHz (impulse reaches delay[19]).
 *   interpolator: 19 samples at 22.05 kHz (its input clock) = 38 samples
 *                 at 44.1 kHz (its output clock).
 *   TOTAL:       19 + 38 = 57 samples at 44.1 kHz input-to-output.
 *
 * Empirical chain-impulse peak lies at 44.1 kHz output t = 57 (phase-0
 * pair) / t = 59 (phase-1 pair) — tied because the peak 22.05 kHz value
 * from the decimator centers on retained-index 9-10 and the interpolator
 * center-tap passthrough emits on phase-1. The nominal reported value
 * SPU94_LATENCY_SAMPLES = 58u sits at the midpoint of the tied peaks;
 * the ±1-sample tolerance in test_fir_chain_latency accommodates both.
 *
 * This value is fixed by the 39-tap linear-phase half-band FIR structure;
 * it is not a runtime parameter. Compile-time constant for static array
 * sizing + preset-delay math. Phase 6 (Python bindings) exposes via the
 * spu94_get_latency_samples() accessor; C consumers may use either.
 * See docs/DECISIONS.md ADR-Phase-4-H (Plan 04) for the corrected
 * rationale — the value was 38u in the original Phase 4 research; Plan
 * 03's chain-level impulse test exposed the miscounted interpolator
 * contribution and it was corrected to 58u. */
#define SPU94_LATENCY_SAMPLES 58u

/* Runtime accessor for latency in samples at 44.1 kHz. Deterministic;
 * returns SPU94_LATENCY_SAMPLES. LTO-eliminable at C consumer call sites. */
uint32_t      spu94_get_latency_samples(void);

/* -----------------------------------------------------------------------
 * ADPCM coloration stage (M2 Phase 2, ADPCM-INT-01..06)
 *
 * When enabled, spu94_process routes input PCM through ADPCM
 * encode+decode before the FIR decimator, introducing PS1-characteristic
 * quantization noise and filter ringing. Off by default. Adds 28 samples
 * of latency (one ADPCM block) when enabled.
 * ----------------------------------------------------------------------- */

/** Enable/disable ADPCM coloration upstream of the FIR decimator.
 *  Off by default. NULL state is a no-op. */
void     spu94_set_adpcm_enabled(spu94_state *state, int enabled);

/** Query ADPCM coloration state. NULL state returns 0. */
int      spu94_get_adpcm_enabled(const spu94_state *state);

/** Total processing latency in samples at 44.1 kHz.
 *  Base: SPU94_LATENCY_SAMPLES (58).
 *  + 28 when ADPCM enabled.
 *  + 15 when DAC FIR enabled in v1.3 mode (true oversample).
 *  + 35 when DAC FIR enabled in v1.2 mode (approximate).
 *  NULL state returns 58. */
uint32_t spu94_get_total_latency_samples(const spu94_state *state);

/* -----------------------------------------------------------------------
 * Send/return mixer controls (Phase 7, D-01 through D-06)
 *
 * Six Q15 fader/send values control the mixer architecture:
 *   input_gain:    scales input before bus split
 *   dry_fader:     dry bus level at master mixer
 *   patina_fader:  patina (ADPCM) bus level at master mixer
 *   dry_send:      dry bus contribution to reverb input
 *   patina_send:   patina bus contribution to reverb input
 *   reverb_fader:  reverb return level at master mixer
 *
 * All values are Q15 int16 in range [0x0000, 0x7FFF].
 * Default: all zero (silence). Hosts must set before expecting audio.
 * No parameter smoothing -- values land immediately (D-06).
 * ----------------------------------------------------------------------- */

void     spu94_set_input_gain(spu94_state *state, int16_t gain);
int16_t  spu94_get_input_gain(const spu94_state *state);

void     spu94_set_dry_fader(spu94_state *state, int16_t level);
int16_t  spu94_get_dry_fader(const spu94_state *state);

void     spu94_set_patina_fader(spu94_state *state, int16_t level);
int16_t  spu94_get_patina_fader(const spu94_state *state);

void     spu94_set_dry_send(spu94_state *state, int16_t level);
int16_t  spu94_get_dry_send(const spu94_state *state);

void     spu94_set_patina_send(spu94_state *state, int16_t level);
int16_t  spu94_get_patina_send(const spu94_state *state);

void     spu94_set_reverb_fader(spu94_state *state, int16_t level);
int16_t  spu94_get_reverb_fader(const spu94_state *state);

/* -----------------------------------------------------------------------
 * Latency compensation (Phase 7, D-07, D-08)
 *
 * When ADPCM is enabled, it introduces a 28-sample block delay.
 * Latency compensation adds a matching 28-sample delay to the dry bus
 * so both arrive at the master mixer time-aligned.
 * ON by default (D-07). OFF creates intentional comb filtering (creative use).
 * Only active when ADPCM is also enabled.
 * ----------------------------------------------------------------------- */

void     spu94_set_latency_comp(spu94_state *state, int enabled);
int      spu94_get_latency_comp(const spu94_state *state);

/* -----------------------------------------------------------------------
 * DAC coloration section (Phase 7, D-09 through D-12)
 *
 * Master toggle + two independent sub-toggles.
 * When master is off, no DAC processing runs.
 * When master is on, FIR and noise each have independent sub-toggles.
 * All three on = faithful PS1 DAC behavior (D-11).
 * Default: all off.
 * ----------------------------------------------------------------------- */

void     spu94_set_dac_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_enabled(const spu94_state *state);

void     spu94_set_dac_fir_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_fir_enabled(const spu94_state *state);

void     spu94_set_dac_noise_enabled(spu94_state *state, int enabled);
int      spu94_get_dac_noise_enabled(const spu94_state *state);

/** Select DAC processing mode. 0 = v1.2 approximate (single-rate FIR),
 *  1 = v1.3 true 8x oversampling (352.8kHz cascade). Default: 1.
 *  When v1.3 is active and both FIR+noise are enabled, noise is injected
 *  at 352.8kHz inside the cascade for spectrally shaped noise floor. */
void     spu94_set_dac_true_oversample(spu94_state *state, int enabled);
int      spu94_get_dac_true_oversample(const spu94_state *state);

/* ------------------------------------------------------------------------- */
/* Public block-based audio entry point (Phase 5, D-01, D-02, D-03, D-04)    */
/* ------------------------------------------------------------------------- */

/* Process a block of 44.1 kHz int16 stereo samples. Planar buffers (D-01):
 * L_in/R_in and L_out/R_out are independent int16 arrays of num_samples
 * length each. Any block size num_samples >= 1 is legal (D-03); num_samples
 * == 0 is a safe no-op. In-place processing is allowed (D-04): L_out == L_in
 * and R_out == R_in are both valid (sample-at-a-time loop is alias-safe).
 * NULL state is a no-op. NULL L_in or R_in substitutes zero for that
 * channel's input samples (convenience for mono-sourced callers). NULL
 * L_out or R_out suppresses the corresponding output writes.
 *
 * Internal rate (22.05 kHz) and FIR boundary conversion are hidden -- the
 * caller sees only 44.1 kHz int16 stereo in, 44.1 kHz int16 stereo out,
 * with a round-trip group delay of SPU94_LATENCY_SAMPLES samples (see
 * Phase 4 contract). Real-time safe: no heap, no locks, no syscalls
 * (verified by Phase 5 Plan 04's tests/rt_safety/ CI gates). */
void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples);

/* Drain trailing reverb tail by feeding internal silence (D-02). Semantics
 * are identical to spu94_process with NULL L_in/R_in -- same block-loop,
 * same per-sample spu94_fir_chain_step. Use this after the input stream
 * ends to capture the decaying reverb tail in the output buffer. The
 * caller chooses how many samples of tail to drain (typical: a few
 * seconds of audio at 44.1 kHz = 44100 * N samples). NULL state is a
 * no-op; num_samples == 0 is a no-op. */
void spu94_flush(spu94_state *state,
                 int16_t *L_out, int16_t *R_out,
                 uint32_t num_samples);

/* ------------------------------------------------------------------------- */
/* Factory preset surface (Phase 5, D-06)                                    */
/* ------------------------------------------------------------------------- */

/* The ten PS1 SPU factory reverb preset identifiers. Numeric values are
 * canonical per Sony LIBSND (BIB-013):
 *   0 Off, 1 Room, 2 Studio A, 3 Studio B, 4 Studio C, 5 Hall,
 *   6 Half Echo, 7 Space Echo, 8 Echo, 9 Delay.
 * SPU94_PRESET__COUNT = 10 is the sentinel (iteration bound).
 * Numeric stability: these values are ABI; never renumber. */
typedef enum {
    SPU94_PRESET_OFF        = 0,
    SPU94_PRESET_ROOM       = 1,
    SPU94_PRESET_STUDIO_A   = 2,
    SPU94_PRESET_STUDIO_B   = 3,
    SPU94_PRESET_STUDIO_C   = 4,
    SPU94_PRESET_HALL       = 5,
    SPU94_PRESET_HALF_ECHO  = 6,
    SPU94_PRESET_SPACE_ECHO = 7,
    SPU94_PRESET_ECHO       = 8,
    SPU94_PRESET_DELAY      = 9,
    SPU94_PRESET_INIT  = 10,
    SPU94_PRESET__COUNT     = 11
} spu94_preset_id_t;

/* A factory preset: human-readable name + 35-register value vector.
 * regs[] is indexed by spu94_reg_t (see spu94_registers.h). Values sourced
 * via the three-source audit (BIB-011 nocash + BIB-012 hitmen; BIB-013
 * Sony SDK corroborates preset-ID ordering). Storage: .rodata. */
typedef struct {
    const char *name;
    int16_t     regs[SPU94_REG__COUNT];
} spu94_preset_t;

/* The factory preset table. Indexed by spu94_preset_id_t. Read-only. */
extern const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT];

/* ------------------------------------------------------------------------- */
/* Work-buffer sizing contract (ADR-0022)                                    */
/* ------------------------------------------------------------------------- */

/* SPU94_WORK_BUF_MAX_BYTES — guaranteed to fit every factory preset.
 *
 * This is the PS1's full SPU RAM size (512 KiB). Every factory preset's
 * delay-line access pattern fits within it. Callers that don't know in
 * advance which preset(s) they'll load can size once against this constant
 * and forget about it:
 *
 *     unsigned char work[SPU94_WORK_BUF_MAX_BYTES];
 *     spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
 *                                 work, SPU94_WORK_BUF_MAX_BYTES);
 *     spu94_load_preset(s, SPU94_PRESET_HALL);   // always safe
 *
 * For embedded callers with tighter memory budgets, use
 * spu94_preset_min_work_buf_size(id) to get the exact byte count required
 * for a specific preset. */
#define SPU94_WORK_BUF_MAX_BYTES 0x80000u

/* Return the minimum work_buf_size (bytes) required to safely run
 * `spu94_load_preset(state, id)` followed by `spu94_process`. The value is
 * a conservative upper bound on the highest byte offset the reverb network
 * will touch while this preset is loaded: it scans the preset's address-
 * type (u16) register values and returns (max_halfword_value + 1) * 2.
 *
 * For id >= SPU94_PRESET__COUNT returns 0.
 *
 * Deterministic: repeated calls with the same id return the same value.
 * O(SPU94_REG__COUNT) on every call; callers wanting O(1) may cache. */
size_t spu94_preset_min_work_buf_size(spu94_preset_id_t id);

/* ------------------------------------------------------------------------- */
/* Observable error counters (ADR-0023, M1 close-out Step 4)                 */
/* ------------------------------------------------------------------------- */

/* A snapshot of the internal error counters. Returned by value from
 * spu94_get_error_counters. All fields are monotonically non-decreasing
 * across the lifetime of a single spu94_state; spu94_reset zeros them.
 *
 * Field semantics:
 *
 *   oob_tap_count -- reverb-body halfword accesses (read or write) whose
 *     computed byte offset lay outside [0, work_buf_size). The reverb
 *     network fails safe on such accesses (read returns 0; write is
 *     discarded), so audio is bit-deterministic even when the counter is
 *     non-zero. Callers treat oob_tap_count > 0 as a *configuration* red
 *     flag: it means the caller supplied a work buffer smaller than the
 *     loaded preset requires. Post-ADR-0022, spu94_load_preset rejects
 *     undersized buffers up front; oob_tap_count therefore remains 0
 *     for any state that was loaded via the public preset loader. The
 *     counter remains useful for callers that hand-write m-prefix or
 *     d-prefix registers directly (e.g., the M4 plugin modulation path).
 *
 * Append-only: new counters may be added at the END of this struct in
 * future releases. ABI bump is allowed; numeric stability on existing
 * fields is guaranteed. */
typedef struct {
    uint64_t oob_tap_count;
} spu94_error_counters_t;

/* Snapshot the error counters. NULL state returns a zeroed struct
 * (matches the read-only-observability convention used by
 * spu94_get_buffer_address). Deterministic: repeated calls between
 * ticks return the same value.
 *
 * Thread-safety: the counter is incremented from inside spu94_tick
 * (reverb body); reading it concurrently with a tick is a data race.
 * Callers in the M1 core-library use case are expected to read between
 * ticks, which has no race. */
spu94_error_counters_t spu94_get_error_counters(const spu94_state *state);

/* Atomically load one of the 10 factory presets into `state` (API-05, D-08).
 * Iterates all 35 registers via the Phase 2 engine-layer setters; the D-04
 * split write policy is honored automatically:
 *   - v-prefix gain registers and mBASE (IMMEDIATE policy) become active
 *     immediately.
 *   - d-prefix and m-prefix address/delay registers (TICK_LATCHED) stage
 *     into the pending slot and commit at the next spu94_tick.
 * Callers seeing a "half-applied" window (new v-prefix gains, old d-prefix
 * and m-prefix delays) for one tick (~45 us at 22.05 kHz) is inaudible and
 * accepted as the D-08 contract.
 *
 * Returns (contract tightened 2026-04-24 per ADR-0022 — caller code gets
 * actionable error signals instead of silent partial loads):
 *   SPU94_OK                  on success (state updated atomically)
 *   SPU94_INVALID_STATE       if state == NULL (state NOT mutated)
 *   SPU94_INVALID_ARG         if id >= SPU94_PRESET__COUNT (state NOT mutated)
 *   SPU94_WORK_BUF_TOO_SMALL  if state->work_buf_size is below
 *                             spu94_preset_min_work_buf_size(id)
 *                             (state NOT mutated — caller may retry with
 *                              a larger work_buf after spu94_init again)
 *
 * Side effect: mBASE being IMMEDIATE fires spu94_mbase_on_write (snap-on-
 * write per ADR-0006) if the preset's mBASE differs from the current value.
 *
 * Migration note: prior to ADR-0022, this function returned SPU94_OK on NULL
 * state (lifecycle-null-safe convention) and SPU94_UNKNOWN_REG on out-of-range
 * id. Callers that branched on `if (rc != SPU94_OK)` continue to work. Callers
 * that keyed specifically on SPU94_UNKNOWN_REG must switch to SPU94_INVALID_ARG
 * for the out-of-range id case. */
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id);

/* ------------------------------------------------------------------------- */
/* User-preset serialization (Phase 13, PRE-01..PRE-05)                      */
/* ------------------------------------------------------------------------- */

/* Upper bound on spu94_preset_save output. Generous: worst-case ~1400 bytes;
 * 4096 leaves room for future field additions without a version bump.         */
#define SPU94_PRESET_BUF_SIZE 4096u

/* Serialize the full engine state (35 registers + 6 mixer faders +
 * latency_comp toggle + 4 DAC toggles = 46 fields) to a human-readable
 * INI-style key=value text buffer.
 *
 * name / description may be NULL (written as empty strings).
 * Name is capped at 64 characters; description at 256. Excess is truncated.
 *
 * Returns bytes written (>= 0, excluding null terminator) on success.
 * Returns -1 if state, buf are NULL, or buf_size is 0.
 * Returns -2 if buf_size is too small to hold the complete output. */
int spu94_preset_save(const spu94_state *state,
                      const char *name,
                      const char *description,
                      char *buf, size_t buf_size);

/* Deserialize a key=value text buffer and restore engine state.
 * Missing keys retain the engine's current value (D-08).
 * Unknown keys are silently ignored (D-09).
 *
 * Returns SPU94_OK on success.
 * Returns SPU94_INVALID_STATE if state is NULL.
 * Returns SPU94_INVALID_ARG if buf is NULL or buf_len is 0. */
spu94_result_t spu94_preset_load(spu94_state *state,
                                 const char *buf, size_t buf_len);

/* ------------------------------------------------------------------------- */
/* Preset interpolation engine (Phase 16, INTERP-01..05)                     */
/* ------------------------------------------------------------------------- */

/* Number of waypoints in the morph continuum. The order is perceptual
 * (confirmed by ear), not the spu94_preset_id_t enum order:
 *   0: Half Echo, 1: Room, 2: Studio A, 3: Studio B, 4: Studio C,
 *   5: Hall, 6: Space Echo, 7: Echo, 8: Delay.                              */
#define SPU94_INTERP_WAYPOINT_COUNT 9

/* Number of user-programmable waypoint slots. Slots sit at the midpoints
 * between Sony's 9 anchors -- slot k occupies morph position (2k+1)/16. */
#define SPU94_INTERP_USER_SLOT_COUNT 8

/* Install register values into user slot `slot` (0..7) and mark it filled.
 * The morph engine immediately treats position (2*slot+1)/16 as a waypoint;
 * subsequent spu94_interp_set_morph calls interpolate through it. NULL state,
 * NULL regs, or out-of-range slot index are no-ops. */
void spu94_interp_set_user_slot(spu94_state *state, int slot,
                                const int16_t regs[SPU94_REG__COUNT]);

/* Mark user slot `slot` empty. The morph engine reverts to interpolating
 * straight across that midpoint as if the slot were never filled. NULL state
 * or out-of-range index are no-ops. */
void spu94_interp_clear_user_slot(spu94_state *state, int slot);

/* Returns 1 if slot is filled, 0 otherwise (or on NULL/out-of-range). */
int  spu94_interp_user_slot_is_filled(const spu94_state *state, int slot);

/* Copy slot contents into out[]. Slot need not be filled (returns the stored
 * bytes, which are zero for never-filled slots). NULL inputs / out-of-range
 * slot are no-ops; out[] is unchanged in that case. */
void spu94_interp_get_user_slot(const spu94_state *state, int slot,
                                int16_t out[SPU94_REG__COUNT]);

/* Set the morph position and write interpolated register values to state.
 *
 * position: 0.0 = first waypoint (Half Echo), 1.0 = last (Delay).
 *           Clamped to [0.0, 1.0]. Values outside this range are safe.
 *
 * At each of the 9 waypoint positions (0/8, 1/8, ... 8/8), the written
 * registers are bit-identical to the corresponding Sony factory preset
 * (INTERP-04 -- no rounding drift).
 *
 * Between waypoints, all 30 active registers are linearly interpolated
 * (INTERP-02). The 5 fixed registers are always written as their constant
 * values: vLOUT/vROUT = 0x7FFF, vLIN/vRIN = 0x8000, mBASE = 0x0000
 * (INTERP-03).
 *
 * Signed registers (v-prefix coefficients) interpolate in their signed
 * int16 range (INTERP-05 -- no unsigned wraparound).
 *
 * Writes use the engine-layer setters (spu94_set_reg_i16/u16), so the
 * split write-timing policy (ADR-0005) is honored automatically:
 * v-prefix gains take effect immediately, d/m-prefix addresses stage
 * to pending and commit at the next spu94_tick.
 *
 * NULL state is a no-op. */
void spu94_interp_set_morph(spu94_state *state, float position);

/* Per-sample register slewing (Phase 18).
 * Sets target register values and arms the Bresenham slew engine.
 * spu94_process steps each register by ±1 per sample, proportionally
 * distributed so all registers converge simultaneously (no stereo skew).
 * Pass the output of spu94_snapshot_registers from a scratch state. */
void spu94_set_slew_targets(spu94_state *state,
                            const int16_t targets[SPU94_REG__COUNT]);

/* Returns 1 while any register has not yet reached its slew target. */
int  spu94_is_slewing(const spu94_state *state);

/* Disable slewing and leave registers at their current values.
 * Use before preset loads or resets that should apply instantly. */
void spu94_slew_cancel(spu94_state *state);

/* Override the in-flight slew duration set by spu94_set_slew_targets.
 * Recomputes per-tick increments so all registers still converge
 * simultaneously at the new sample budget. Lets the caller decouple slew
 * rate from morph magnitude (e.g. a "Glide" knob). No-op if no slew is
 * active; samples < 1 is clamped to 1. NULL state is a no-op. */
void spu94_set_slew_duration(spu94_state *state, int32_t samples);

/* Morph Grit. Controls how the reverb body reads from the work buffer
 * during slewed transitions.
 *
 *   SPU94_GRIT_INT   (0, default): all reads integer. Faithful to PS1
 *     hardware -- no fractional addressing exists in the original
 *     silicon. Halfword stepping during morph excites the comb network
 *     and produces the "alive" character that motivates this project.
 *   SPU94_GRIT_FRACT (1): all reads fractional (linear interpolation
 *     between adjacent halfwords). Smooths morph transitions; the
 *     +-0.5 halfword feedback-loop mismatch also adds a textured patina.
 *
 * Out-of-range values are clamped to SPU94_GRIT_INT. NULL state is
 * a no-op. */
#define SPU94_GRIT_INT   0
#define SPU94_GRIT_FRACT 1

void spu94_set_morph_grit(spu94_state *state, int grit);

/* Returns the current Morph Grit (SPU94_GRIT_INT or SPU94_GRIT_FRACT). */
int  spu94_get_morph_grit(const spu94_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_H */
