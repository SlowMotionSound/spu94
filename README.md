# SPU-94

SPU-94 is a bit-faithful software reimplementation of the PlayStation 1 SPU reverb, built from the published hardware spec rather than ported from any existing emulator. It ships as a plain C library with ctypes-based Python bindings and a small `spu94` command-line tool that renders WAV files through any of the ten factory presets.

The sound it produces is the PS1's: Q15 fixed-point truncation at every multiply, a four-stage filter network running at 22.05 kHz behind a 39-tap half-band FIR at each sample-rate boundary, and a documented hardware quirk where setting vIIR to the most-negative int16 inverts the reverb output. Those details are what give the original its character — SPU-94 preserves them intact.

Built for recording and broadcast engineers who want the PS1 reverb as a modern, playable tool. Sample-accurate where the spec is explicit. Documented and deliberate where it isn't.

---

## Current state

**Milestone 1 — April 2026**

Shipping today:

- The reverb network, the four-stage filter chain, the 39-tap half-band FIR at both sample-rate boundaries, the mix-bus hard clip, and all ten factory presets — bit-tested against a one-million-step mid-stream fuzz harness
- The Python binding (`import spu94`), the SPU94 class for ergonomic use, and the `spu94` command-line tool
- Linux wheel via scikit-build-core + cibuildwheel (manylinux_2_28, Python 3.10+)

Upcoming, not yet in this release:

- Witness-diff verification against lv2-psx-reverb (Phase 7)
- Golden-file regression tests for each preset (Phase 7)
- MCU portability proof via Cortex-M7 cross-compile (Phase 8)
- A JUCE plugin wrapper with named musical levers (Milestone 4)
- Eurorack hardware (Milestone 5)

## Quick install

Install the wheel (Linux x86_64, glibc 2.28 or newer):

```bash
pip install spu94
```

Or build from source (editable install — source edits to the Python side go live without reinstall):

```bash
git clone https://github.com/anthonyaccurso/spu94
cd spu94
pip install -e .
```

Or build the C library and CLI directly without Python:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Either path gives you the same `spu94` command on your PATH and the same `import spu94` in Python.

## Python walkthrough

Two surfaces: the raw-panel functions (state handle passed explicitly, matches the C side 1:1) and the `SPU94` class (handle-owning sugar over the raw layer). Pick the one that fits your workflow — both are public and documented, and the class is a thin wrapper, never a second implementation.

The raw panel:

```python
import numpy as np
import spu94

state = spu94.init(work_buf_size=8192)
try:
    spu94.load_preset(state, "hall")
    spu94.tick(state)

    n = 44100  # one second at 44.1 kHz
    L_in  = np.zeros(n, dtype=np.int16)
    R_in  = np.zeros(n, dtype=np.int16)
    L_out = np.zeros(n, dtype=np.int16)
    R_out = np.zeros(n, dtype=np.int16)

    spu94.process(state, L_in, R_in, L_out, R_out)

    # Drain the reverb tail after the input ends:
    spu94.flush(state, L_out, R_out)
finally:
    spu94.destroy(state)
```

The class, with context-manager handling:

```python
import numpy as np
import spu94

with spu94.SPU94() as rev:
    rev.load_preset("hall")
    rev.tick()

    n = 44100
    L_in  = np.zeros(n, dtype=np.int16)
    R_in  = np.zeros(n, dtype=np.int16)
    L_out = np.zeros(n, dtype=np.int16)
    R_out = np.zeros(n, dtype=np.int16)

    rev.process(L_in, R_in, L_out, R_out)
    rev.flush(L_out, R_out)
```

A note on input format: `spu94.process` accepts int16 C-contiguous numpy arrays and nothing else. Passing float32 raises a clear TypeError that includes the exact conversion recipe — `(arr * 32767).clip(-32768, 32767).astype(np.int16)`. This is deliberate. The PS1 SPU had no format-conversion layer because it had no non-int16 audio to convert from. Everything in the hardware is 16-bit signed integer end-to-end. A forgiving binding that silently converted would add a layer the original never had — exactly the kind of helpful magic the rest of the project avoids. Strict is *more* faithful, not less.

Every one of the thirty-five reverb-affecting registers is readable and writable at any time — during playback, between blocks, as often as you want. `rev.set_reg(spu94.Register.vIIR, -0x8000)` on a live state invokes the documented hardware anomaly described below; `rev.snapshot()` returns all thirty-five values as a tuple for inspection or preset diffing.

## CLI walkthrough

Render a WAV file through the Hall preset:

```
spu94 --preset hall input.wav output.wav
```

Append two seconds of reverb tail after the input ends:

```
spu94 --preset hall --tail-seconds 2 input.wav output.wav
```

Apply an override JSON — use a preset as the base, change just a few registers:

