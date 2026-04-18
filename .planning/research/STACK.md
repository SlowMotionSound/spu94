# Stack Research — SPU-94 (Milestone 1)

**Domain:** Real-time DSP C library, bit-faithful algorithm reimplementation, multi-target (Linux host + Cortex-M MCU) portability.
**Researched:** 2026-04-18
**Overall confidence:** MEDIUM-HIGH — most decisions verified against primary sources; some tradeoffs (Meson vs CMake, cppcheck vs clang-tidy) are judgment calls informed by the project's specific constraints.

**Scope note:** This document takes the locked-in decisions (plain C, ctypes, Python+numpy+scipy+matplotlib+pytest, JUCE-later, MIT-or-Apache) as given. It fills in concrete versions, flags, plugins, and the surrounding machinery.

---

## 1. C Standard and Toolchain

### Recommendation

**C11** (`-std=c11 -pedantic`) for the core library. **Confidence: HIGH.**

### Rationale

- **C99 is too conservative for 2026.** All three toolchains in scope (GCC on Linux, Clang on Linux, `arm-none-eabi-gcc` for Daisy) have supported C11 fully for a decade. No MCU toolchain in active use rejects C11.
- **C11 buys specific features SPU-94 actually wants:**
  - `_Static_assert` — compile-time checks on fixed-point widths, struct sizes, endianness. Critical for a bit-accurate library.
  - `_Alignas` / `alignof` — explicit alignment for SIMD-friendly work buffer, and for Cortex-M DMA alignment later.
  - `<stdnoreturn.h>`, `<stdatomic.h>` (optional, guarded) — useful even though the hot path is single-threaded; atomics let the future M4 plugin parameter-exchange layer be portable.
  - Anonymous structs/unions — cleaner register-file representation.
- **C17 is "C11 with defect reports fixed" and adds no new features** (per WG14). Choosing C17 costs nothing over C11 but gains only bug-report clarity; the practical difference is zero. Using `-std=c11` is the more widely documented, more widely toolchain-supported baseline.
- **C23 is off the table for M1.** `arm-none-eabi-gcc 13.x` (the version most Daisy users have installed) only partially supports C23. Not worth the portability risk.

**Why not C99:** Static assertions would have to go through `typedef char assert_[cond ? 1 : -1]` hacks, and anonymous structs are a GNU extension. Every line of that noise is a portability liability for FPGA HLS later.

**Why not C++:** Already locked out by project constraints. C++ on MCUs means fighting `-fno-exceptions -fno-rtti` religiously; plain C sidesteps the fight entirely.

### Compiler Flags

**Host baseline (GCC/Clang on Linux):**
```
-std=c11 -pedantic
-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
-Wstrict-prototypes -Wmissing-prototypes
-Wcast-align -Wcast-qual -Wpointer-arith
-Wvla                      # VLAs are real-time hostile; forbid them
-Werror                    # CI only; local dev can override
-fno-common                # Catch ODR violations
-fvisibility=hidden        # Export only what the public header declares
```

**Debug + sanitizers (CI + local dev):**
```
-O0 -g3 -fno-omit-frame-pointer
-fsanitize=address,undefined
-fsanitize=integer          # Clang only; catches truncation surprises (useful AND ironic for a truncation-faithful library — see Pitfalls)
```

**Release (host):**
```
-O2                        # NOT -O3; -O2 is the stable, well-tested point; -O3 can trigger autovectorization surprises
-DNDEBUG
-ffp-contract=off          # CRITICAL for determinism — see question 9
-fno-fast-math             # Never fast-math a library claiming bit accuracy
```

**Cross-compile (arm-none-eabi-gcc, Daisy Seed = Cortex-M7 + FPU):**
```
-mcpu=cortex-m7 -mthumb
-mfpu=fpv5-d16 -mfloat-abi=hard
-std=c11 -Os               # MCU prefers size; still compile with full warnings
-ffunction-sections -fdata-sections
# Link-time: -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs
```

**M1 cross-compile target is a smoke test** — it just has to *compile and link clean* to prove MCU portability. No audio I/O, no HAL integration. A tiny `main.c` that calls `spu94_init` / `spu94_process` / `spu94_free` and returns 0 is sufficient.

### Confidence: HIGH

