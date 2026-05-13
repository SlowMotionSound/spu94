# Phase 25: Buses & Validator Gates - Pattern Map

**Mapped:** 2026-05-12
**Files analyzed:** 6 (new/modified)
**Analogs found:** 6 / 6

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/plugin/PluginProcessor.cpp` (modify) | controller | request-response | self (existing processBlock) | exact |
| `src/plugin/PluginProcessor.h` (modify) | controller | request-response | self (existing header) | exact |
| `src/plugin/CMakeLists.txt` (modify) | config | N/A | self (CLAP_FEATURES line 106) | exact |
| `.github/workflows/plugins.yml` (modify) | config | batch | self (existing CI workflow) | exact |
| `tests/plugin/test_bus_layout.cpp` (create) | test | request-response | `tests/plugin/test_state_roundtrip.cpp` | role-match |
| `tests/plugin/test_mono_sum.cpp` (create) | test | transform | `tests/plugin/test_null_passthrough.cpp` | role-match |
| `tests/plugin/CMakeLists.txt` (modify) | config | N/A | self (existing test targets) | exact |

## Pattern Assignments

### `src/plugin/PluginProcessor.cpp` — isBusesLayoutSupported override (controller, request-response)

**Analog:** self — the override is new, but it follows the existing class structure.

**Class declaration pattern** (`PluginProcessor.h` lines 17-31):
```cpp
class SPU94AudioProcessor : public juce::AudioProcessor
{
public:
    SPU94AudioProcessor();
    ~SPU94AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;
    // ... isBusesLayoutSupported goes here, among the other overrides
```

**Constructor BusesProperties pattern** (`PluginProcessor.cpp` lines 10-13):
```cpp
SPU94AudioProcessor::SPU94AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
```
Note: The constructor stays unchanged. The `isBusesLayoutSupported` override declares supported layouts; the constructor only declares the default.

**Mono input handling pattern** (`PluginProcessor.cpp` lines 572-575):
```cpp
const float* rawL = buffer.getReadPointer(0);
const float* rawR = buffer.getNumChannels() > 1
                        ? buffer.getReadPointer(1)
                        : buffer.getReadPointer(0);
```
Already handles mono input by duplicating channel 0. No change needed here.

**Output pointer pattern** (`PluginProcessor.cpp` lines 595-600):
```cpp
float* hostOutPtrs[2] = {
    buffer.getWritePointer(0),
    buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr
};
int hostNOut = 0;
srcChain_.processOut(coreOutL, coreOutR, coreN, hostOutPtrs, hostNOut);
```
When `hostOutPtrs[1]` is nullptr, `SrcChain::processOut` already skips writing to R (lines 303, 341 in SrcChain.cpp). But R data is lost -- for mono summing, a scratch buffer is needed to capture R so `(L+R)*0.5` can be computed.

**SrcChain processOut null-R handling** (`SrcChain.cpp` lines 295-297, 340-341):
```cpp
jassert(hostOut != nullptr && hostOut[0] != nullptr);
// Note: hostOut[1] is NOT asserted non-null
// ...
float* outR = (hostOut[1] != nullptr) ? hostOut[1] : nullptr;
```
SrcChain already tolerates a null R output. The scratch buffer approach (provide a real buffer for R, sum afterward) is clean.

**Side-channel limiter pattern** (`PluginProcessor.cpp` lines 619-632):
```cpp
float* outL = hostOutPtrs[0];
float* outR = hostOutPtrs[1];
for (int i = 0; i < n; ++i)
{
    const float wetL = outL[i];
    const float wetR = (outR != nullptr) ? outR[i] : wetL;
    const float mid  = 0.5f * (wetL + wetR);
    const float side = 0.5f * (wetL - wetR);
    const float sideLimited = std::tanh(side / kSideKnee) * kSideCeiling;
    outL[i] = mid + sideLimited;
    if (outR != nullptr) outR[i] = mid - sideLimited;
}
```
For mono output, side = 0 by definition after (L+R)*0.5 summing, so the limiter becomes identity. Should be skipped with a guard: `if (buffer.getNumChannels() > 1) { ... }`.

**Stack scratch allocation pattern** (`PluginProcessor.cpp` lines 550-555):
```cpp
int16_t coreInL [kMaxBlock];
int16_t coreInR [kMaxBlock];
int16_t coreOutL[kMaxBlock];
int16_t coreOutR[kMaxBlock];
```
Mono scratch buffer for R should follow this same stack-allocation style: `float monoRScratch[kMaxBlock];`

**Under-produce padding pattern** (`PluginProcessor.cpp` lines 606-617):
```cpp
if (hostNOut < n)
{
    float* outL = hostOutPtrs[0];
    float* outR = hostOutPtrs[1];
    const float lastL = (hostNOut > 0) ? outL[hostNOut - 1] : 0.0f;
    const float lastR = (outR != nullptr && hostNOut > 0) ? outR[hostNOut - 1] : 0.0f;
    for (int i = hostNOut; i < n; ++i)
    {
        outL[i] = lastL;
        if (outR != nullptr) outR[i] = lastR;
    }
}
```
This already handles null outR. For mono output with scratch, the padding needs to also pad the scratch R buffer before mono summing.

---

### `src/plugin/CMakeLists.txt` — CLAP_FEATURES update (config)

**Analog:** self, line 106.

**Current CLAP_FEATURES line** (`src/plugin/CMakeLists.txt` line 106):
```cmake
                            CLAP_FEATURES audio-effect stereo reverb)