```json
{
  "base": "hall",
  "overrides": {
    "vIIR":     -8000,
    "mLCOMB1":  "0x1000"
  }
}
```

```
spu94 --config my_override.json input.wav output.wav
```

Values in `--config` JSON accept either integers or hex strings (`"0x3F00"`, `"-0x8000"`). Register names match the canonical names from the C API — see `spu94 --list-presets` for the preset side, and the Python `Register` IntEnum for the register side.

For exact register specification (flat-map form, useful for golden-file reproduction):

```json
{
  "vLOUT": 16384,
  "vROUT": 16384,
  "mBASE": 16128,
  "...32 more entries": "..."
}
```

When every register is specified, the preset system is bypassed entirely — the register values you provide are written atomically.

List the factory presets:

```
spu94 --list-presets
```

Get help:

```
spu94 --help
```

All errors exit non-zero with a single-line stderr message prefixed `spu94: error:`. No tracebacks, no multi-line diagnostics. Examples:

```
spu94: error: unknown preset 'hll' — valid: off, room, studio_a, studio_b, studio_c, hall, half_echo, space_echo, echo, delay
spu94: error: input file '/tmp/missing.wav' not found
spu94: error: WAV file '/tmp/mono.wav' has 1 channels; stereo (2 channels) required
```

## For the DSP-curious

If you've read this far you probably want the technical story. Four details do most of the work.

**Q15 truncation is the character.** Every multiply in the reverb network is a Q15 fixed-point multiply with truncation toward zero, not rounding. That consistent bias — one direction, every sample — is a subtle asymmetry the ear reads as vintage. Modern DSP rounds; the PS1 truncates. Swapping one for the other changes the sound measurably, and swapping is exactly what most port-from-spec implementations silently do. SPU-94 truncates. The rationale is recorded in `docs/DECISIONS.md` as ADR-0001.

**The 39-tap half-band FIR at both sample-rate boundaries.** The SPU's reverb network runs internally at 22.05 kHz. Every sample that arrives at the 44.1 kHz input boundary passes through a 39-tap half-band decimating FIR; every sample leaving passes through the matching interpolator. Omitting this filter — as some open-source emulators do by design — makes the reverb audibly brighter than hardware. SPU-94 reproduces Sony's coefficients verbatim in integer arithmetic. Latency is 58 samples total (one-way group delay), documented in `spu94_get_latency_samples()` and pinned across every release by a regression gate.

**The vIIR anomaly.** Writing the value `0x8000` (the most negative int16) to the `vIIR` register causes the reverb to negate its output. It's a hardware quirk, not a bug. The spec documents it; the hardware does it; SPU-94 reproduces it. If you set vIIR to -32768 and hear your reverb flip polarity, that's correct.

**From spec, not ported.** SPU-94's algorithm was written from nocash's published register documentation and the archived Sony PSX SDK, not translated from Mednafen, lv2-psx-reverb, DuckStation, or MiSTer. Those implementations are consulted as behavioral witnesses when the spec is ambiguous — their output audio is compared against SPU-94's output — but their source code is not read as a primary development activity. This preserves licensing flexibility and gives every gray-area resolution its own defensible rationale in `docs/DECISIONS.md`.

Every choice above is recorded with full context in `docs/DECISIONS.md`. The ADRs are numbered and cross-linked; start at ADR-0001 if you want the whole story.

## Roadmap

- **Milestone 1 (this release):** Reverb network + mix-bus clip + factory presets + Python binding + CLI + wheel + README
- **Milestone 2:** 4-bit Sony ADPCM encode/decode for the full sample-playback path
- **Milestone 3:** DAC reconstruction modeling (period-appropriate CXD2562Q / CXD2925Q coloration in DSP)
- **Milestone 4:** JUCE plugin (VST3, AU, LV2, Standalone) with named musical levers (Room Size, Pre Delay, Decay, Diffusion, Damping), parameter smoothing, and CV-input mappings ready for Eurorack
- **Milestone 5:** Hardware validation via an original PS1 console with a digital capture path, plus a Eurorack module PCB with period-appropriate DAC hardware

## Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│  Caller (CLI / Python user / future JUCE plugin)            │
└──────────────────────────┬──────────────────────────────────┘
                           │  int16 stereo @ 44.1 kHz
                           ▼
                 ┌──────────────────┐
                 │  spu94_process() │  block-based, in-place legal
                 └────────┬─────────┘
                          │
        per sample:       │
                          ▼
                 ┌──────────────────┐
                 │  39-tap FIR      │  44.1 → 22.05 kHz
                 │  (decimator)     │
                 └────────┬─────────┘
                          │
                          ▼
                 ┌──────────────────┐
                 │   spu94_tick()   │  per 22.05 kHz tick
                 │                  │
                 │  apply pending → │  tick-latched writes commit
                 │  reverb body:    │  SAME IIR → DIFF IIR →
                 │                  │  4-tap comb → APF1 → APF2
                 │  buffer advance  │  MAX(mBASE, (ba+2) & 0x7FFFE)
                 └────────┬─────────┘
                          │
                          ▼
                 ┌──────────────────┐
                 │  39-tap FIR      │  22.05 → 44.1 kHz
                 │  (interpolator)  │
                 └────────┬─────────┘
                          │
                          ▼
                 ┌──────────────────┐
                 │  hard clip stage │  mix-bus saturation to ±0x7FFF
                 └────────┬─────────┘
                          │  int16 stereo @ 44.1 kHz
                          ▼
                      (caller receives)
