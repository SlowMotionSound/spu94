# Pitfalls Research — SPU-94

**Domain:** Bit-faithful reimplementation of PS1 SPU reverb (fixed-point DSP, C library + Python bindings, mid-stream-modulatable)
**Researched:** 2026-04-18
**Confidence:** MEDIUM-HIGH (primary facts from nocash/psx-spx; implementation pitfalls from comparable projects' public artifacts; legal commentary from secondary sources — verify with counsel before final license/naming decisions)

---

## Orientation

Pitfalls are grouped by the eight categories in the research brief. Each pitfall specifies: **What goes wrong**, **Why it happens**, **Severity** (catastrophic / significant / nuisance), **Warning signs**, **Prevention**, **M1 phase mapping**, and **Source**.

Phase labels below reference the M1 requirement bullets in `.planning/PROJECT.md`. M1 has no formally numbered phases yet — the labels below are provisional groupings the orchestrator can collapse into phases during roadmap construction:

- **P-REG** — implement 24 reverb registers with documented semantics
- **P-FIXPOINT** — implement fixed-point arithmetic + hard clip
- **P-NETWORK** — implement comb + all-pass network + work buffer
- **P-API** — design the public C API, including mid-stream writes
- **P-MODTEST** — build the modulation test harness
- **P-PYBIND** — ship Python bindings via ctypes
- **P-CLI** — ship `spu94` CLI + golden-file harness
- **P-CROSS** — cross-compile smoke test to Daisy / Cortex-M
- **P-WITNESS** — lv2-psx-reverb output-only witness diff harness
- **P-DECISIONS** — maintain `DECISIONS.md`
- **P-LEVERS** — maintain `docs/LEVERS-CATALOG.md`
- **P-PRESETS** — ship 10 factory presets as fixtures

---

## Category 1 — PS1 SPU Reverb Algorithmic Pitfalls (The Actual DSP)

### 1.1 The vIIR = -8000h hardware anomaly

**Severity:** Significant (catastrophic if aiming for bit-exact and not handled)

**What goes wrong:** vIIR is documented as operating only in the range -7FFFh..+7FFFh. When written with -8000h (0x8000 interpreted as a signed int16 — the most-negative value), the multiplication step is performed correctly, but the final value written to memory gets negated. An implementation that treats -8000h as a normal coefficient will produce outputs that diverge from hardware and from any witness that models this quirk.

**Why it happens:** The anomaly arises from PS1 hardware's handling of `INT16_MIN * x` — the product's saturation or sign-flip path differs from the other 16-bit coefficients. It is counter-intuitive enough that implementations often treat the full int16 range uniformly.

**Warning signs:** Preset audition sounds "close" but shows a persistent wrongness only on presets that happen to use -8000h in vIIR; witness diff shows a sign inversion in one channel at late reverb decay.

**Prevention:** Treat vIIR as a 16-bit register but explicitly special-case the -8000h value and document in `DECISIONS.md`. Add a P-REG unit test that writes -8000h to vIIR and checks the post-write memory value against the nocash-documented behavior.

**Phase:** P-REG (register unit tests), P-DECISIONS (document the special case)

**Source:** [psx-spx SPU documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/); mirrored in [problemkaputt.de/psx-spx.txt](https://problemkaputt.de/psx-spx.txt)

---

### 1.2 Signed vs unsigned coefficient interpretation

**Severity:** Catastrophic

**What goes wrong:** All 16-bit reverb volume registers (vIIR, vWALL, vCOMB1-4, vAPF1-2, vLOUT, vROUT, vLIN, vRIN) are *signed* int16 in the range -8000h..+7FFFh. Treating them as unsigned (which is tempting if your register-write API takes `uint16_t`) turns any negative coefficient (bit 15 set) into a wildly wrong positive number. Result: network becomes unstable, output saturates to silence or clip.

**Why it happens:** Ingesting raw hex preset values into a `uint16_t*` array is natural and looks correct until you multiply. The nocash docs write register values in hex, which reads as unsigned to a first glance.

**Warning signs:** Any preset with negative coefficients (half the factory presets do) produces instantly saturated / NaN-like output. Witness diff is wildly different on precisely those presets.

**Prevention:** Public register-write API takes `int16_t` for volume registers and `uint16_t` for address/offset registers. Internal coefficient storage is `int16_t`. Assertion tests feed the 10 factory presets through the register writer and verify the sign bit round-trips. Document the signedness of every register in `docs/LEVERS-CATALOG.md`.

**Phase:** P-REG, P-API, P-LEVERS

**Source:** [psx-spx SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) — "all volume registers store signed 16-bit values in the range -8000h..+7FFFh"

---

### 1.3 The implicit "divide by 8000h" fractional point

**Severity:** Catastrophic

**What goes wrong:** Every coefficient multiplication in the reverb formula is effectively Q15: `(a * b) / 0x8000` with the result re-saturated to int16. Omitting the shift, using `/ 0x10000`, or using `>> 16` (Q16 instead of Q15) halves or doubles every reflection gain. The network goes silent or explodes.

**Why it happens:** Confusion between Q15 (shift right by 15) and Q16 (shift right by 16), or between `/ 0x8000` (Q15 with sign handling) and `>> 15` on a signed integer (implementation-defined in C99 — see Pitfall 2.3). Also tempting to refactor into `* coeff_as_float` where `coeff_as_float = coeff / 32768.0f`.

**Warning signs:** Every register acts like it's 6 dB too loud or quiet; preset audition shows characteristic decay that is either too short or infinite.

**Prevention:** Implement a single `q15_mul(int32_t a, int16_t b) -> int16_t` helper used by every reverb-stage multiply, with truncation semantics (see 1.4) and saturation (see 1.5). Unit-test `q15_mul` exhaustively against a known table before wiring into the network.

**Phase:** P-FIXPOINT (single macro/inline helper with unit tests precedes P-NETWORK)

**Source:** Inferred from the uniform `/8000h` structure of the nocash reverb formulas; consistent with [fixed-point DSP Q15 conventions](https://sestevenson.wordpress.com/2009/08/19/rounding-in-fixed-point-number-conversions/).

---

### 1.4 Truncation vs rounding in the Q15 multiply

**Severity:** Catastrophic (for bit-accuracy goal)

**What goes wrong:** The PS1 SPU truncates (discards fractional bits toward -∞ for negative two's-complement values); rounding — even banker's rounding — is a semantic divergence that accumulates across every reverb pass and breaks bit-exact witness diffs. The M1 requirements explicitly call out "integer truncation (not rounding)."

**Why it happens:** "Rounding is more accurate" is conventional DSP wisdom ([dsprelated on Q15](https://sestevenson.wordpress.com/2009/08/19/rounding-in-fixed-point-number-conversions/)). Any engineer applying that default loses bit-accuracy. It is also invisible on a single sample — only emerges after many recirculations.

**Warning signs:** Golden-file tests pass individual register tests but diverge on long-tail reverb (where truncation error accumulates); witness diff shows a tiny, slowly-growing delta.

**Prevention:** Explicit `q15_mul_truncate` helper with a comment block citing the M1 requirement. Dedicated unit test comparing truncate vs round on a sweep of (a, b) pairs to verify the implementation is the truncate variant. Bit-truncation on two's-complement is equivalent to arithmetic shift right on *positive* values but diverges for negative — the test must cover negatives.

**Phase:** P-FIXPOINT

**Source:** M1 requirement in `PROJECT.md`; reinforced by [nocash/psx-spx reverb formulas](https://psx-spx.consoledev.net/soundprocessingunitspu/) which describe results as written-back-to-memory int16.

---

### 1.5 Saturation (clamp to [-8000h, +7FFFh]) vs wrap-around overflow

**Severity:** Catastrophic

**What goes wrong:** Every intermediate reverb value must saturate (clamp) to int16 range on overflow, not wrap. A C implementation that lets int arithmetic wrap naturally (e.g., `int16_t r = (int16_t)(a * b)` with truncation of the int32 product) will produce audible glitches on loud inputs that diverge from hardware's hard-clip behavior. Conversely, an implementation that accidentally clamps *address* registers (which are 16-bit unsigned wraparound pointers into SPU RAM) will break buffer addressing.

**Why it happens:** C integer overflow on signed types is undefined behavior; implementers either use `(int16_t)x` casts (wrap) or hand-roll saturation. Applying saturation to address arithmetic (should wrap) is a symmetric mistake.

**Warning signs:** Loud transients produce clicks or discontinuities on the reverb return that aren't present on the dry signal; OR loud transients sound cleaner on SPU-94 than on lv2-psx-reverb witness — the witness preserves the hardware's clip color.

**Prevention:** Separate `sat_add_i16`, `sat_sub_i16`, `q15_mul` helpers (which saturate) from `addr_add_u16` helpers (which wrap modulo buffer). Each has its own unit test. `q15_mul` must take an `int32_t` accumulator so the product has headroom *before* saturation. P-FIXPOINT test vector includes deliberately-overflowing operands.

**Phase:** P-FIXPOINT; address arithmetic belongs to P-NETWORK

**Source:** Mednafen's Nov 2020 fix specifically addressed "output precision/ranges/clamping" — evidence this is a pitfall even experienced emulator authors hit. [Mednafen ChangeLog](https://mednafen.github.io/documentation/ChangeLog.txt)

---

### 1.6 22050Hz half-rate: the single biggest implementation divergence in the field

**Severity:** Catastrophic (for bit-accuracy goal); Significant (for perceptual accuracy)

**What goes wrong:** The SPU reverb engine runs at half the main SPU rate. Per nocash: "The reverb hardware spends one 44100h cycle on left calculations, and the next 44100h cycle on right calculations" — so the reverb effectively operates at 22050 Hz, with L/R interleaved on alternating 44.1 kHz ticks. Implementations that run reverb at full 44.1 kHz on both channels produce a brighter reverb that misses the hardware's characteristic HF roll-off. **lv2-psx-reverb explicitly ships this deviation:** the README states "Unlike the real console, this code doesn't downsample the reverb to 22050 Hz. But other than the additional brightness of the higher frequencies it sounds almost spot on to the original."

**Why it happens:** Downsample → process → upsample is extra code and adds a filter-design question. Skipping the step yields "basically the same reverb" at half-ish the engineering cost. Also, host audio is almost always 44.1 or 48 kHz, so the 22050 conversion is friction.

**Warning signs:** Witness diff against lv2-psx-reverb looks "close" on the dry transient but the reverb tail is noticeably brighter than either hardware capture or a witness that models the half-rate. If you diff against lv2-psx-reverb and the spectra match, you've both made the same mistake.

**Prevention:**
1. Run reverb internally at 22050 Hz with explicit L/R alternation per the nocash formula.
2. Host-rate I/O handled by an input downsampler and an output upsampler that are documented and justified in `DECISIONS.md` — these are "outside the bit-accurate core."
3. Cross-reference against a hardware capture (M5 requirement) rather than against lv2-psx-reverb on this specific question — lv2-psx-reverb is NOT a witness for half-rate behavior.
4. Document this explicitly in `DECISIONS.md` as a known witness divergence, so future readers don't assume lv2-psx-reverb agreement implies correctness.

**Phase:** P-NETWORK (internal 22050 rate); P-API (rate conversion at the boundary); P-DECISIONS (document lv2-psx-reverb's deliberate deviation on this axis); P-WITNESS (call this out in the diff harness so diffs don't silently mask the divergence)

**Source:** [psx-spx SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/); [lv2-psx-reverb README](https://github.com/ipatix/lv2-psx-reverb/blob/master/README.md)

---

### 1.7 Work buffer wrap addressing and the `(MAX(mBASE, (addr+2) AND 7FFFEh))` formula

**Severity:** Significant (crash or silent corruption)

**What goes wrong:** The reverb buffer address advances per sample using `BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)`. Getting any element of this wrong — forgetting the MAX clamp, using `0x7FFFF` instead of `0x7FFFE`, advancing by 1 instead of 2 (addresses are in 16-bit units, not bytes per spec), or comparing as signed — produces read/write-out-of-range bugs at the top of SPU RAM (near 0x80000), buffer wraparound artifacts at the mBASE boundary, or phase discontinuities when the address recycles.

**Why it happens:** Address units in the spec are inconsistent: `mBASE` is documented as "divided by 8" in some contexts, register offsets are in int16 units, and SPU RAM is byte-addressed. Translation errors are the norm. Off-by-one at the wraparound boundary is a classic crash.

**Warning signs:** Long-running tests crash (SIGSEGV) after minutes; short tests pass. Or: perfectly clean output except for a single click at a predictable interval (the wrap boundary).

**Prevention:**
- Write the address math as an `spu_rev_addr_advance` helper with a unit test that specifically probes the mBASE boundary and the 0x7FFFE wrap.
- Fuzz test: pick 1000 random `mBASE` values and 1000 random offsets, advance 10^6 samples, assert no address ever exceeds 0x7FFFE and never drops below mBASE.
- The internal buffer in SPU-94 can be a power-of-two C array with a compile-time assertion that `0x7FFFE < buffer_size`; the wrap math uses `& (size-1)` rather than the hardware's more-complex form — and this substitution is *documented in DECISIONS.md as equivalent* (or is it? Verify that hardware MAX(mBASE, wrap) produces identical addresses to a pure power-of-two mask given the same register writes — this may itself be a gray-area decision).

**Phase:** P-NETWORK, P-DECISIONS

**Source:** [psx-spx SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) — "BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)"

---

### 1.8 Zero-valued registers: "off" vs "blow up"

**Severity:** Significant

**What goes wrong:** Some registers at zero mean "skip this stage"; some mean "feed the network with 0"; some are undefined. E.g., `vCOMB1 = 0` zeros one comb contribution (legitimate and part of the "Reverb off" preset). But `dCOMB1 = 0` pointing at a buffer region that isn't cleared can alias an adjacent stage's tap and produce non-silence even when the operator expected silence. The "Reverb off" preset uses *nonzero* dummy offsets specifically to prevent buffer-fill corruption — per the spec. Implementations that "helpfully" zero offsets when zeroing volumes replicate the wrong semantics.

**Why it happens:** "0 means off" is the intuitive API design for a musical control, but it's not what the hardware does.

**Warning signs:** "Reverb off" preset produces non-silence, or worse, noise-shaped silence that changes with input. Witness diff on "Off" preset disagrees.

**Prevention:** Treat every register as "write-through" — no bailout logic that says "if volume is 0, skip this stage." Implement the formulas literally. Unit-test the "Reverb off" preset explicitly: load it, feed white noise, assert output is zero (or the documented near-zero value, whatever the spec specifies).

**Phase:** P-REG, P-PRESETS

**Source:** [psx-spx SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) — "The 'Reverb off' configuration uses dummy registers with specific nonzero memory offsets to prevent buffer fill corruption"

---

### 1.9 Input gain ambiguity — vLIN/vRIN vs vIIR vs the main SPU mix

**Severity:** Significant

**What goes wrong:** Where "input" enters the reverb is genuinely confusing because there are multiple gain stages: the main SPU mix sends a signal to the reverb via the *main volume* × *reverb send* path, which reaches `Lin/Rin` in the reverb formula. Inside the formula, `vLIN * Lin` appears at the Lin contribution to `dLSAME/dLDIFF`. `vIIR` is the IIR coefficient for same-side reflections, not input gain — a common misreading. Implementations that put input gain in the wrong stage sound louder/quieter or have wrong-feeling "depth."

**Why it happens:** The nocash formula is dense. It's tempting to skim it and put "the input gain" at the first multiply you encounter.

**Warning signs:** Wet/dry balance feels wrong across all presets uniformly (suggests a gain-stage misplacement, not a coefficient bug); witness diff shows output level off by a consistent dB across all presets.

**Prevention:** Before writing code, in `DECISIONS.md` draw the full signal graph with every multiply labeled by register name. Implement from the diagram; review the diagram against nocash side-by-side. Unit-test each register's musical effect in isolation (P-MODTEST).

**Phase:** P-NETWORK, P-DECISIONS (signal-flow diagram is a deliverable, not a sketch)

**Source:** [psx-spx SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) — reverb formula block

---

### 1.10 Silent reordering of operations within a sample

**Severity:** Significant (for bit-accuracy)

**What goes wrong:** The nocash formulas prescribe an order: Lin-side reflections, then Rin-side, then comb filters, then APF1, then APF2. A compiler-friendly refactor that interleaves L/R or reorders comb+APF stages produces the same ideal output but diverges on overflow/saturation (since saturation is not associative).

**Why it happens:** Refactoring for cache locality or vectorization. The reordering is invisible in float math but observable in saturated int math.

**Warning signs:** Bit-exact witness diff passes on small amplitudes and fails on loud ones.

**Prevention:** Implement the reverb step function as a single flat sequence matching the nocash formula line-by-line, with each intermediate in its own named local variable. Add a comment block above each stage citing the exact nocash line. Resist optimization until P-MODTEST passes.

**Phase:** P-NETWORK

**Source:** Inferred from saturation non-associativity (general fixed-point DSP wisdom); [dsprelated](https://www.dsprelated.com/showthread/comp.dsp/78628-1.php)

---

## Category 2 — Bit-Accuracy Verification Pitfalls

### 2.1 Floating-point creep ("just this one coefficient as float")

**Severity:** Catastrophic

**What goes wrong:** An engineer thinks "the coefficient is a constant, I can store it as a float, multiply by float, and cast back." Each int16→float→int16 round-trip introduces rounding (since `float` has 24 bits of mantissa and can represent every int16 exactly, this is safe *in isolation* — but combined with the wrong saturation/truncation semantics, it injects rounding where the hardware truncates). **lv2-psx-reverb converts all coefficients to float at load time.** This is why lv2-psx-reverb is a *behavioral* witness, not a bit-accurate one.

**Why it happens:** Speed, readability, fewer bugs in the arithmetic — float is just easier. SPU-94's entire value proposition rejects this trade.

**Warning signs:** Any appearance of `float` or `double` types anywhere in `libspu94/src/reverb*.c`. Any use of `*`, `/` on non-integer types within the hot path. `grep -rn 'float\|double' libspu94/src/reverb*` as a CI check.

**Prevention:**
- Enforce via CI: `grep -E '\b(float|double)\b' libspu94/src/reverb*.c` must return zero matches except in explicit "rate converter" files at the API boundary.
- Type the hot path with `int16_t`, `int32_t`, `uint16_t` exclusively.
- All test vectors are integer-valued.

**Phase:** P-FIXPOINT, P-NETWORK, and a CI guard added in the earliest P-API increment

**Source:** [lv2-psx-reverb README](https://github.com/ipatix/lv2-psx-reverb/blob/master/README.md) confirms float-based implementation; general fixed-point DSP discussion at [Valhalla DSP](https://valhalladsp.com/2011/07/07/algorithmic-reverbs-distortion-and-noise/)

---

### 2.2 Endian or bit-order assumptions in preset / register init

**Severity:** Significant

**What goes wrong:** Hex values in the nocash preset tables are the little-endian 16-bit words as they appear in SPU register space. Reading them into a host struct with wrong field layout, or reading a hardware capture file with wrong endian, inverts bytes and turns sensible register values into garbage.

**Why it happens:** Most test environments are x86_64 little-endian and Cortex-M is also little-endian, so the bug stays hidden until exactly the wrong platform test.

**Warning signs:** Test on x86 passes; Cortex-M cross-compile passes build but fails smoke test (if smoke test ever runs code). Alternatively, any `.wav` or `.bin` fixture loaded directly into a struct triggers garbage.

**Prevention:** Always read preset / capture data through an explicit `read_u16_le(uint8_t *p)` helper; never `memcpy` a struct from disk. Preset tables are C constants in source (`{ .vIIR = 0x4C96, ... }`) — the compiler handles endian.

**Phase:** P-PRESETS, P-CLI (WAV I/O), P-CROSS (smoke test)

**Source:** General systems-programming wisdom; reinforced by psx-spx noting "games will typically write to [SPU registers] using two 16-bit writes instead of a single 32-bit write" — implying byte-order sensitivity at the hardware interface.

---

### 2.3 Signed right-shift implementation-defined behavior

**Severity:** Nuisance → Significant (depends on compiler target)

**What goes wrong:** `int x = -7; x >> 1;` is implementation-defined in C99 for negative values. GCC, Clang, MSVC, and ARM's compilers all arithmetic-shift (sign-extend), so in practice this is a nonissue on our targets. **But:** the *standard* doesn't guarantee it, so some static analyzers flag it, and some older/embedded toolchains (legacy TI, Keil) documented differently. If SPU-94 ever targets a toolchain that logical-shifts signed negatives, truncation becomes rounding-toward-zero, breaking bit-accuracy silently.

**Why it happens:** Using `x >> 15` for Q15 truncation "because it's faster than divide."

**Warning signs:** Cross-compile to an exotic target passes unit tests but fails golden-file tests; behavior differs between `-O0` and `-O3`.

**Prevention:** Prefer `/ 0x8000` and let the compiler optimize (modern compilers produce the same arithmetic shift). If you need `>>`, wrap in a `q15_shr(int32_t x)` helper with a static_assert probe: `_Static_assert((-1 >> 1) == -1, "arithmetic shift required");` — fail the build loudly on any platform that doesn't arithmetic-shift. Cortex-M GCC is fine; this is insurance.

**Phase:** P-FIXPOINT (the helper + static_assert), P-CROSS (run the assert on the Cortex-M build)

**Source:** [ISO C99 § 6.5.7 / Autoconf discussion](https://lists.gnu.org/archive/html/autoconf-patches/2001-08/msg00104.html); [CMSIS uses fixed-width types specifically for this reason](https://arm-software.github.io/CMSIS_5/DSP/html/group__group.html)

---

### 2.4 Sample-offset ambiguity in witness diffs — "one sample off but otherwise identical"

**Severity:** Significant (wastes weeks if not handled upfront)

**What goes wrong:** You render the same audio through SPU-94 and lv2-psx-reverb, diff them, and they're wrong by a single sample of latency. You assume bug. It's actually buffer scheduling. OR: it's a real bug that adds one sample of latency that accumulates. You can't tell without an alignment step.

**Why it happens:** Different implementations have different first-sample conventions, different startup-silence lengths, different internal buffer initialization.

**Warning signs:** Witness diff shows "large" error only at first N samples, then "small" error after; or cross-correlation peak is at a nonzero lag.

**Prevention:** The diff harness runs cross-correlation first, aligns to peak lag, and reports lag separately from post-alignment diff. Report both `raw_rms_diff` and `aligned_rms_diff`. Configure an acceptable lag tolerance (e.g., ±4 samples) and fail if lag drifts over time or differs between presets. Use the approach of tools like [Audio DiffMaker](https://www.libinst.com/Audio%20DiffMaker.htm).

**Phase:** P-WITNESS (the harness is where this lives)

**Source:** [Audio DiffMaker](https://www.libinst.com/Audio%20DiffMaker.htm); general audio-comparison tooling

---

### 2.5 Golden files pinned to a buggy implementation

**Severity:** Significant

**What goes wrong:** You snapshot output on day 30, then on day 60 you fix a real bug and 80% of your golden tests fail. You have to distinguish "fixed a bug" from "regressed." Every regeneration of goldens is a mini-crisis.

**Why it happens:** Goldens are seductive — they catch real regressions cheaply.

**Prevention:**
- Every golden file has a companion `*.sha256` AND a companion `*.sig.md` describing: what inputs produced it, what commit hash was checked out, what the author listened for and believed correct, and what *known limitations* applied at sign-off.
- Regenerating a golden is a git-committed event with a mandatory DECISIONS.md entry.
- Goldens are signed off by explicit human audition (listen to the file) at creation, not just "algorithm didn't crash."

**Phase:** P-CLI (golden harness), P-DECISIONS

**Source:** Inferred from experience with golden-file testing in audio projects

---

## Category 3 — API Design Pitfalls for Real-Time DSP Libraries

### 3.1 Hidden allocations in the process path

**Severity:** Catastrophic (for RT-safety; less so for M1 since no RT path yet)

**What goes wrong:** The C library itself is RT-safe, but the Python binding's `process_block()` allocates a numpy array on every call. Or the CLI's WAV-reader allocates per-block. Or a debug log path conditionally calls `malloc()`. When the code is later wrapped in JUCE (M4), these allocations cause dropouts.

**Why it happens:** Python / WAV I/O code is written at a different level of rigor than the core; allocations hide in innocuous-looking constructs (`numpy.empty`, `struct.pack` into a new bytes object).

**Warning signs:** M1 tests pass but M4 JUCE integration has xruns; profiling shows `malloc` in the audio callback.

**Prevention:**
- Core C library: grep for `malloc|calloc|realloc|free|mmap|brk` in `libspu94/src/` — CI-enforced zero matches. All state is in a caller-allocated `struct spu94_state`.
- Python binding: the `process_block(state, input_np, output_np)` API takes caller-supplied numpy arrays (both input and output); the binding never allocates. Document in a README.
- CLI: WAV I/O is outside the "core" and may allocate, but the per-sample path inside does not.

**Phase:** P-API (design), P-PYBIND (binding design), P-CLI (keep allocation out of per-sample loop)

**Source:** Standard real-time-audio guidance; [Ross Bencina "real-time audio programming 101"](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing)

---

### 3.2 Locks or syscalls in the process path

**Severity:** Catastrophic (for RT-safety; less for M1)

**What goes wrong:** To make mid-stream register writes "safe," an implementer adds a mutex around the register array. Audio thread takes the lock every sample; control thread takes it every write. Priority inversion, blocking, dropouts.

**Why it happens:** "Thread safety" intuition from general-purpose programming.

**Prevention:**
- Register writes from the control side use atomics or a single-writer-single-reader lock-free pattern.
- Document the concurrency model in `DECISIONS.md`: which registers are safe to write from which thread, under what memory-ordering guarantees.
- Specifically: option A — the audio thread reads a `_Atomic int16_t` register array written by the control thread (may tear on non-atomic-word-sized writes on some MCUs — verify `_Atomic int16_t` is lock-free on Cortex-M). Option B — ring-buffer of pending writes drained at the start of each block. Option B is safer and easier to verify; A has lower latency.
- M1 test: run the modulation test harness with register writes on a separate thread and verify zero drops (P-MODTEST).

**Phase:** P-API, P-MODTEST, P-DECISIONS

**Source:** Standard RT-audio guidance; Ross Bencina; [timur.audio "Four common mistakes in audio development"](https://timur.audio/using-locks-in-real-time-audio-processing-safely)

---

### 3.3 Callback vs push vs pull API choice

**Severity:** Significant

**What goes wrong:** Choosing a callback-driven API forecloses the pull-based CLI use case (golden-file rendering). Choosing pure push forecloses the real-time-plugin use case. Choosing something bespoke makes the FFI harder.

**Why it happens:** API designed around today's caller (Python tests), not tomorrow's caller (JUCE plugin, MCU firmware).

**Prevention:** Adopt a pure **block-processing pull API** as the primitive: `spu94_process_block(state, in_L, in_R, out_L, out_R, n_samples)`. All other styles are wrappers. This works for CLI (call in a loop), for Python (call on numpy slices), for JUCE (call from the audio callback), for MCU (call from the DMA half-complete ISR). Callbacks or push adapters can be added later on top; the primitive is pull.

**Phase:** P-API

**Source:** Idiomatic to JUCE's AudioProcessor::processBlock, PortAudio's callback, CoreAudio's AUInternalAudioStreamBasicDescription, and every MCU DMA-double-buffer pattern. Lowest common denominator.

---

### 3.4 Parameter smoothing sneaking into the "bit-accurate" core

**Severity:** Catastrophic (for accuracy claim)

**What goes wrong:** To make mid-stream modulation glitch-free, an implementer adds coefficient smoothing (one-pole LPF on every register change) inside the core. Now the core isn't bit-accurate anymore — every coefficient the network sees is a smoothed version of the one the user wrote. The accuracy claim is silently gone.

**Why it happens:** The M1 mandate ("mid-stream register updates must be glitch-free") and the bit-accuracy mandate pull opposite directions. Smoothing is the obvious solution to the first; it contradicts the second.

**Prevention:**
- The core is **bit-accurate given the sequence of register values it observes**. Smoothing is *outside* the core — it lives in the M4 "lever layer."
- For M1: mid-stream writes take effect at sample boundaries with no internal smoothing. The modulation test (P-MODTEST) verifies that writing a coefficient at sample N produces, at sample N+1, network behavior consistent with "coefficient was this value from the start."
- Glitch-freedom in M1 means: no crashes, no denormals/NaN, bounded output, no buffer corruption. It does NOT mean: no zipper noise. Zipper noise at audio-rate coefficient sweeps IS expected in M1 (the hardware has it too at high modulation rates).
- This boundary is documented in `DECISIONS.md` and `LEVERS-CATALOG.md`: each lever is annotated with "needs smoothing at the lever layer (M4)" vs "safe to modulate raw at audio rate."

**Phase:** P-API, P-MODTEST, P-DECISIONS, P-LEVERS

**Source:** [Valhalla DSP on smoothing](https://valhalladsp.com/2010/11/27/valhallashimmer-the-controls/) — "all sliders have been designed to be tweaked in real time and have a smoothed response to avoid clicks" (i.e., smoothing is a musical-lever concern); this must live outside the bit-accurate core.

---

## Category 4 — Mid-Stream Register Modulation Pitfalls

### 4.1 Delay-length (`dCOMB1..4`, `dAPF1/2`, `dLSAME`, etc.) changes: phase discontinuity

**Severity:** Significant (audible click); Catastrophic (if buffer state corrupts)

**What goes wrong:** Changing a delay offset mid-stream makes the next read tap a different location in the work buffer than the previous read expected. Depending on what's at the new location, the result is: a click (most common), a phase-inverted echo (if the new tap reads from an inverted copy), or — worst — silently-wrong output that still sounds plausible.

The real PS1 hardware just does this. Games that changed reverb preset mid-gameplay produced the same click. So there is no "correct" glitch-free behavior to replicate — there is only a *policy* to choose and document.

**Why it happens:** The implementer tries to "smooth out" the delay-length change via crossfade or interpolated read position, adding dependencies (read+write indices, sub-sample delay, sinc interpolation) that are outside the bit-accurate mandate.

**Prevention:**
- Policy decision documented in `DECISIONS.md`: "Delay-length register changes take effect at the next sample boundary. No fractional delay, no crossfade, no interpolation. Resulting clicks are part of the faithful behavior."
- The M4 lever layer can later add a crossfade wrapper for musical use (flagged as a non-bit-accurate mode).
- Modulation test (P-MODTEST) verifies the click happens at the expected sample and doesn't propagate beyond the immediate transient.
- **Separate test:** rapid delay-length modulation (sweep at 10 Hz) must not corrupt the work buffer or cause unbounded output. Catches the *catastrophic* failure mode.

**Phase:** P-API, P-MODTEST, P-DECISIONS, P-LEVERS

**Source:** [KVR forum on delay-line interpolation and zipper](https://www.kvraudio.com/forum/viewtopic.php?t=42849); [CCRMA on delay-line interpolation](https://ccrma.stanford.edu/~jos/pasp06/Delay_Line_Interpolation.html)

---

### 4.2 Coefficient zipper noise at audio-rate modulation

**Severity:** Nuisance (expected); only a pitfall if unacknowledged

**What goes wrong:** Sweeping vWALL or vIIR at audio rate produces zipper — stepped artifacts as the coefficient quantizes across int16 values. This is audible. Users expect smooth modulation.

**Why it happens:** Coefficients are int16. A sweep from 0x4000 to 0x4001 is a discrete step.

**Prevention:**
- Acknowledge in M1: zipper at audio-rate modulation is expected and documented. Not a bug.
- In `LEVERS-CATALOG.md`, annotate each register with its expected zipper behavior under modulation.
- M4 lever layer adds interpolation where musically appropriate, explicitly crossing the bit-accuracy boundary with documentation.

**Phase:** P-LEVERS, P-MODTEST (observe and document, not fix)

**Source:** [Valhalla on smoothing as a plugin-level concern](https://valhalladsp.com/2010/11/27/valhallashimmer-the-controls/)

---

### 4.3 Register write happening between internal L and R sub-samples

**Severity:** Significant

**What goes wrong:** The reverb runs at 22050 Hz with L and R on alternating 44.1 kHz ticks. A register write from the control thread at 44.1 kHz may land between the L-calc tick and the R-calc tick. If the write lands there, L and R use *different* coefficients for "the same sample." Depending on policy this is correct (matches hardware) or wrong (creates L/R imbalance).

**Why it happens:** The implementer picks one convention without realizing the interaction with half-rate processing.

**Prevention:**
- Pick and document the convention in `DECISIONS.md`. Two reasonable choices: (a) writes take effect only at L-tick boundaries (stereo-synchronous); (b) writes take effect immediately, accepting L-before-R mismatch on the transitional sample (matches hardware more closely).
- Choice (b) is more faithful and simpler; prefer unless witness diff disproves.
- Modulation test (P-MODTEST) includes a test that writes a register at a known sub-sample offset and verifies the observed L/R output matches the policy.

**Phase:** P-NETWORK, P-API, P-MODTEST, P-DECISIONS

**Source:** Inferred from [psx-spx](https://psx-spx.consoledev.net/soundprocessingunitspu/) L/R alternation description

---

### 4.4 Comparable projects' approaches (observable, not source-read)

**What the field does:**
- **lv2-psx-reverb:** smooths wet/dry/master gain parameters with exponential ramping (`rev->dry += 0.001f * (dry_coef - rev->dry)`), but switches presets by *reloading all coefficients at once* — audible click on preset change, no interpolation of individual register values. On preset-switch specifically, it clears the SPU buffer with `memset()`, which is a design choice (avoids loud artifacts from an old-preset tail bleeding into the new preset) but isn't hardware-faithful.
- **Valhalla plugins:** all parameters are smoothed ("designed to be tweaked in real time and have a smoothed response to avoid clicks"). This is a plugin-level contract, above the DSP core.
- **Strymon pedals:** proprietary, public documentation is marketing-level.

**Implication for SPU-94:** M1 core does not memset on preset switch (hardware doesn't), does not smooth coefficients (hardware doesn't). The M4 lever layer may offer a `preset_morph` wrapper that does one or both, clearly flagged as "non-bit-accurate convenience feature."

**Source:** [lv2-psx-reverb architecture summary](https://github.com/ipatix/lv2-psx-reverb/blob/master/psx-reverb.c) (reviewed for architectural approach only, not code); [Valhalla blog](https://valhalladsp.com/2010/11/27/valhallashimmer-the-controls/)

---

## Category 5 — Python Binding Pitfalls (ctypes + numpy)

### 5.1 numpy array garbage-collected while ctypes pointer is live

**Severity:** Catastrophic (segfault)

**What goes wrong:**
```python
ptr = np.ascontiguousarray(np.zeros(1024, dtype=np.float32)).ctypes.data_as(POINTER(c_float))
# np.ascontiguousarray result has no ref; GC runs; ptr is dangling
lib.spu94_process(ptr, 1024)  # SEGV
```

**Why it happens:** Fluent-API pattern hides the array object. Temporary numpy arrays from arithmetic (`(a+b).ctypes.data`) are immediately GC-eligible.

**Prevention:**
- The Python wrapper always takes named numpy arrays as arguments and keeps references in locals/attributes during the call.
- Wrapper docstring explicitly warns: "Do not pass expression results directly — bind to a local first."
- Use `.ctypes.data_as()` (keeps a ref) not `.ctypes.data` (raw int).
- Test: `pytest` + `valgrind` (or at least `faulthandler` enabled) on a hot-loop test that creates fresh arrays in a tight loop, to surface GC-interaction bugs.

**Phase:** P-PYBIND

**Source:** [numpy GH#28238 "array.ctypes.data_as is not memory safe"](https://github.com/numpy/numpy/issues/28238); [numpy docs](https://numpy.org/doc/stable/reference/generated/numpy.ndarray.ctypes.html)

---

### 5.2 ctypes does not release the GIL

**Severity:** Significant (for perf); not catastrophic for M1 tests

**What goes wrong:** A ctypes-called C function holds the GIL for its entire duration. For long `process_block()` calls with large block sizes, this blocks other Python threads. If test harness tries to stream register writes from a separate Python thread during processing, writes stall until the block returns.

**Why it happens:** ctypes default.

**Prevention:**
- For M1, acceptable: tests run single-threaded.
- For the modulation test (P-MODTEST), use blocks small enough that the write-thread latency is acceptable (e.g., 64-sample blocks at 44.1 kHz = ~1.5 ms quantum), OR bypass ctypes for the hot loop and use a cffi/Cython shim later.
- Document in `DECISIONS.md`: "ctypes chosen for minimum-maintenance binding; does not release GIL. Multi-threaded stress tests use small blocks to keep control-thread latency bounded. A future cffi migration is viable if GIL becomes a blocker."
- If the modulation test requires it, consider a C-side test entry point (a function that internally does both processing and scheduled writes, called once from Python).

**Phase:** P-PYBIND, P-MODTEST, P-DECISIONS

**Source:** [Python ctypes docs](https://docs.python.org/3/library/ctypes.html); [NumPy "Python as glue"](https://numpy.org/devdocs/user/c-info.python-as-glue.html)

---

### 5.3 C/Python struct layout mismatches

**Severity:** Catastrophic (wrong data, hard to diagnose)

**What goes wrong:** C struct has implicit padding; `ctypes.Structure` has default packing (which may or may not match). Register values read from the Python side are garbage.

**Why it happens:** `ctypes.Structure` fields are packed to native alignment by default, which *usually* matches C's, but not for unusual field orderings. Adding a `uint64_t` field later silently re-aligns later fields.

**Prevention:**
- `struct spu94_state` is defined in C with explicit `_Static_assert` on offsets of every public field.
- Python side uses `ctypes.Structure` with `_pack_ = 1` only if C side uses `__attribute__((packed))` — otherwise, match natively.
- Alternative: expose no public state fields across FFI. Python sees only an opaque pointer + getter/setter functions. This is slower but bulletproof; preferred for M1.
- Test: trivial Python test that writes a register and reads it back via a C-side getter verifies the round-trip. Run on every commit.

**Phase:** P-API (opaque-pointer contract), P-PYBIND

**Source:** [ctypes docs](https://docs.python.org/3/library/ctypes.html); general FFI wisdom.

---

### 5.4 "It works on my Linux" — glibc/ABI distribution pitfalls

**Severity:** Nuisance for M1 (single-user); Significant if project expands

**What goes wrong:** Shared library built against glibc 2.38 fails to load on a user's older Ubuntu with glibc 2.31. Or: symbol visibility defaults to public, exposing internal symbols that break ABI on the next release.

**Prevention:**
- For M1 (Linux dev target), build script is reproducible (pinned container or documented flags).
- `-fvisibility=hidden` by default, `__attribute__((visibility("default")))` on the public API.
- Use a dedicated Linux container (Ubuntu 22.04 LTS baseline or similar) for builds; document.
- Avoid C library features not in the Cortex-M newlib subset (see Category 8).
- `readelf -d libspu94.so` in CI to audit the ABI and catch accidental symbol-leakage or unwanted dependencies.

**Phase:** P-API (visibility annotations), P-PYBIND, P-CROSS

**Source:** General Linux distribution wisdom; [GCC visibility docs](https://gcc.gnu.org/wiki/Visibility)

---

## Category 6 — Licensing / Legal Pitfalls

### 6.1 Inadvertent derivative work from GPL witnesses

**Severity:** Catastrophic (relicensing/project reset required)

**What goes wrong:** Even with a stated "no reading GPL source" policy, implementer checks a witness's source to resolve an ambiguity, unconsciously absorbs structure (variable names, function ordering, comment cadence), and produces code that is arguably derivative. Mednafen is GPLv2; lv2-psx-reverb is GPLv3. A derivative work inherits the copyleft; SPU-94 could not then be relicensed MIT/Apache.

**Why it happens:** "Just a quick peek to resolve this one question" compounds. Even without copy-paste, *structural similarity* has been held to matter in cases like Oracle v. Google and Computer Associates v. Altai.

**Prevention:**
- Written policy (already in PROJECT.md Constraints): witness source is not read as a primary activity.
- If a consultation happens (e.g., to resolve a specific ambiguity), log it in `DECISIONS.md` with: what was consulted, what question, what was learned, what was NOT taken.
- **Positive defense — the "non-tainted implementer" pattern from Computer Associates v. Altai:** write the implementation from the spec FIRST, then consult witnesses only for behavioral diffs via audio comparison, never structural diffs via code review. This is the clean-room pattern the Altai court accepted.
- Avoid naming conventions that mirror Mednafen/lv2-psx-reverb (their `rev->dry += 0.001f * ...` pattern, their file structure, their function names).
- Final license choice deferred to end of M1 — if derivation creeps in, the project falls back to GPLv3.

**Phase:** P-DECISIONS (log every witness consultation)

**Source:** [Wikipedia Clean-room design](https://en.wikipedia.org/wiki/Clean_room_design); [RetroReversing](https://www.retroreversing.com/clean-room-reversing); [Computer Associates v. Altai](https://en.wikipedia.org/wiki/Computer_Associates_Int%27l,_Inc._v._Altai,_Inc.); [Sony v. Connectix](https://en.wikipedia.org/wiki/Sony_Computer_Entertainment,_Inc._v._Connectix_Corp.)

---

### 6.2 nocash psx-spx is itself of ambiguous copyright status

**Severity:** Significant (project's licensing posture is less defensible than assumed)

**What goes wrong:** The psx-spx GitHub project README openly states: "No copyright or license have been properly acquired to republish and alter the document... This document isn't a clean room reverse engineering project. A good chunk of the original document has been either directly copy/pasted from the confidential code and documentation from Sony, or summarized." The nocash document is the *primary* spec reference for SPU-94. If Sony were to assert copyright over the spec document, anyone who built software strictly from it would be in a grayer zone than they thought.

**Why it happens:** Reverse-engineering docs for consoles are a legal gray area, and Sony specifically is believed to have been aggressive historically. The psx-spx maintainers are honest about their posture, but users downstream may not read the fine print.

**Prevention:**
- Do not redistribute nocash/psx-spx content verbatim in SPU-94 documentation — paraphrase in your own words.
- Cite nocash as *a* reference among several (hitmen.c02.at, archived Sony SDK docs), not as the single source.
- DECISIONS.md treats each ambiguity resolution as an engineering decision, not a citation. The SPU-94 code is authored from understanding, not transcription.
- Do not include verbatim register tables from psx-spx in the SPU-94 repository. The 10 factory presets are register values (facts, uncopyrightable individually), but the presentation / table structure should be SPU-94's own.
- Consult a lawyer before a public release if this matters to you; acceptable risk for personal project.

**Phase:** P-DECISIONS, P-PRESETS

**Source:** [psx-spx GitHub README](https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/index.md); [kraptor/psx-docs](https://github.com/kraptor/psx-docs)

---

### 6.3 Trademark naming — "PSX Reverb" and "PS1"

**Severity:** Significant (easy to avoid, expensive to remediate)

**What goes wrong:** Product named "PS1 Reverb" or marketed with the Sony logo invites a trademark takedown. Sony has been historically assertive (cf. Sony v. Connectix, though Connectix won on fair-use for the *emulator* — the trademark question was separate).

**Why it happens:** SEO temptation. "PSX" and "PS1" are search terms; the recognizable names sell.

**Prevention:**
- Product name is **SPU-94** (already decided). Working directory "PSX Reverb" is internal only.
- Marketing copy describes the project by its technical character ("reimplementation of the Sound Processing Unit reverb from a 1990s home console") without using "PlayStation," "PS1," or "PSX" in product names, package names (`libspu94`, not `libps1reverb`), or the main README title.
- Sony's trademarks ("PlayStation", "PS1", "PSX", the logo) are nominatively fair-use-able in descriptive prose ("SPU-94 is inspired by the reverb implementation in Sony's PlayStation 1"), but not usable in trademark position.
- Package registry names (future PyPI, crates.io, etc.) should not use "psx" / "ps1".

**Phase:** P-API (package naming), P-CLI (binary name), P-DECISIONS

**Source:** [Sony v. Connectix](https://en.wikipedia.org/wiki/Sony_Computer_Entertainment,_Inc._v._Connectix_Corp.); [retroreversing.com on trademark vs copyright](https://www.retroreversing.com/clean-room-reversing)

---

### 6.4 "Clean-room" defense for a solo developer

**Severity:** Significant

**What goes wrong:** Strict clean-room (one person writes spec, another writes code, never meet) is impossible for a solo project. Without it, derivative-work claims are harder to rebut.

**Prevention:**
- Solo-friendly defensive posture (consistent with Altai):
  1. **Temporal separation** — specification reading and code writing are different sessions, logged in `DECISIONS.md`.
  2. **Source provenance** — every non-obvious design choice in code has a `DECISIONS.md` entry citing the spec section it derives from, not the witness it matches.
  3. **No witness-reading as primary activity** — already in PROJECT.md.
  4. **Audio-level witness diffing only** — structural comparison is never the method of verification.
  5. **Written record of work** — git commits with meaningful messages serve as contemporaneous evidence of independent authorship.
- Be realistic: this is defensible for a non-commercial, non-competing project. If SPU-94 becomes a commercial product, formal legal review is warranted.

**Phase:** P-DECISIONS (the log IS the defense artifact)

**Source:** [RetroReversing](https://www.retroreversing.com/clean-room-reversing); [Lexology on IP clean room policy](https://www.lexology.com/library/detail.aspx?g=57ce5c16-717f-4fe0-9925-30628c54085c)

---

## Category 7 — Project-Management Pitfalls

### 7.1 Scope creep toward "the rest of the SPU"

**Severity:** Catastrophic for delivery timeline

**What goes wrong:** "The reverb works — while I'm at it, let me add ADPCM decode, because it's related." ADPCM is M2 work; ADSR is explicitly out of scope; pitch modulation and noise are out of scope. Each addition delays M1 and expands verification surface.

**Why it happens:** Adjacent features are tempting precisely because the domain knowledge is fresh.

**Prevention:**
- `PROJECT.md` already enumerates Out of Scope bulletpoints — treat these as a contract.
- Every time the temptation appears, ask: "Does this make M1 reverb demonstrably more faithful to hardware, or is this M2+?" If the latter, create a note in a `FUTURE.md` or analogous file and return to the current work.
- Use the factory presets as a ground-truth completion criterion: M1 is done when all 10 presets produce documented, signed-off output matching witness + listener-audition. Not when every tangentially-related idea is implemented.

**Phase:** Project-wide; guarded at every phase transition via the GSD transition checklist

**Source:** [atd.org "Just one more thing"](https://www.td.org/content/atd-blog/scope-creep-the-allure-of-just-one-more-thing); project-management wisdom

---

### 7.2 DECISIONS.md becoming a dumping ground

**Severity:** Significant (undermines document's value)

**What goes wrong:** Every small choice ("used `static` for internal function") gets logged, burying the real gray-area resolutions in noise. Readers can't find the meaningful decisions. The document stops being consulted.

**Prevention:**
- DECISIONS.md entries are admitted only for:
  1. Ambiguity resolutions where the spec is incomplete (every entry names the spec section or documents "spec silent").
  2. Witness consultations (what was consulted and why).
  3. Deviations from witnesses where the rationale matters (e.g., "we model the 22050 Hz half-rate; lv2-psx-reverb does not").
  4. Policy choices with downstream consequences (e.g., the mid-stream write ordering convention).
- NOT admitted: routine coding choices, refactoring notes, TODO-list items.
- Each entry has a standard template: **Question / Context (spec section) / Options considered / Decision / Witness check / Date / Status (open|resolved|deferred)**.
- Monthly review of DECISIONS.md to audit whether every entry still earns its place.

**Phase:** P-DECISIONS (establish template in first commit)

**Source:** Architecture Decision Record (ADR) community practice; [Michael Nygard's original ADR essay](https://www.cognitect.com/blog/2011/11/15/documenting-architecture-decisions)

---

### 7.3 Witness-chasing — tuning code to match a specific witness when spec is silent

**Severity:** Significant

**What goes wrong:** Witness diff against lv2-psx-reverb shows mismatch on a gray-area behavior; implementer tweaks SPU-94 to match lv2-psx-reverb. But lv2-psx-reverb may itself have resolved the ambiguity arbitrarily (or wrongly — see its documented 22050 divergence). Now SPU-94's behavior is inherited from a witness's decision, not from an independent reading of spec.

**Prevention:**
- Process rule: when a witness diff reveals a disagreement and the spec is silent, **STOP before changing code**. Open a `DECISIONS.md` entry. Document: what the witness does, what the spec says (silent or partial), what the other witnesses do, what Anthony's musical ear prefers, what the decision is, *and why it's not just "match the witness."*
- If witnesses agree and Anthony's ear agrees, matching is legitimate (consensus behavior). Document as such.
- If witnesses disagree, SPU-94 picks one side; document the split and the choice. The choice may or may not match any individual witness.
- Include multiple witnesses where possible — at minimum lv2-psx-reverb (primary for M1) plus one auditioned hardware capture (for M5). Agreement of two > agreement of one.

**Phase:** P-WITNESS, P-DECISIONS

**Source:** Direct from `PROJECT.md` gray-area philosophy; reinforced by the lv2-psx-reverb 22050 divergence — a concrete example where witness-chasing would produce *less* faithful output than spec-following.

---

### 7.4 False confidence from factory-preset audition

**Severity:** Significant

**What goes wrong:** The 10 factory presets are the most familiar PS1 reverb sounds. If they *sound right*, it's tempting to declare victory. But "sounds right" is a terrible oracle — sampling many recognizable sounds doesn't prove bit-accuracy; it proves "approximately the right shape." A reimplementation with wrong truncation can still audition convincingly on "Hall."

**Prevention:**
- Presets are **fixtures**, not **tests**. Their role: drive the witness-diff harness with real-world register configurations. Their role is NOT: to serve as "the reverb sounds correct."
- Numeric tests are primary: spec-checklist coverage (every documented behavior has a test), register-level unit tests, witness diff RMS/peak deltas, golden-file diffs.
- Preset audition is a final sanity check at milestone sign-off — documented in DECISIONS.md as "auditioned, agreed with hardware capture / lv2-psx-reverb to within [tolerance], signed off."
- Never ship a release because "it sounds right." Ship because the numeric tests pass AND it sounds right.

**Phase:** P-PRESETS, P-WITNESS, P-CLI (golden-file)

**Source:** Inferred from general audio-regression-testing wisdom; [Audio DiffMaker](https://www.libinst.com/Audio%20DiffMaker.htm) and similar tools exist precisely because ear-test isn't sufficient.

---

## Category 8 — Hardware-Port Foresight Pitfalls (from M1, looking toward M5+)

### 8.1 Dynamic allocation in the core

**Severity:** Catastrophic for MCU port (cross-compile may fail; audio may glitch)

**What goes wrong:** `malloc`/`free` in the core makes MCU port impossible (no heap, or statically sized heap) and risks RT-violations on desktop. Even a single `malloc` in an init function is a problem if re-init happens on the audio thread.

**Prevention:** Already in PROJECT.md constraints: no heap in hot path. M1-level rule: **no heap, period, in `libspu94/src/`**. All state is caller-allocated (`spu94_state_size()` returns the required byte count; caller provides `spu94_init(void *memory, size_t len, ...)`). CI lint on `malloc|calloc|realloc|free|mmap|brk` yields zero hits.

**Phase:** P-API, P-CROSS

**Source:** Standard embedded-C practice; [Daisy seed examples](https://github.com/electro-smith/libDaisy)

---

### 8.2 `long` and `int` are platform-dependent

**Severity:** Significant

**What goes wrong:** On Linux x86_64, `long` is 64-bit; on Cortex-M, `long` is 32-bit. Code that packs something into `long` and expects 64 bits silently loses half the bits on cross-compile. `int` is 32-bit on both major targets (but not guaranteed — `int` can be 16-bit in embedded systems historically).

**Prevention:**
- **Ban `long`, `int`, `unsigned`, etc. in all public APIs and hot paths.** Use `int16_t`, `int32_t`, `int64_t`, `uint16_t`, `uint32_t`, `uint64_t`, `size_t`, `ptrdiff_t` exclusively.
- CI check: grep for `\b(long|int|short|unsigned|signed)\b` (with exceptions for `const`, `static`, etc.) — flag any non-stdint usage.
- Cortex-M smoke test (P-CROSS) compiles with `-Wall -Wextra -Wconversion -Werror`.

**Phase:** P-API, P-CROSS

**Source:** [ARM Cortex-M ABI](https://developer.arm.com/documentation); [CMSIS-DSP uses C99 fixed-width types](https://github.com/ARM-software/CMSIS-DSP); general embedded-C wisdom

---

### 8.3 Unaligned memory access

**Severity:** Significant (Cortex-M0 faults; M3/M4 slow)

**What goes wrong:** Desktop x86 handles unaligned access silently (penalty, but no fault). Cortex-M0 hardfaults on unaligned load/store. If the reverb work buffer is exposed as an `int16_t*` but sized/offset such that the pointer lands on an odd byte, SPU-94 crashes on M0 but runs on M4 and on Linux.

**Prevention:**
- Work buffer is `int16_t[N]` (static array) — compiler guarantees alignment.
- No casts from `void*` or `uint8_t*` to `int16_t*` without an alignment assertion.
- Cross-compile flags include `-mno-unaligned-access` or the equivalent for the target; clean build is the test.

**Phase:** P-NETWORK (buffer definition), P-CROSS

**Source:** [ARM Cortex-M0 reference manual](https://developer.arm.com/documentation/ddi0419/latest/); general embedded-C wisdom

---

### 8.4 Recursive functions and large stack frames

**Severity:** Significant (silent corruption on MCU)

**What goes wrong:** Cortex-M typically has 4–32 KB of stack; recursion or large stack-allocated arrays (e.g., `int32_t scratch[8192]`) blow the stack without warning, corrupting adjacent heap/data.

**Prevention:**
- Zero recursive functions in the core.
- Large buffers allocated in `spu94_state` (caller-provided); never `int32_t scratch[N]` in a function body where N > 64 or so.
- Cross-compile with `-Wstack-usage=256` (or similar) to flag any function with large stack frames.

**Phase:** P-NETWORK, P-CROSS

**Source:** Embedded-C norms; Cortex-M startup code conventions

---

### 8.5 libc dependencies that aren't in newlib-nano

**Severity:** Significant (cross-compile link failure)

**What goes wrong:** `printf("%f", ...)` pulls in the full float-formatting paths; `sin()` / `exp()` pull in libm; `fopen` is absent on bare-metal. Innocuous inclusion of `<stdio.h>` for debug becomes an MCU link error.

**Prevention:**
- Core C library uses only `<stdint.h>`, `<stddef.h>`, optionally `<string.h>` for `memset`/`memcpy` (which newlib-nano provides).
- No `<stdio.h>`, `<math.h>`, `<stdlib.h>` (except `<stdlib.h>` for `size_t`? — no, that's in `<stddef.h>`).
- CLI / Python binding may use anything; they are not cross-compiled.
- Cross-compile smoke test (P-CROSS) link-errors out if any forbidden symbol sneaks in.

**Phase:** P-CROSS, P-API

**Source:** [newlib-nano docs](https://sourceware.org/newlib/); [ARM embedded toolchain](https://developer.arm.com/Tools%20and%20Software/GNU%20Toolchain)

---

### 8.6 `size_t` vs `uint32_t` on 32-bit targets

**Severity:** Nuisance

**What goes wrong:** Public API takes `size_t` for sample counts. On 32-bit MCU, `size_t` is 32-bit; on 64-bit desktop, 64-bit. This is fine for sizes (both exceed 2^31 samples = 13 hours of audio) but not fine for printf format strings or struct-layout assumptions.

**Prevention:** Use `size_t` for counts in the API (idiomatic). Use `%zu` in printf (CLI only). Don't store `size_t` in a struct with strict layout — use `uint32_t` for explicit-size fields.

**Phase:** P-API

**Source:** C99 standard

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Use `float` for "just one" coefficient | Easier arithmetic | Silently loses bit-accuracy; CI guard becomes selective | Never in core; OK at API boundary (rate converter) |
| Skip the 22050 downsample | Simpler hot loop | Brightens reverb tail; permanent divergence from hardware; project's core claim undermined | Never (this is SPU-94's differentiator vs lv2-psx-reverb) |
| Mutex around register array | "Easy" thread safety for mid-stream writes | RT-unsafe; kills MCU port | Never; use atomics or SPSC ring |
| Inline witness source "just to check" | Resolves an ambiguity fast | Derivative-work risk; license constraint; DECISIONS.md integrity | Never as primary activity; logged exception only |
| `malloc` in `spu94_init` | Natural C idiom for state allocation | Kills MCU port | Never — use caller-allocated state |
| Coefficient smoothing in core | "Glitch-free" modulation out of the box | Bit-accuracy silently broken | Never in core; M4 lever layer only |
| `printf` for debug in core | Fast debugging | Kills MCU build; drags in libc | Only behind `#ifdef DEBUG_DESKTOP` + excluded from MCU build |
| Skip golden-file sign-off protocol | Ship faster | Goldens pin to bugs; regeneration crises | Never — protocol takes 10 min, saves days |

---

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| nocash psx-spx as spec | Treat as definitive and comprehensive | Treat as primary, cross-reference with hitmen.c02.at; paraphrase, don't transcribe |
| lv2-psx-reverb as witness | Assume agreement means correctness | Use for *behavioral* diff only; know its deviations (22050 half-rate, float coefficients, memset on preset switch); never as a clean-room source |
| numpy arrays across ctypes FFI | Pass expressions (GC risk) | Bind to named local, use `.ctypes.data_as()`, test with faulthandler enabled |
| WAV I/O | Let WAV lib allocate inside per-sample loop | Allocate once outside the loop; pass in pre-sized buffers |
| JUCE (future M4) | Let the plugin wrapper call `spu94_init` from the audio thread | `spu94_init` on the control thread; `spu94_process_block` only on the audio thread |
| Cortex-M DMA (future M5+) | Assume buffers are aligned | Explicit align attribute on the buffer struct; runtime assert in `spu94_init` |

---

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Allocating inside `process_block` | Dropouts under RT load | Caller-allocated state and buffers | At first JUCE integration (M4) or MCU port (future) |
| Mutex on register array | Occasional dropouts under parameter automation | Atomics or SPSC ring buffer | Under sustained automation/modulation |
| GIL blocking modulation thread | Control-write latency correlates with block size | Small blocks, or C-side test entry point | Modulation test harness (P-MODTEST) with write rates > 1/block |
| Running core at 44.1 kHz instead of 22.05 kHz | Slightly faster processing, but wrong sound | Explicit half-rate path | M1 witness diff vs hardware capture (if captured); otherwise caught at M5 |
| Cache-unfriendly buffer layout | Minor MCU-side perf issues | Keep work buffer as a single contiguous `int16_t` array | MCU port, where the buffer may not fit in tightly-coupled memory |

---

## Security Mistakes

(SPU-94 is a local audio library; traditional network/auth security is not relevant. Safety-adjacent concerns:)

| Mistake | Risk | Prevention |
|---------|------|------------|
| Buffer overflow on register write | Corrupt state; exploit via crafted preset file | Bounds-check register index in `spu94_write_register` |
| Integer overflow in size math | Heap / stack overflow via oversized `n_samples` | `size_t` checks on block size; cap at a sensible maximum |
| Reading preset from untrusted source | Denial-of-service via pathological register values | Validate register values against documented ranges; reject NaN coefficients; clamp address registers to SPU RAM size |

---

## UX Pitfalls (Library / API Users)

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| API takes `uint16_t` for signed coefficients | User writes `0x8000` expecting -32768; gets +32768 or undefined | Separate signed-volume and unsigned-address register accessors |
| "Helpful" smoothing always on | User who wants bit-accurate diff gets smoothed output and can't tell why | Core is raw; smoothing is opt-in at a higher layer |
| Silent sample-rate conversion | User passes 48 kHz input, expects 48 kHz bit-accurate output, gets resampled | Explicit `spu94_set_host_rate()`; resampling is documented as outside the bit-accurate core |
| Unclear L/R buffer convention | User passes interleaved `L,R,L,R` where API wants separate `L[]`, `R[]` | API explicitly takes two separate `int16_t*` (matches hardware's L-half/R-half buffer model) |

---

## "Looks Done But Isn't" Checklist

- [ ] **24 registers implemented:** Often missing the vIIR = -8000h special case — verify with dedicated test (Pitfall 1.1).
- [ ] **Fixed-point math:** Often quietly rounding when it should truncate — verify with exhaustive q15_mul test (Pitfall 1.4).
- [ ] **Saturation:** Often wrapping when it should saturate, OR saturating when it should wrap (address math) — verify with separate helpers and negative-value test vectors (Pitfall 1.5).
- [ ] **22050 half-rate:** Often skipped entirely — verify with spectral test vs hardware capture or by comparing to a known-half-rate witness (Pitfall 1.6).
- [ ] **Work buffer wrap:** Often off-by-one at the mBASE or 0x7FFFE boundary — verify with fuzz test at boundary (Pitfall 1.7).
- [ ] **"Reverb off" preset:** Often produces non-silence due to zero-offset aliasing — verify explicit silence test (Pitfall 1.8).
- [ ] **Mid-stream write glitch-freedom:** Often means "no crashes" but not "no buffer corruption" — verify with sustained fast-modulation stress test (Pitfall 4.1).
- [ ] **No allocations in hot path:** Often missed by CI because `malloc` hides in included stdlib functions — verify with `-Wl,--wrap=malloc` or `ldd` symbol audit (Pitfall 3.1, 8.1).
- [ ] **Python binding memory safety:** Often missing GC-reference checks — verify with stress test + `faulthandler` + valgrind (Pitfall 5.1).
- [ ] **DECISIONS.md coverage:** Often claim "documented all gray areas" but many implicit decisions are uncaptured — verify by reviewer asking "where is this decision recorded?" for every non-obvious code path.
- [ ] **Witness-vs-spec-vs-ear separation:** Often blurred. Verify that every DECISIONS.md entry explicitly states which basis drove the choice (Pitfall 7.3).
- [ ] **Trademark-clean naming:** Often "PSX" or "PS1" leaks into package/binary names — verify `grep -ri 'psx\|ps1' libspu94/ --exclude-dir=.git` finds only internal comments, never user-facing identifiers (Pitfall 6.3).
- [ ] **Cross-compile smoke test:** Often "builds" but never "links" — verify that the Cortex-M build actually produces an `.elf` with `arm-none-eabi-size` showing the full core, not just stubs.

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Truncation vs rounding error found late | MEDIUM | Isolate via q15_mul audit; replace; re-run all golden + witness tests; regenerate goldens with DECISIONS entry |
| Float creep discovered | HIGH | Grep for `float\|double` in core; rewrite affected stages; new golden files; this is the "relicense to GPL" scenario if discovered after public release |
| Derivative-work claim | CATASTROPHIC | Options: relicense to match witness's GPL; or rewrite affected portions clean-room with a second (non-tainted) implementer. Document. |
| mBASE wrap crash in production | MEDIUM | Reproduce with fuzz test; fix; regression test on boundary cases |
| Witness diff suddenly diverges on all presets | MEDIUM | Bisect git history; check for accidental float creep or reorder; often a single-line change |
| Scope creep threatens M1 shipment | LOW-MEDIUM | Freeze feature set at M1 checkpoint; move non-M1 work to FUTURE.md; ship what's done |
| DECISIONS.md consensus drifted from code | MEDIUM | Audit each entry; update code or update document; the truth is in the code — the doc follows |

---

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| 1.1 vIIR = -8000h | P-REG | Unit test on vIIR with -8000h input |
| 1.2 Signedness | P-REG, P-API | Type system (`int16_t` in API); round-trip test |
| 1.3 Q15 vs Q16 fractional point | P-FIXPOINT | Exhaustive `q15_mul` test with known table |
| 1.4 Truncation vs rounding | P-FIXPOINT | Dedicated truncation test incl. negative values |
| 1.5 Saturation vs wrap | P-FIXPOINT, P-NETWORK | Separate helpers + overflow test vectors |
| 1.6 22050 half-rate | P-NETWORK, P-WITNESS, P-DECISIONS | Internal 22050 path; hardware capture comparison at M5 |
| 1.7 Work buffer wrap | P-NETWORK | Fuzz test at mBASE and 0x7FFFE boundaries |
| 1.8 Zero-register semantics | P-REG, P-PRESETS | "Reverb off" preset silence test |
| 1.9 Input gain path | P-NETWORK, P-DECISIONS | Signal-flow diagram review + per-register unit tests |
| 1.10 Operation reordering | P-NETWORK | Implementation matches nocash formula line-by-line |
| 2.1 Float creep | P-FIXPOINT, P-NETWORK, CI | Grep enforcement on `float\|double` in core |
| 2.2 Endian / bit-order | P-PRESETS, P-CLI, P-CROSS | Explicit `read_u16_le` helper; no raw struct reads |
| 2.3 Signed right-shift UB | P-FIXPOINT, P-CROSS | `_Static_assert` on arithmetic shift behavior |
| 2.4 Witness diff sample offset | P-WITNESS | Cross-correlation alignment + report lag |
| 2.5 Goldens pin to buggy state | P-CLI, P-DECISIONS | Sign-off protocol + companion `.sig.md` per golden |
| 3.1 Allocations in hot path | P-API, P-PYBIND, P-CLI | CI grep on `malloc`-family; caller-allocated state |
| 3.2 Locks in hot path | P-API, P-MODTEST, P-DECISIONS | Atomics / SPSC ring; concurrency doc |
| 3.3 Callback-vs-pull API | P-API | Block-processing pull primitive |
| 3.4 Smoothing in core | P-API, P-MODTEST, P-LEVERS | Smoothing is M4; core is raw |
| 4.1 Delay-length change policy | P-API, P-MODTEST, P-DECISIONS | Documented policy + stress test |
| 4.2 Coefficient zipper | P-MODTEST, P-LEVERS | Observe, document, do not "fix" in M1 |
| 4.3 Writes between L and R | P-API, P-NETWORK, P-MODTEST | Documented convention + sub-sample-offset write test |
| 5.1 numpy GC / ctypes | P-PYBIND | Stress test with `faulthandler` / valgrind |
| 5.2 ctypes GIL | P-PYBIND, P-MODTEST | Small blocks for mod test; document limitation |
| 5.3 Struct layout mismatch | P-API, P-PYBIND | Opaque-pointer API; round-trip getter test |
| 5.4 glibc / ABI | P-API, P-CROSS | `-fvisibility=hidden`; reproducible build |
| 6.1 Derivative work | P-DECISIONS | No-read policy; consultation log |
| 6.2 nocash copyright | P-DECISIONS, P-PRESETS | Paraphrase spec; cross-reference multiple sources |
| 6.3 Trademark | P-API, P-CLI, P-DECISIONS | Package name is `libspu94`; no "psx"/"ps1" in user-facing identifiers |
| 6.4 Solo clean-room defense | P-DECISIONS | Git log + DECISIONS.md as contemporaneous record |
| 7.1 Scope creep | project-wide | Out-of-Scope list is contractual |
| 7.2 DECISIONS.md dumping ground | P-DECISIONS | Template + monthly audit |
| 7.3 Witness-chasing | P-WITNESS, P-DECISIONS | Process rule: no code change before DECISIONS.md entry |
| 7.4 Preset-audition false confidence | P-PRESETS, P-WITNESS, P-CLI | Numeric tests primary; audition is final sanity |
| 8.1 Dynamic allocation | P-API, P-CROSS | Caller-allocated state; CI grep |
| 8.2 `long`/`int` sizes | P-API, P-CROSS | Stdint-only; `-Wconversion` |
| 8.3 Unaligned access | P-NETWORK, P-CROSS | Static `int16_t` buffer; alignment asserts |
| 8.4 Recursion / stack frames | P-NETWORK, P-CROSS | `-Wstack-usage=256` |
| 8.5 libc dependencies | P-CROSS, P-API | `<stdint.h>` + `<stddef.h>` + `<string.h>` only in core |
| 8.6 `size_t` portability | P-API | Use stdint for on-disk/ABI; `size_t` for counts only |

---

## Sources

### Primary (HIGH confidence — documentation of facts)
- [psx-spx Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) — canonical spec reference; caveats in Pitfall 6.2
- [problemkaputt.de psx-spx.txt](https://problemkaputt.de/psx-spx.txt) — nocash original
- [hitmen.c02.at PSX SPU docs](https://hitmen.c02.at/files/docs/psx/spu.txt) — secondary cross-reference

### Witness projects (observable behavior only — source not read as primary activity)
- [lv2-psx-reverb](https://github.com/ipatix/lv2-psx-reverb) — README and architectural posture only; GPLv3
- [Mednafen ChangeLog](https://mednafen.github.io/documentation/ChangeLog.txt) — Nov 2020 reverb precision fix
- [jsgroth's PS1 SPU series](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) — implementation commentary
- [DuckStation SPU commit using Mednafen formula](https://github.com/stenzek/duckstation/commit/809b9f89ca0a24934ffa13c7901345ed0aa82eeb) — shows Mednafen's formula as de facto reference

### DSP / audio (MEDIUM confidence — general wisdom)
- [Valhalla DSP on algorithmic reverbs](https://valhalladsp.com/2011/07/07/algorithmic-reverbs-distortion-and-noise/)
- [Valhalla DSP on parameter smoothing](https://valhalladsp.com/2010/11/27/valhallashimmer-the-controls/)
- [CCRMA on delay-line interpolation](https://ccrma.stanford.edu/~jos/pasp06/Delay_Line_Interpolation.html)
- [KVR zipper-noise thread](https://www.kvraudio.com/forum/viewtopic.php?t=42849)
- [Shawn's DSP tutorials on Q15 rounding](https://sestevenson.wordpress.com/2009/08/19/rounding-in-fixed-point-number-conversions/)
- [dsprelated on truncation and saturation](https://www.dsprelated.com/showthread/comp.dsp/78628-1.php)

### Python / ctypes / numpy (MEDIUM confidence)
- [Python ctypes docs](https://docs.python.org/3/library/ctypes.html)
- [NumPy "Python as glue"](https://numpy.org/devdocs/user/c-info.python-as-glue.html)
- [numpy ctypes memory safety GH#28238](https://github.com/numpy/numpy/issues/28238)
- [Python bug tracker — ctypes dangling pointer](https://bugs.python.org/issue41883)

### Embedded / C portability (MEDIUM confidence)
- [GCC visibility wiki](https://gcc.gnu.org/wiki/Visibility)
- [CMSIS-DSP on fixed-width types](https://github.com/ARM-software/CMSIS-DSP)
- [Rupt/c-arithmetic-right-shift (portable shift)](https://github.com/Rupt/c-arithmetic-right-shift)
- [Autoconf on signed shift](https://lists.gnu.org/archive/html/autoconf-patches/2001-08/msg00104.html)

### Legal (MEDIUM confidence — lay summaries; consult counsel for decisions)
- [Wikipedia — Clean-room design](https://en.wikipedia.org/wiki/Clean_room_design)
- [RetroReversing — clean-room reversing](https://www.retroreversing.com/clean-room-reversing)
- [Sony v. Connectix](https://en.wikipedia.org/wiki/Sony_Computer_Entertainment,_Inc._v._Connectix_Corp.)
- [Lexology — IP clean room policy](https://www.lexology.com/library/detail.aspx?g=57ce5c16-717f-4fe0-9925-30628c54085c)
- [psx-spx GitHub README — admitted copyright ambiguity](https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/index.md)

### Project management (LOW-MEDIUM confidence — general wisdom)
- [atd.org — scope creep](https://www.td.org/content/atd-blog/scope-creep-the-allure-of-just-one-more-thing)
- [Michael Nygard — ADR](https://www.cognitect.com/blog/2011/11/15/documenting-architecture-decisions)
- [Audio DiffMaker](https://www.libinst.com/Audio%20DiffMaker.htm) — tooling precedent for audio diff

---

*Pitfalls research for: SPU-94 — PS1 SPU reverb bit-faithful reimplementation*
*Researched: 2026-04-18*