```
Change to:
```cmake
                            CLAP_FEATURES audio-effect mono stereo reverb)
```

---

### `.github/workflows/plugins.yml` — validator gates (config, batch)

**Analog:** self — the existing workflow structure at lines 1-285.

**Job structure pattern** (lines 36-46):
```yaml
jobs:
  build-plugins:
    name: build-${{ matrix.name }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - { os: ubuntu-22.04, name: linux }
          - { os: macos-14,     name: macos }
          - { os: windows-2022, name: windows }
```

**SHA-pinned action pattern** (lines 49, 88, 239, 267):
```yaml
uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683 # v4.2.2
uses: actions/cache@1bd1e32a3bdc45362d1e726936510720a7c30a57 # v4.2.0
uses: actions/upload-artifact@b4b15b8c7c6ac21ea08fcf65892d2ee8f75cf882 # v4.4.3
uses: actions/download-artifact@fa0a91b85d4f404e444e00e005971372dc801d16 # v4.1.8
```
All new steps must pin third-party actions to full commit SHA.

**pluginval install + smoke pattern** (lines 132-171):
```yaml
- name: Install pluginval (Linux)
  if: matrix.name == 'linux'
  run: |
    curl -fsSL -o /tmp/pluginval.zip \
      "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_Linux.zip"
    mkdir -p "$HOME/bin"
    unzip -o /tmp/pluginval.zip -d "$HOME/bin"
    chmod +x "$HOME/bin/pluginval"
    echo "$HOME/bin" >> "$GITHUB_PATH"

- name: Smoke (Linux plugin formats)
  if: matrix.name == 'linux'
  run: |
    set -e
    pluginval --validate-in-process --strictness-level 1 \
      "${PLUGIN_ARTEFACTS_DIR}/VST3/SPU-94.vst3"
```
Promotion to strictness-7 replaces `--strictness-level 1` with `--strictness-level 7` and removes `continue-on-error`.

**Advisory pluginval-7 job pattern** (lines 252-285):
```yaml
pluginval-early-warning:
    name: pluginval-strictness-7 (advisory)
    needs: build-plugins
    runs-on: ubuntu-22.04
    continue-on-error: true
    steps:
      - name: Install deps
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends \
            libasound2 libxinerama1 ...
```
This entire advisory job gets either promoted to a hard gate (removing `continue-on-error: true`) or replaced by per-format strictness-7 steps in the main build job.

**Per-OS conditional pattern** (lines 54-56, 162-171):
```yaml
- name: Install Linux deps
  if: matrix.name == 'linux'
  run: |
    ...

- name: Smoke (Linux plugin formats)
  if: matrix.name == 'linux'
```
New validator steps (auval, lv2lint, VST3 validator) follow this same `if: matrix.name == '...'` gating pattern.

**Artefacts dir env var** (lines 33):
```yaml
env:
  PLUGINVAL_VERSION: "v1.0.4"
  PLUGIN_ARTEFACTS_DIR: "build/src/plugin/spu94_plugin_artefacts/Release"
```
All validator steps reference `${PLUGIN_ARTEFACTS_DIR}` for built plugin bundles.

---

### `tests/plugin/test_bus_layout.cpp` (create, test, request-response)

**Analog:** `tests/plugin/test_state_roundtrip.cpp`

**Test file structure pattern** (`test_state_roundtrip.cpp` lines 1-25, 239-251):
```cpp
// tests/plugin/test_state_roundtrip.cpp
//
// Phase 24 / PLUG-26, PLUG-27: headless state round-trip tests.

#include "PluginProcessor.h"
#include <JuceHeader.h>

#include <cstdio>
#include <cstdlib>

namespace {

// ... test functions ...

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI init;

    int failures = 0;
    int total = 4;
    if (!test_set_state_restores_params())     ++failures;
    // ...
    std::printf("\n%d/%d tests passed.\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}
```

**Test function pattern** (`test_state_roundtrip.cpp` lines 75-116):
```cpp
bool test_set_state_restores_params() {
    std::printf("test_set_state_restores_params... ");

    auto proc = std::make_unique<SPU94AudioProcessor>();

    // ... exercise the processor ...

    bool ok = true;
    auto check = [&](const char* name, float expected, float actual) {
        if (!approxEq(expected, actual)) {
            std::printf("\n  FAIL %s: expected %.4f, got %.4f", name, expected, actual);
            ok = false;
        }
    };

    // ... assertions ...

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}
```

**Processor instantiation pattern** (`test_state_roundtrip.cpp` line 89):
```cpp
auto proc = std::make_unique<SPU94AudioProcessor>();
```
The bus layout test will use `checkBusesLayoutSupported()` on a processor instance.

---

### `tests/plugin/test_mono_sum.cpp` (create, test, transform)

**Analog:** `tests/plugin/test_null_passthrough.cpp`

**Heavyweight test structure** (`test_null_passthrough.cpp` lines 1-34, 94-104):
```cpp
#include "SrcChain.h"
#include <spu94/spu94.h>
#include <samplerate.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {
// ... constants and helpers ...
} // anonymous namespace

int main()
{
    std::printf("==== PLUG-15 null test (two host SRs) ====\n\n");
    TestResult r44 = runNullTest(44100.0);
    TestResult r48 = runNullTest(48000.0);
    // ...
    return (r44.pass && r48.pass) ? 0 : 1;
}
```

**Engine allocation pattern** (`test_null_passthrough.cpp` lines 116-135):
```cpp
void* alignedState = std::aligned_alloc(16, kStateBufSize);
void* alignedWork  = std::aligned_alloc(16, kWorkBufSize);
std::memset(alignedState, 0, kStateBufSize);
std::memset(alignedWork,  0, kWorkBufSize);

spu94_state* engine = spu94_init(alignedState, kStateBufSize,
                                  alignedWork,  kWorkBufSize);
spu94_load_preset(engine, SPU94_PRESET_HALL);
spu94_set_input_gain(engine, kQ15Unity);
```

**dBFS helper** (`test_null_passthrough.cpp` lines 63-67):
```cpp
double linearToDbfs(double x) noexcept
{
    if (x <= 0.0) return -200.0;
    return 20.0 * std::log10(x);
}
```

However, `test_mono_sum.cpp` is a simpler unit test (verify (L+R)/2 accuracy). The `test_state_roundtrip.cpp` structure (bool test functions, `main()` with failure counter) is a better fit than the heavyweight null-test structure. Use the roundtrip test's lightweight pattern with SrcChain if needed, or pure arithmetic verification.

---

### `tests/plugin/CMakeLists.txt` (modify, config)

**Analog:** self — existing test target declarations.

**Console app test target pattern** (`tests/plugin/CMakeLists.txt` lines 79-125):
```cmake
# Phase 24 PLUG-26/27: Full processor state round-trip test.
juce_add_console_app(test_state_roundtrip
    PRODUCT_NAME "SPU-94 State Roundtrip Test"
)
juce_generate_juce_header(test_state_roundtrip)

target_sources(test_state_roundtrip PRIVATE
    test_state_roundtrip.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

# Standalone-only WavLoader bolt-on
if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_state_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_state_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_state_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_state_roundtrip PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_state_roundtrip PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

add_test(NAME state_roundtrip
         COMMAND test_state_roundtrip)
```

`test_bus_layout.cpp` needs the full processor (calls `checkBusesLayoutSupported`), so it must link the same sources as `test_state_roundtrip`. `test_mono_sum.cpp` may need either the full processor (to test processBlock with a 1-channel buffer) or just SrcChain + BoundaryConverter (lighter-weight).

**Lightweight test target pattern** (`tests/plugin/CMakeLists.txt` lines 11-45):
```cmake
juce_add_console_app(test_null_passthrough
    PRODUCT_NAME "SPU-94 Null Test"
)
juce_generate_juce_header(test_null_passthrough)

target_sources(test_null_passthrough PRIVATE
    test_null_passthrough.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

target_include_directories(test_null_passthrough PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_null_passthrough PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_null_passthrough PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
)

add_test(NAME plug15_null_passthrough_48k
         COMMAND test_null_passthrough)
```

---

## Shared Patterns

### RT-Safety: Stack Allocation for Scratch Buffers
**Source:** `src/plugin/PluginProcessor.cpp` lines 550-555
**Apply to:** All processBlock modifications (mono scratch buffer)
```cpp
constexpr int kMaxBlock = 4096;
// Stack-allocated scratch -- NO heap allocation in processBlock
int16_t coreInL [kMaxBlock];
int16_t coreInR [kMaxBlock];
```

### ScopedNoDenormals Guard
**Source:** `src/plugin/PluginProcessor.cpp` lines 228-233
**Apply to:** processBlock -- already present, must remain first statement
```cpp
juce::ScopedNoDenormals noDenormals;
```

### Null Channel Pointer Guard Pattern
**Source:** `src/plugin/PluginProcessor.cpp` lines 595-597, `SrcChain.cpp` lines 303, 341
**Apply to:** All output-writing code in the plugin path
```cpp
float* hostOutPtrs[2] = {
    buffer.getWritePointer(0),
    buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr
};
```

### CI SHA-Pinning Convention
**Source:** `.github/workflows/plugins.yml` lines 49, 88, 239, 267
**Apply to:** All new CI steps that use third-party GitHub Actions
```yaml
uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683 # v4.2.2
```

### Test File Structure (Lightweight)
**Source:** `tests/plugin/test_state_roundtrip.cpp` lines 239-251
**Apply to:** `test_bus_layout.cpp`, `test_mono_sum.cpp`
```cpp
int main() {
    juce::ScopedJuceInitialiser_GUI init;

    int failures = 0;
    int total = N;
    if (!test_foo()) ++failures;
    if (!test_bar()) ++failures;
    std::printf("\n%d/%d tests passed.\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}
```

### Test CMake Target Pattern (Full Processor)
**Source:** `tests/plugin/CMakeLists.txt` lines 79-125
**Apply to:** `test_bus_layout` target (needs `checkBusesLayoutSupported` on a processor instance)
```cmake
juce_add_console_app(test_bus_layout
    PRODUCT_NAME "SPU-94 Bus Layout Test"
)
juce_generate_juce_header(test_bus_layout)
target_sources(test_bus_layout PRIVATE
    test_bus_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    # ... all plugin source files ...
)
# ... same includes, definitions, and link libraries as test_state_roundtrip ...
add_test(NAME bus_layout COMMAND test_bus_layout)
```

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All files have close analogs in the existing codebase |

## Key Corrections to RESEARCH.md

1. **SrcChain processOut null-R is safe.** The research (Pitfall 1) states SrcChain.processOut asserts `hostIn[1] != nullptr` at line 240-241. This assertion is on `processIn` (the INPUT direction), not `processOut`. The `processOut` at line 296 only asserts `hostOut[0] != nullptr`; it handles `hostOut[1] == nullptr` gracefully at lines 303 and 341 by setting `R = nullptr` and guarding writes with `if (R)` / `if (outR)`. The scratch buffer approach is still needed for mono summing (to capture R for the `(L+R)*0.5` computation), but no assertion will fire.

## Metadata

**Analog search scope:** `src/plugin/`, `tests/plugin/`, `.github/workflows/`
**Files scanned:** 14 source files + 2 CI workflows
**Pattern extraction date:** 2026-05-12