```

State lives in a caller-allocated buffer — the library never touches the heap during audio processing. Thirty-five registers control everything: twelve gain registers (vIIR, vCOMB1-4, vWALL, vAPF1/2, vLIN, vRIN, vLOUT, vROUT) take effect immediately; twenty-two address and delay registers latch at the next tick; mBASE is immediate with a documented snap-on-write side effect. The split write-timing policy is what lets every parameter be modulated cleanly during playback — no zipper noise, no glitch on sample zero.

## Licensing posture

SPU-94 is original work. The algorithm was written from the published hardware spec (nocash psx-spx, the archived Sony PSX SDK documentation). Source code from the GPL-licensed emulator projects — Mednafen, lv2-psx-reverb, DuckStation, MiSTer — is not read as a primary development activity. When one of those implementations is consulted to resolve a specific ambiguity, the consultation is logged in `docs/DECISIONS.md` with the specific question, the behavior observed, and the resolution chosen.

Vendored third-party code is limited to two single-header libraries, both under permissive licenses:

- **dr_wav** (`vendor/dr_wav/dr_wav.h`, public domain / MIT-0 dual-licensed) — WAV file I/O for the CLI binary only, never linked into `libspu94.so`
- **jsmn** (`vendor/jsmn/jsmn.h`, MIT) — zero-allocation JSON tokenizer for the CLI's `--config` parser, CLI binary only

Both include their upstream LICENSE files preserved verbatim under `vendor/dr_wav/LICENSE` and `vendor/jsmn/LICENSE`. A permanent regression gate (`scripts/ci/verify-no-drwav-in-libspu94.sh`) asserts `nm -D libspu94.so` reports zero `drwav_` or `jsmn_` symbols — the vendored libraries stay bound to the CLI target exactly.

The final permissive-license pick for SPU-94 itself — MIT or Apache-2.0 — is deferred to the end of Milestone 1. The `LICENSE` file at the repository root is a placeholder explaining this decision. When the pick is made, flipping the label is a LICENSE-file edit; no pyproject or source changes are required.

nocash's psx-spx documentation is a factual reference — register layouts, coefficient tables, algorithmic behavior. SPU-94 paraphrases facts in its own documentation and cites the specific nocash section via a bibliography entry; it does not transcribe nocash prose. This discipline is explained in more detail in `docs/BIBLIOGRAPHY.md` (populated in Phase 7).

## Acknowledgments

Primary sources consulted:

- **nocash psx-spx** (problemkaputt.de / psx-spx.consoledev.net) — the authoritative hardware spec
- **The archived Sony PlayStation SDK documentation** — cross-verification of register addresses and preset values
- **hitmen c02 SPU reference** — independent second source for preset values; used as the tiebreaker during the preset-audit

Vendored libraries:

- **dr_wav** by David Reid (github.com/mackron/dr_libs) — public domain / MIT-0
- **jsmn** by Serge Zaitsev (github.com/zserge/jsmn) — MIT

Behavioral witnesses (consulted for audio output, not source code):

- **Mednafen** — GPLv2
- **lv2-psx-reverb** — GPLv3 (excluded for frequency-response-accuracy questions; its README acknowledges the 39-tap FIR is omitted by design)
- **DuckStation**
- **MiSTer FPGA PSX core**

## Contributing

Development workflow:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Python-binding tests:

```bash
pip install -e .
pytest tests/python/binding
```

The repository uses Architecture Decision Records for gray-area resolutions — `docs/DECISIONS.md` is a first-class deliverable. A good pull request includes:

- Tests that exercise the new behavior (C Unity tests under `tests/unit/`, Python pytest tests under `tests/python/binding/` or `tests/cli/`)
- An ADR entry for any decision that involves choice among alternatives, especially where the spec is silent
- Updates to any section of the README that drifts out of date

Licensing note for contributors: SPU-94 is original work per the posture above. Please do not submit code derived from reading GPL-licensed emulator sources — paraphrased observations are fine when clearly attributed in `docs/DECISIONS.md`; line-by-line translations are not.

---

Thanks for reading. If you build something with SPU-94, I'd love to hear about it.