Verified against GCC docs ([Floating-point implementation](https://gcc.gnu.org/onlinedocs/gcc/Floating-point-implementation.html), [Optimize Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)), Daisy Makefile patterns ([libDaisy/core/Makefile](https://github.com/electro-smith/libDaisy/blob/master/core/Makefile)), and the Simon Byrne fast-math writeup ([simonbyrne.github.io/notes/fastmath](https://simonbyrne.github.io/notes/fastmath/)).

---

## 2. Build System

### Recommendation

**CMake 3.25+** for the C core library, with a **top-level Makefile** providing the ergonomic developer interface (`make test`, `make clean`, `make mcu-smoke`). **Confidence: MEDIUM-HIGH.**

### Rationale

This was the closest call in the whole stack. The honest answer:

- **Meson is technically nicer** (better error messages, cleaner cross files, cleaner subproject isolation per [mesonbuild.com/Cross-compilation](https://mesonbuild.com/Cross-compilation.html)).
- **CMake is more ubiquitous** and has the network effect: every IDE knows how to open a CMakeLists.txt, clang-tidy/clangd integrate natively via `compile_commands.json`, and cibuildwheel + scikit-build-core is the mainstream path for shipping C-plus-Python wheels in 2026.
- **Daisy's official build flow is Makefile-based**, and this is the decisive factor *against* tying the core library's build to either Meson or CMake *too tightly*. The core library must be buildable from a handwritten 30-line Makefile fragment that the Daisy template can `include`.

**The chosen shape:**
1. **The C core (`libspu94`) has a hand-written Makefile AND a CMakeLists.txt.** Both are short. Both produce bit-identical objects. The source files are enumerated explicitly (no globbing). The Makefile exists specifically so that the Daisy cross-compile target can consume the core with zero friction.
2. **CMake is the "integrator."** It builds the shared library for ctypes consumption, the CLI (`spu94`), the static library for embedding, and wires up tests. This is what developers run day to day.
3. **Meson is the also-ran.** Picking it would be correct for a pure-C project; picking it becomes wrong the moment you factor in the Daisy Makefile reality and the Python-wheel reality. Not worth the bet.

### Layout

```
libspu94/
  CMakeLists.txt              # Primary build for host
  Makefile                    # Thin wrapper: make host / make mcu-smoke / make test
  core/
    Makefile.inc              # Drop-in for Daisy template to `include`
    src/*.c  include/*.h
  cli/
    spu94.c                   # dr_wav-based CLI
  tests/
    CMakeLists.txt            # ctest + unity-style C unit tests
  python/
    spu94/__init__.py         # ctypes wrapper
    pyproject.toml            # scikit-build-core backend
  mcu-smoke/
    main.c  Makefile          # arm-none-eabi-gcc, links libspu94.a, does nothing
  cmake/
    toolchain-arm-none-eabi.cmake
```

### What NOT to do

- **Don't use plain Make only.** Tests and Python binding packaging need a real build system's dependency tracking.
- **Don't use autotools.** It's 2026.
- **Don't use Bazel.** Overkill for a 5kLoC C library, and the Bazel-for-Python story is still awkward.
- **Don't use Ninja directly.** CMake generates Ninja files; that's the right layer.

### Confidence: MEDIUM-HIGH

The CMake-vs-Meson choice is a judgment call based on ecosystem gravity; either would work. Daisy's Makefile convention pushes hard in the direction of a hand-written core Makefile regardless. Sources: [mesonbuild.com/meson-python](https://mesonbuild.com/meson-python/how-to-guides/shared-libraries.html), [libDaisy Makefile](https://github.com/electro-smith/libDaisy/blob/master/core/Makefile), [discuss.python.org packaging thread](https://discuss.python.org/t/adding-extension-module-examples-to-the-packaging-user-guide/105111).

---

## 3. WAV I/O (CLI only)

### Recommendation

**`dr_wav` v0.14.5** (single-header, MIT-0 / public domain), vendored into `cli/` as `dr_wav.h` + a one-line `#define DR_WAV_IMPLEMENTATION` in `cli/spu94.c`. **Confidence: HIGH.**

### Rationale

- **License.** MIT-0 / public domain dual license is the most permissive possible. Zero licensing concern for redistribution. libsndfile is LGPL-2.1-or-later, which would force either dynamic linking (awkward for a single-binary CLI) or an LGPL-compliance posture on the whole project. Since the user has deferred the MIT-vs-Apache decision specifically to avoid LGPL contamination, dr_wav is the correct choice.
- **Single-header.** Zero build system complexity. `cli/` has one extra file in it.
- **WAV-only.** SPU-94 tests on 44.1 kHz / 48 kHz 16-bit and 32-bit-float WAV; FLAC, AIFF, Ogg are irrelevant. libsndfile's breadth is wasted here.
- **Active maintenance.** Latest release v0.14.5 on 2026-03-03 (recent security fix for malformed `smpl` chunk) — actively patched, not abandoned.
- **Crucially, dr_wav is linked ONLY into the CLI, not into `libspu94`.** The core library has zero I/O dependencies. This is a hard architectural line.

### Why not roll our own

WAV looks simple until you hit: RIFF chunk ordering, non-PCM format tags, broken headers from other tools, float-vs-int sample formats, LIST/INFO metadata chunks. dr_wav handles all of this. Rolling our own is a known time sink with zero project value.

### Confidence: HIGH

[dr_libs GitHub](https://github.com/mackron/dr_libs), [libsndfile homepage](https://libsndfile.github.io/libsndfile/) (confirmed LGPL-2.1-or-later).

---

## 4. Python Binding Layout (ctypes)

### Recommendation

**ctypes + `numpy.ctypeslib.ndpointer`**, packaged as a **binary wheel** via **scikit-build-core + cibuildwheel**. Package layout co-locates the `.so` next to the Python wrapper. **Confidence: MEDIUM-HIGH.**

### Rationale and layout

```
python/spu94/
  __init__.py                  # Public Python API (dataclasses, load_preset, etc.)
  _lib.py                      # ctypes bindings, function signatures, struct definitions
  _binary/
    libspu94.so                # Installed here by scikit-build-core; wrapper finds it via __file__
  presets/
    room.json  hall.json ...   # Factory preset register configs as data files
py.typed                       # PEP 561 marker
```

**Binding idioms (the 2026 playbook):**

1. **Resolve the library via `__file__`, not `ctypes.util.find_library`.** `find_library` is unreliable cross-platform; shipping the `.so` inside the package and loading it with `ctypes.CDLL(os.path.join(os.path.dirname(__file__), "_binary", "libspu94.so"))` is the portable idiom (per [joerick/python-ctypes-package-sample](https://github.com/joerick/python-ctypes-package-sample)).

2. **Declare every `argtypes` and `restype` explicitly.** Silent integer-to-pointer coercions are the #1 ctypes footgun. Every function gets a line.

3. **Use `numpy.ctypeslib.ndpointer` for audio buffers:**
   ```python
   F32_1D = np.ctypeslib.ndpointer(dtype=np.float32, ndim=1, flags="C_CONTIGUOUS")
   lib.spu94_process.argtypes = [ctypes.c_void_p, F32_1D, F32_1D, F32_1D, F32_1D, ctypes.c_size_t]
   ```
   This gives you shape + dtype + contiguity validation at the boundary, for free.

4. **Struct packing: `ctypes.Structure` with explicit `_fields_` mirroring the C header.** Add `_Static_assert(sizeof(spu94_registers) == EXPECTED, ...)` on the C side to catch drift.

5. **Treat the ctypes wrapper as part of the public API.** A Python user should never `import ctypes` themselves; they import `spu94` and get idiomatic Python.

**Packaging (the cibuildwheel path):**

- **`pyproject.toml` with `build-backend = "scikit_build_core.build"`.** scikit-build-core is the 2026 mainstream for CMake-built Python packages; it's the successor to scikit-build and is actively maintained by the same crew that does pybind11.
- **`cibuildwheel` in GitHub Actions** builds `manylinux2014_x86_64` wheels. macOS and Windows deferred per project constraints.
- Wheel is tagged `py3-none-manylinux...` (the Python-version-agnostic tag) since ctypes doesn't use the Python C ABI. One wheel works for CPython 3.9 through 3.14+, and PyPy.
- **sdist contains the full C source** so pip-install-from-source works on systems without a matching wheel.

### What NOT to do

- **Don't try to use pybind11/nanobind.** Locked out by project constraint, and the ctypes choice is specifically to minimize the binding-maintenance surface.
- **Don't use `cffi`.** Locked out. (cffi is arguably nicer than ctypes for some cases; the decision has been made.)
- **Don't install the `.so` to system paths.** Keep it inside the package directory. No `LD_LIBRARY_PATH` dance.
- **Don't over-abstract.** The Python package exists to drive tests and exploration; keep it thin.

### Confidence: MEDIUM-HIGH

Layout pattern verified against [joerick/python-ctypes-package-sample](https://github.com/joerick/python-ctypes-package-sample) and the [cibuildwheel ctypes discussion](https://github.com/pypa/cibuildwheel/issues/837). scikit-build-core version: latest stable is 0.10.x; verify at publish time.

---

## 5. Test Framework

### Recommendation

**pytest 8.x** plus: **pytest-regressions** (golden files), **pytest-benchmark** (perf tracking), **numpy.testing** (tolerance assertions). **Confidence: HIGH.**

### Rationale

| Plugin | Purpose | Why |
|---|---|---|
| **pytest-regressions** (≥ 2.5, prefer 3.0+) | Golden-file audio output snapshotting | The `num_regression` and `data_regression` fixtures handle numpy arrays with configurable tolerances; `file_regression` for binary WAV. Mature (ESSS, used in production scientific pipelines). Has a `--force-regen` flag that matches the DECISIONS.md workflow: you sign off an intentional change by regenerating the golden. |
| **pytest-benchmark** 5.x | Track real-time safety claims empirically | When the C API says "no allocations in the hot path," we want continuous evidence. Benchmarks run in CI; regressions visible on PRs. |
| **numpy.testing.assert_allclose** (stdlib) | Audio assertions with rtol/atol | The idiomatic way to say "these two audio buffers agree to 1 LSB in Q15." Don't invent a custom matcher. |
| **pytest-xdist** (optional) | Parallelize slow tests | Not needed for M1 (test suite will be small) but trivial to add. |

**Parametrization strategy for register ranges:**
```python
@pytest.mark.parametrize("vIIR", [0x0000, 0x4000, 0x7FFF, 0x8000, 0xFFFF])
@pytest.mark.parametrize("vWALL", [0x0000, 0x4000, 0x7FFF])
def test_reverb_bounded(vIIR, vWALL, golden_impulse):
    ...
```
pytest's built-in parametrization is sufficient; don't pull in Hypothesis for M1. (Hypothesis-based property testing is a strong candidate for *M2 ADPCM*, where input-space exploration actually pays off. Note it for the M2 research brief.)

**Witness-diff harness** (diffing SPU-94 output against lv2-psx-reverb output): this is a small custom pytest module, not a plugin. It takes two WAVs, loads them via `scipy.io.wavfile`, and asserts `numpy.testing.assert_allclose(a, b, atol=1)`. The atol=1 threshold (literally 1 LSB in int16) is the bit-accuracy target.

### What NOT to do

- **Don't use pytest-golden.** It's less maintained than pytest-regressions and has no numpy-array support. pytest-regressions wins cleanly.
- **Don't invent custom fixtures for golden files before trying pytest-regressions.** The wheel already exists.
- **Don't skip pytest-benchmark** — even a `skip_benchmark` marker that runs locally-only is valuable. It's the only continuous check on real-time safety claims before M4 adds actual audio hardware.

### Confidence: HIGH

Sources: [pytest-regressions PyPI](https://pypi.org/project/pytest-regressions/) (2026 3.0+ release with improved numpy diffing), [pytest-benchmark 5.2.3 docs](https://pytest-benchmark.readthedocs.io/), [ESSS/pytest-regressions](https://github.com/ESSS/pytest-regressions).

---

## 6. Visualization and DSP Analysis

### Recommendation

**matplotlib 3.9+** as the primary (already locked in). Add **scipy.signal** (already locked in as scipy) for spectrograms/impulse responses. **Do not add librosa or pyloudnorm for M1.** **Confidence: HIGH.**

### Rationale

- **matplotlib + scipy.signal covers 100% of M1 needs.** Impulse-response plots, spectrograms, FFT magnitude, zero-pole diagrams, step response — all are one-liners in scipy.signal that render through matplotlib.
- **librosa** is excellent for *music information retrieval* (onset detection, chroma, MFCC, beat tracking). None of that is relevant to verifying a reverb algorithm. It's a heavyweight dependency (numba, soundfile, audioread) for zero M1 value.
- **pyloudnorm** measures ITU-R BS.1770 integrated loudness. Relevant *someday* for the M4 plugin ("does my wet signal match input loudness?"), but not for M1 verification. Note for M4 research.
- **Candidates worth knowing but not adding in M1:**
  - `soxr` — if resampling becomes relevant (it won't in M1; SPU runs at a fixed sample rate).
  - `pyroomacoustics` — academic reverb modeling library; potentially interesting for *comparing* SPU-94 output to "correct" Schroeder reverbs as a sanity check. File for M1-late exploration only.

### Confidence: HIGH

This is "don't add what you don't need" applied rigorously. scipy + matplotlib is the universally correct floor for DSP exploration in Python.

---

## 7. Daisy / Cortex-M Cross-Compile Smoke Test

### Recommendation

**Bare `arm-none-eabi-gcc` + a ~30-line linker script + a ~20-line `main.c`. Do NOT depend on libDaisy for the M1 smoke test.** **Confidence: MEDIUM-HIGH.**

### Rationale

The M1 smoke-test goal is narrow: **prove the `libspu94.a` static library compiles clean for `cortex-m7` with `arm-none-eabi-gcc`**, using no toolchain features the library doesn't need.

This is deliberately *below* the libDaisy abstraction level. libDaisy is a C++ HAL with HAL startup code, clock configuration, USB, MIDI, audio I/O — *none* of which the smoke test exercises. Pulling libDaisy in for a smoke test means:

- libDaisy requires a specific arm-none-eabi-gcc version (10.3-2021.10 was the historical sweet spot; newer versions have had intermittent issues per Daisy forums).
- libDaisy's Makefile drags in HAL objects (~2 MB of HAL source).
- libDaisy is C++ — introduces C++ runtime concerns the C core doesn't need.

**The lean smoke test:**

```c
// mcu-smoke/main.c
#include "spu94.h"
int main(void) {
    static float L[64], R[64], outL[64], outR[64];
    spu94_t *s = spu94_create_static();  // no heap; hands back pointer to static buffer
    spu94_load_preset(s, SPU94_PRESET_ROOM);
    spu94_process(s, L, R, outL, outR, 64);
    while (1) { __asm__("wfi"); }
}
```

Compiled with:
```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
  -std=c11 -Os -ffunction-sections -fdata-sections -Wall -Werror \
  -nostartfiles -T minimal.ld main.c libspu94.a -o smoke.elf
```

`minimal.ld` is a 30-line stub with a `.text` section at flash origin and a reset vector pointing to `main`. No clocks initialized. No HAL. It just has to **link to a valid ELF** — we're not flashing it.

**CI check:** `arm-none-eabi-size smoke.elf` — assert `.text < 64kB`, assert `.bss` is a known fixed value (catches accidental `static` growth and inadvertent heap usage).

### libDaisy integration is explicitly M4+ work

When the real Daisy firmware project lands (future milestone), the `core/Makefile.inc` drop-in is ready; libDaisy's CMake or Makefile can consume it unchanged. M1 just proves the *library* ports; the *firmware wrapper* is later.

### 2026 state of `arm-none-eabi-gcc`

The 2026-current Arm GNU Toolchain is **14.x** (`arm-none-eabi-gcc` 14.2.rel1 on [developer.arm.com](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)). Debian ships 15.x in sid. Daisy-specific users often still use 10.3-2021.10 for libDaisy compatibility reasons, but since we're avoiding libDaisy for M1, we can use whatever the host distro ships (probably 13.x or 14.x on Ubuntu 24.04 / 24.10). **Pin to one version in CI via Docker** — see question 9.

### Confidence: MEDIUM-HIGH

The "lean smoke test, not libDaisy" choice is a judgment call based on the project's "prove portability cheaply, defer integration" philosophy. Fully defensible. [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads), [Daisy forum toolchain thread](https://forum.electro-smith.com/t/get-started-faster-a-newer-simpler-way-to-install-the-toolchain/1523).

---

## 8. Fixed-Point Helpers

### Recommendation

**Hand-rolled.** **Confidence: HIGH.**

Use `stdint.h` types directly: `int16_t`, `int32_t`, `int64_t`. Define a thin set of inline helpers (`spu94_mul_q15`, `spu94_sat_s16`, `spu94_truncate_q15_from_q30`) in `include/spu94_fixed.h`. No external dependency.

### Rationale

- **The whole project thesis is bit-faithful reproduction of 1994 Sony SPU arithmetic.** Using libfixmath (which does Q16.16 with its own rounding conventions) is *actively harmful* — it does the arithmetic *correctly* by modern standards, which is exactly the behavior SPU-94 must *not* exhibit.
- **libfixmath and fpm use rounding on multiply.** SPU-94 must use truncation. Wrong library, wrong tool.
- **CMSIS-DSP Q15/Q31 ops use saturating arithmetic with rounding.** Same problem: it handles overflow *safely*, but the PS1 SPU's hard-clipping/overflow behavior *is the character of the sound*. Using a library that saturates politely destroys the product.
- **The relevant math is small:** Q15 multiply, Q30 accumulate, truncate-to-Q15, saturate-to-int16. Maybe 6 inline functions. Hand-rolling is ~50 lines, and every line is documented against a specific register's spec.

```c
// Example — the kind of helper we actually want
static inline int16_t spu94_mul_q15_trunc(int16_t a, int16_t b) {
    // Explicitly: sign-extended 32-bit product, arithmetic-shift-right-by-15
    // with truncation toward negative infinity (NOT round-to-nearest).
    // Matches SPU hardware observation documented in psx-spx.
    int32_t prod = (int32_t)a * (int32_t)b;
    return (int16_t)(prod >> 15);
}
```

Every line of this function is a project-specific design decision. Hiding it behind a library call defeats the point.

### What NOT to use

| Library | Why not |
|---|---|
| libfixmath | Rounds on multiply; wrong arithmetic for SPU emulation |
| CMSIS-DSP Q15 ops | Saturating + rounding; hides the artifacts we're modeling |
| fpm (C++ header) | C++ only; also rounds; also: locked out by project constraint |
| GCC `_Fract` types (ISO/IEC TR 18037) | Patchy compiler support (especially on arm-none-eabi); nonstandard; debugger support weak |

### Confidence: HIGH

This is one of the stronger recommendations in the doc because the reasoning is *project-specific* — the libraries aren't bad, they're just solving a different problem. "Bit-accuracy is not optional — it is the sound" (PROJECT.md) settles this unambiguously.

---

## 9. Reproducibility / CI

### Recommendation

**GitHub Actions + pinned Docker image** for CI. **Reproducibility hinges on five discipline points** (listed below), not on Nix. **Confidence: MEDIUM-HIGH.**

### Rationale

The real question: **what does "reproducible" mean here?** For SPU-94, it means *golden-file tests produce byte-identical output across developer machines and CI machines*. That's a narrower goal than NixOS-style "100% reproducible binaries." The narrower goal is achievable with a much lighter toolchain.

### The five discipline points

**1. The core library must be fully integer.** All of the reverb network math is fixed-point by design. If the core library never touches `float` or `double`, floating-point determinism is a non-issue inside the core. Floats may appear at the I/O boundary (WAV loader → `int16_t` conversion, CLI output), and *there* the rules below apply.

**2. Compile every float operation with `-ffp-contract=off` AND `-fno-fast-math`.** GCC defaults to `-ffp-contract=fast`, which allows fused multiply-add substitution — and FMA on x86 (with AVX2) produces different bit-exact results than on a machine without FMA. `-ffp-contract=off` is the single flag most commonly missed. ([GCC FP implementation](https://gcc.gnu.org/onlinedocs/gcc/Floating-point-implementation.html), [krister.github.io fast-math writeup](https://kristerw.github.io/2021/10/19/fast-math/).)

**3. Pin the toolchain version.** CI uses a Docker image `spu94-ci:gcc14.2-clang19-python3.12`. Every developer `docker pull`s the same image. Local Ubuntu developers can run their system toolchain for speed, but **golden-file tests are only blessed against the CI image**.

**4. Pin every Python dependency via `requirements.txt` with hashes** (or use `uv lock` — 2026 is well past the `pip-tools` / `pip-compile` transition to `uv`). numpy major-version changes can shift last-bit float results; scipy.signal changes can shift FFT bin outputs. Don't get surprised.

**5. Normalize source build artifacts.** `SOURCE_DATE_EPOCH=0` when building release tarballs; sorted `ar` input order for the static lib; etc. Matters only at the distribution stage, but set now to avoid a rewrite later.

### Why not Nix

Nix would give you a stronger guarantee than the above — genuine bit-for-bit binary reproducibility. But:
- The learning curve costs weeks.
- Anthony is on Ubuntu Studio; Nix-on-Ubuntu adds friction.
- The golden-file guarantee we need is *output* reproducibility, not *binary* reproducibility. Docker + the five points above get us there with a fraction of the investment.

**Note it for the future:** if SPU-94 ever becomes a commercial release with SBOM / supply-chain attestation requirements, revisit Nix. Not an M1 concern.

### CI shape (GitHub Actions)

```yaml
jobs:
  host-linux-gcc:    # Builds host .so + runs all pytest including golden files
  host-linux-clang:  # Same, with sanitizers (ASan, UBSan) on a subset
  mcu-smoke:         # Cross-compile to cortex-m7; size-check the ELF
  static-analysis:   # clang-tidy + cppcheck (see question 10)
  wheel-build:       # cibuildwheel produces the manylinux2014 wheel
```

All jobs run in the same pinned Docker image (or a matrix of pinned images).

### Confidence: MEDIUM-HIGH

Strategy is standard; the `-ffp-contract=off` point is the one most teams miss. Sources: [reproducible-builds.org](https://reproducible.nixos.org/), [simonbyrne fast-math](https://simonbyrne.github.io/notes/fastmath/).

---

## 10. Static Analysis

### Recommendation

Run **three** tools in CI, in tiers of strictness. **Confidence: HIGH.**

| Tool | When | Rigor | Blocking? |
|---|---|---|---|
| **Compiler warnings** (GCC `-Wall -Wextra -Werror` AND Clang `-Wall -Wextra -Werror`) | Every build | Highest signal-to-noise | YES — build fails |
| **clang-tidy** (with `bugprone-*`, `cert-*`, `portability-*`, `readability-*` checks) | Every PR | High SNR; a few false positives | YES — warnings fail CI |
| **cppcheck** (with `--enable=warning,style,performance,portability --std=c11`) | Every PR | Lower SNR than clang-tidy; catches different class of bugs | YES initially, can be downgraded to advisory if false-positive noise becomes a problem |
| **`-fsanitize=address,undefined,integer`** (Clang dynamic analysis) | Test suite, not every build | Very high signal when triggered | YES in the sanitizers CI job |

### Rationale

- **clang-tidy is the highest-value tool for this project.** Specific checks that matter for SPU-94:
  - `bugprone-integer-division` — catches `x / 2` where `x >> 1` was meant (or vice versa) in fixed-point code.
  - `bugprone-narrowing-conversions` — catches silent `int32_t → int16_t` assignments, the exact class of bug a truncation-faithful library *must* do explicitly, not accidentally.
  - `cert-flp30-c` — don't use float in loop counters (real-time hygiene).
  - `portability-restrict-system-includes` — keeps the core library hermetic.
  - `readability-magic-numbers` — forces every magic constant in the reverb network to have a named `#define` with a spec citation.
- **cppcheck catches a different class** — specifically, uninitialized reads across function boundaries that clang-tidy sometimes misses, and some array-bounds issues. Free, zero setup, runs fast. Worth the CI minute.
- **Coverity is overkill for M1.** It's the gold standard for commercial C projects but the open-source Coverity Scan flow is annoying (delayed results, no PR integration) and the licensed version is $$$. Skip.
- **Infer, CodeChecker, PVS-Studio** — all solid, all more friction than clang-tidy + cppcheck provides in return. Defer.
- **Sanitizers (ASan/UBSan/MSan) are not static analysis** but belong in the same CI slot because they catch overlapping bug classes. UBSan's integer overflow check (`-fsanitize=integer`) is *especially important* because SPU-94 intentionally overflows — but it must overflow *in specific documented places only*. UBSan catches unintended overflows while your documented-overflow code uses `__attribute__((no_sanitize("integer")))` on the specific functions that simulate SPU saturation.

### `.clang-tidy` starter

```yaml
Checks: >
  bugprone-*,
  cert-*,
  portability-*,
  readability-*,
  -readability-identifier-length,
  -readability-magic-numbers,  # enable later once the constant catalog is mature
  -cert-err33-c
WarningsAsErrors: '*'
HeaderFilterRegex: '^(include|src)/.*\.h$'
```

### Confidence: HIGH

Sources: [danmar/cppcheck clang-tidy comparison](https://github.com/danmar/cppcheck/blob/main/clang-tidy.md), [clang-tidy integrations](https://clang.llvm.org/extra/clang-tidy/Integrations.html), [developers-heaven.net static + sanitizer writeup](https://developers-heaven.net/blog/static-and-dynamic-analysis-tools-clang-tidy-cppcheck-and-sanitizers/).

---

## Consolidated Stack Summary

### Core (shipped to users of the library)

| Technology | Version | Purpose | Rationale |
|---|---|---|---|
| C11 | `-std=c11 -pedantic` | Core language | Static asserts, alignas, anonymous structs; widely supported on all target toolchains |
| GCC | 13+ (host), `arm-none-eabi-gcc` 14.x (cross) | Primary compiler | Best MCU toolchain support; pair with Clang for second opinion |
| Clang | 18+ (host only) | Secondary compiler + sanitizers | ASan/UBSan/integer-sanitizer; diverse-compiler coverage |
| CMake | 3.25+ | Primary build system | Ecosystem gravity, Python-wheel path, LSP integration |
| `dr_wav.h` | v0.14.5 (vendored) | CLI WAV I/O | MIT-0/public domain; single header; no impact on core library |

### Development and Test

| Technology | Version | Purpose | Rationale |
|---|---|---|---|
| Python | 3.11+ | Test/analysis host | Modern enough for `tomllib`, `typing` improvements; widely available on Ubuntu 24.04+ |
| numpy | 2.x | Buffer passing, math | Pinned in `requirements.txt`; numpy 2.x ABI stable throughout 2026 |
| scipy | 1.14+ | `scipy.signal` for DSP analysis | `scipy.signal.freqz`, `scipy.signal.impulse_response` |
| matplotlib | 3.9+ | Plotting | Locked in |
| pytest | 8.x | Test runner | Locked in |
| pytest-regressions | 3.0+ | Golden-file numpy array snapshotting | Mature, numpy-aware, --force-regen matches DECISIONS.md workflow |
| pytest-benchmark | 5.x | Real-time performance regression tracking | Empirical check on "no hot-path allocations" claim |
| scikit-build-core | 0.10+ | Python build backend (wraps CMake) | 2026 mainstream for CMake+Python |
| cibuildwheel | 2.x | Cross-platform wheel builds | GitHub Actions integration; manylinux2014 target |
| clang-tidy | 18+ | Static analysis | Highest-signal static analysis for C |
| cppcheck | 2.13+ | Secondary static analysis | Catches complementary bug classes |

### Infrastructure

| Technology | Purpose | Rationale |
|---|---|---|
| GitHub Actions | CI | Standard; free for public; already where everyone is |
| Docker | Pinned build environment | Simpler than Nix for a small-team project; sufficient for output-reproducibility |
| `uv` | Python dependency resolution | 2026 successor to pip-tools/pip-compile |

---

## What NOT to Use (Critical)

| Avoid | Why | Use Instead |
|---|---|---|
| libsndfile in the CLI | LGPL-2.1-or-later contaminates license posture | dr_wav (MIT-0) |
| libfixmath / fpm / CMSIS-DSP Q15 | Round instead of truncate — wrong arithmetic for PS1 SPU emulation | Hand-rolled fixed-point helpers with documented truncation semantics |
| `malloc` / `free` in the hot path | Real-time safety violation | Preallocated work buffers; `spu94_create_static()` style API |
| `printf` / `fprintf` in the core library | Not real-time safe; pulls stdio into MCU binary | Error codes returned to caller; logging only in CLI |
| `-ffast-math` / default `-ffp-contract=fast` | Breaks golden-file determinism across machines | `-fno-fast-math -ffp-contract=off` |
| libDaisy for the M1 smoke test | Drags in HAL, C++ runtime, specific compiler version | Bare `arm-none-eabi-gcc` with minimal linker script |
| pybind11 / nanobind / cffi | Locked out by project constraints; also maintenance-heavier than ctypes | ctypes + `numpy.ctypeslib.ndpointer` |
| `-O3` on release builds | Autovectorization can shift last-bit results across machines | `-O2` |
| `std::vector` equivalents (dynamic arrays) | No heap in hot path | Fixed-size static arrays; work buffer sized at compile time or at init |
| Nix for CI reproducibility | Overkill; weeks of learning curve for output-reproducibility goal | Pinned Docker image + the five discipline points |
| Coverity / PVS-Studio for M1 | Diminishing returns over clang-tidy+cppcheck; friction-heavy | clang-tidy + cppcheck + sanitizers |
| C23 features | Partial toolchain support in 2026 `arm-none-eabi-gcc` | Stick to C11 |
| `-std=gnu11` (GNU extensions enabled) | Hides portability issues | `-std=c11 -pedantic` |

---

## Hidden Traps Called Out

1. **`-ffp-contract=fast` is GCC's default even without `-ffast-math`.** Most teams catch `-ffast-math`; few catch this. Will silently break cross-machine golden file tests.
2. **UBSan `-fsanitize=integer` is a double-edged sword for SPU-94.** The library *intentionally* overflows in documented places. You need `__attribute__((no_sanitize("integer")))` annotations on exactly those functions, not a blanket disable. Otherwise UBSan becomes noise and gets turned off entirely — losing the catch on *unintentional* overflows.
3. **ctypes struct layout drift between C and Python is silent.** A C-side `_Static_assert(sizeof(spu94_registers) == 48, "...")` plus a Python-side assertion at import time catches this before the first wrong-output surprise.
4. **dr_wav vendoring means you own security patches.** When dr_wav 0.14.5 fixed the smpl-chunk CVE in March 2026, every vendoring project had to manually update. Set a quarterly reminder; automate if possible.
5. **numpy 2.x changed some default integer types on Windows.** SPU-94 is Linux-only for M1, but note for M4 plugin cross-platform expansion.
6. **`arm-none-eabi-gcc` linker scripts default to expecting `_start` — bare-metal M1 smoke test uses `-nostartfiles` and a custom `main`**. Getting this wrong burns an afternoon the first time.
7. **pytest-regressions' `num_regression` uses pandas by default for floats.** For pure numpy workflows, prefer `data_regression` or `file_regression` with explicit numpy serialization. Otherwise pandas becomes a surprise transitive dependency.

---

## Installation (Target State After Phase 1)

```bash
# System packages (Ubuntu 24.04)
sudo apt install build-essential cmake ninja-build \
                 gcc-arm-none-eabi \
                 clang clang-tidy cppcheck \
                 python3.12 python3.12-venv python3.12-dev

# Python environment
uv venv
uv pip install -r requirements.txt   # pytest, pytest-regressions, pytest-benchmark,
                                      # numpy, scipy, matplotlib, scikit-build-core

# Build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build

# Cross-compile smoke test
cmake -S . -B build-mcu -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
cmake --build build-mcu --target mcu-smoke

# Python wheel
python -m pip install build
python -m build --wheel
```

---

## Confidence Summary

| Area | Confidence | Notes |
|---|---|---|
| C standard (C11) | HIGH | Widely supported; features directly useful |
| Compiler flags | HIGH | `-ffp-contract=off` is the critical non-obvious one |
| Build system (CMake) | MEDIUM-HIGH | Meson is defensible; CMake wins on ecosystem gravity + Daisy Makefile reality |
| WAV I/O (dr_wav) | HIGH | License dominates the decision |
| Python binding layout | MEDIUM-HIGH | scikit-build-core path is mainstream but evolving |
| Test framework (pytest + regressions + benchmark) | HIGH | All three are mature, well-documented |
| Visualization (matplotlib + scipy) | HIGH | "Don't add what you don't need" applied rigorously |
| Daisy smoke test (bare-metal) | MEDIUM-HIGH | Judgment call to avoid libDaisy; fully defensible |
| Fixed-point (hand-rolled) | HIGH | Project-specific reasoning (truncation semantics) settles it |
| Reproducibility (Docker + discipline) | MEDIUM-HIGH | Nix would be stronger but not worth the cost for output-reproducibility |
| Static analysis (clang-tidy + cppcheck + sanitizers) | HIGH | Standard recommendation; the UBSan-integer caveat is the non-obvious part |

---

## Sources

### Primary (HIGH confidence)

- [GCC Floating-Point Implementation docs](https://gcc.gnu.org/onlinedocs/gcc/Floating-point-implementation.html) — verified `-ffp-contract` default behavior
- [GCC Optimize Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) — optimization flag semantics
- [dr_libs GitHub (mackron)](https://github.com/mackron/dr_libs) — v0.14.5 release, license verification
- [libsndfile homepage](https://libsndfile.github.io/libsndfile/) — LGPL-2.1-or-later confirmation
- [libDaisy Makefile (master)](https://github.com/electro-smith/libDaisy/blob/master/core/Makefile) — Daisy toolchain conventions
- [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) — current `arm-none-eabi-gcc` release info
- [pytest-regressions (ESSS/GitHub)](https://github.com/ESSS/pytest-regressions) — golden-file plugin with numpy support
- [pytest-benchmark 5.2.3 docs](https://pytest-benchmark.readthedocs.io/)
- [danmar/cppcheck clang-tidy comparison](https://github.com/danmar/cppcheck/blob/main/clang-tidy.md)
- [libfixmath (Wikipedia)](https://en.wikipedia.org/wiki/Libfixmath) — confirmed MIT license and Q16.16 rounding behavior

### Secondary (MEDIUM confidence)

- [Simon Byrne: Beware of fast-math](https://simonbyrne.github.io/notes/fastmath/) — determinism writeup
- [Krister Walfridsson: Optimizations enabled by -ffast-math](https://kristerw.github.io/2021/10/19/fast-math/)
- [meson-python shared-library guide](https://mesonbuild.com/meson-python/how-to-guides/shared-libraries.html)
- [joerick/python-ctypes-package-sample](https://github.com/joerick/python-ctypes-package-sample) — ctypes + cibuildwheel pattern
- [pypa/cibuildwheel discussion #837](https://github.com/pypa/cibuildwheel/issues/837) — ctypes wheel-tagging conventions
- [CMake vs Meson real-life comparison (Kea Sigma Delta)](https://keasigmadelta.com/blog/cmake-vs-meson-a-real-life-comparison-with-actual-code/)
- [Embedded Artistry: Meson for cross-platform embedded builds](https://embeddedartistry.com/course/building-a-cross-platform-build-system-for-embedded-projects/)
- [Reproducible Builds (Wikipedia)](https://en.wikipedia.org/wiki/Reproducible_builds)

### Tertiary (LOW confidence — verify at implementation time)

- Exact pytest-regressions v3.0 feature set — verify against PyPI release notes at implementation time
- scikit-build-core version number — pin to latest stable when CI is set up
- Specific clang-tidy check list — iterate during Phase 1; the starter list is a recommendation, not gospel

---

*Stack research for: bit-faithful C reimplementation of PS1 SPU reverb, Linux-primary, MCU-portable.*
*Researched: 2026-04-18*
