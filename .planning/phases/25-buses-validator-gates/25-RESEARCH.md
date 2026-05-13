# Phase 25: Buses & Validator Gates - Research

**Researched:** 2026-05-12
**Domain:** JUCE AudioProcessor bus layout declaration + CI-gated plugin validation (pluginval, auval, lv2lint, VST3 SDK validator)
**Confidence:** HIGH

## Summary

Phase 25 is two cleanly separable workstreams that share zero code: (A) declaring mono/mono + mono/stereo + stereo/stereo bus configurations in `isBusesLayoutSupported` and handling mono I/O in `processBlock`, and (B) promoting four plugin validators from advisory/absent to hard CI gates in `plugins.yml`.

The bus layout work is well-supported by JUCE's API. The key pattern is overriding `isBusesLayoutSupported` to accept exactly three `{input, output}` channel-set pairs, while the `BusesProperties` constructor declares the *default* layout (stereo/stereo). The processBlock plugin path already handles mono input gracefully (duplicates channel 0 to both L/R at line 573-575) and produces output to a potentially-null second channel pointer (line 596-597). The main code change is the `isBusesLayoutSupported` override plus a mono-output summing path.

The validator work is CI-only -- no plugin source changes. pluginval strictness-7 is the target (it enables "All parameters" fuzz, "Background thread state" restore, and "Parameter thread safety" concurrency tests). auval runs on macOS only (`aufx Sp94 Spu9`). lv2lint + sord_validate run on Linux only. The VST3 SDK validator must be built from source alongside the SDK, which adds a build step.

**Primary recommendation:** Implement the bus layout override first (it changes plugin behavior and affects all validators), then wire the four validators as hard gates in `plugins.yml`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
Phase 25 was assessed for gray areas and found to be fully specified by v1.7 requirements (PLUG-32..42). No audible, creative, or user-facing decisions remained undecided. User confirmed skip-to-planning.

### Claude's Discretion
- AU manufacturer code and subtype code selection (4-character codes for auval)
- Mono-to-mono output gain factor ((L+R)/2 standard summing vs alternatives)
- Validator CI step ordering and parallelization strategy
- Whether pluginval strictness-7 runs per-format or on a representative subset per OS
- lv2lint and sord_validate installation method (package manager vs build from source)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PLUG-32 | `isBusesLayoutSupported` declares exactly mono-mono, mono-stereo, stereo-stereo | JUCE `isBusesLayoutSupported` API pattern (Architecture Patterns section) |
| PLUG-33 | Mono input duplicated into both L and R reverb inputs | Existing processBlock already does this at line 573-575; needs formal validation |
| PLUG-34 | mono-mono output sums internal L+R back to mono | New code path: `(L+R) * 0.5f` written to single output channel |
| PLUG-35 | All other configurations rejected (sidechain, surround, Atmos, multi-bus) | The `isBusesLayoutSupported` whitelist approach rejects everything not listed |
| PLUG-36 | Plugin loads on a mono track in Logic without auval failure | auval integration + isBusesLayoutSupported correctness |
| PLUG-37 | pluginval strictness-7 on every build/format/OS | CI workflow promotion from advisory to hard gate |
| PLUG-38 | auval on every macOS build | New CI step: `auval -v aufx Sp94 Spu9` |
| PLUG-39 | lv2lint + sord_validate on every Linux LV2 build | New CI step with apt-installed sordi + built-from-source lv2lint |
| PLUG-40 | VST3 SDK validator on every VST3 build | New CI step: build validator from VST3 SDK source |
| PLUG-41 | pluginval RT-safety probe reports zero allocations | Covered by strictness-7 at runtime; existing RT discipline should pass |
| PLUG-42 | Any validator warning or error fails CI | `continue-on-error: false` + strict exit code checks |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Bus layout declaration | Plugin wrapper (PluginProcessor.cpp) | -- | JUCE AudioProcessor API; host negotiates layout via this callback |
| Mono input duplication | Plugin wrapper (processBlock) | -- | Host delivers 1-channel buffer; wrapper duplicates before SRC sandwich |
| Mono output summing | Plugin wrapper (processBlock) | -- | SPU core always outputs stereo; wrapper sums for mono-out hosts |
| pluginval gate | CI (plugins.yml) | -- | GitHub Actions step, no source code |
| auval gate | CI (plugins.yml, macOS runner) | -- | macOS system tool, no source code |
| lv2lint/sord_validate gate | CI (plugins.yml, Linux runner) | -- | Linux-only LV2 validation, no source code |
| VST3 validator gate | CI (plugins.yml) | Build system (CMake) | Must build from VST3 SDK source in CI |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.x (already in project) | AudioProcessor bus layout API | Already the project framework; `isBusesLayoutSupported` is the canonical API [VERIFIED: existing CMakeLists.txt] |
| pluginval | v1.0.4 | Plugin validation (all formats) | Industry standard, already in CI at strictness-1 [VERIFIED: plugins.yml line 32] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| auval | macOS system tool | AU format validation | macOS CI runner only; validates AU component loads in Logic [VERIFIED: macOS system utility] |
| lv2lint | 0.16.2 (latest) | LV2 spec conformance | Linux CI runner only; validates LV2 bundle metadata [CITED: github.com/sfztools/lv2lint] |
| sord_validate | 0.16.18 (sordi package) | RDF/Turtle data validation | Linux CI runner alongside lv2lint [VERIFIED: apt-cache policy sordi shows 0.16.18-1] |
| VST3 SDK validator | matches SDK version | VST3 conformance | All-OS CI; built from Steinberg SDK source [CITED: steinbergmedia.github.io/vst3_dev_portal] |

**Installation (CI only -- no local install needed):**
```bash
# Linux (lv2lint + sord_validate)
sudo apt-get install -y sordi lilv-utils lv2-dev
# lv2lint: not in Ubuntu apt -- must build from source with meson/ninja

# macOS (auval is pre-installed; no action needed)

# VST3 validator: built from VST3 SDK source via CMake target
# (already available as SMTG_RUN_VST_VALIDATOR in the Steinberg CMake)
```

## Architecture Patterns

### System Architecture Diagram

```
                    HOST (DAW)
                       |
          isBusesLayoutSupported()
          returns true/false for
          proposed {in, out} layout
                       |
                 +-----v------+
                 | processBlock|
                 |  (buffer)   |
                 +------+------+
                        |
          buffer.getNumChannels() == 1 or 2
                        |
        +---------------+----------------+
        |                                |
   MONO INPUT (1 ch)             STEREO INPUT (2 ch)
   rawL = ch[0]                  rawL = ch[0]
   rawR = ch[0]  (duplicate)    rawR = ch[1]
        |                                |
        +---------------+----------------+
                        |
              Input Gain -> SRC In -> Core -> SRC Out
              (always stereo internally)
                        |
        +---------------+----------------+
        |                                |
   MONO OUTPUT (1 ch)            STEREO OUTPUT (2 ch)
   ch[0] = (L+R) * 0.5f         ch[0] = L
                                 ch[1] = R
        |                                |
        +---------------+----------------+
                        |
                   Side Limiter
                        |
                  Back to Host
```

### Pattern 1: isBusesLayoutSupported Override

**What:** JUCE calls this to ask whether a proposed channel layout is acceptable. The plugin returns `true` only for the three whitelisted configs.

**When to use:** Always -- this is the single point of control for bus layout negotiation.

**Example:**
```cpp
// Source: JUCE docs (docs.juce.com/master/classjuce_1_1AudioProcessor.html)
// + JUCE tutorial (juce.com/tutorials/tutorial_audio_bus_layouts)
bool SPU94AudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // Reject disabled buses
    if (mainIn == juce::AudioChannelSet::disabled()
        || mainOut == juce::AudioChannelSet::disabled())
        return false;

    // Whitelist exactly three configurations:
    //   mono  -> mono    (1 in, 1 out)
    //   mono  -> stereo  (1 in, 2 out)
    //   stereo -> stereo (2 in, 2 out)
    if (mainIn == juce::AudioChannelSet::mono())
        return mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::stereo();

    if (mainIn == juce::AudioChannelSet::stereo())
        return mainOut == juce::AudioChannelSet::stereo();

    return false;  // reject everything else (surround, Atmos, sidechain, etc.)
}
```

[VERIFIED: JUCE API docs confirm `getMainInputChannelSet()` / `getMainOutputChannelSet()` return `AudioChannelSet` from the proposed layout. JUCE tutorial code patterns match this structure.]

**Critical detail -- BusesProperties constructor:** The constructor declares the *default* layout, not the set of supported layouts. The current stereo-only default is fine as long as `isBusesLayoutSupported` is overridden. Hosts that want mono will propose a different layout, which the override then accepts or rejects. The constructor does NOT need to change to add mono defaults. [CITED: juce.com/tutorials/tutorial_audio_bus_layouts -- "the DAW can change this at any time"]

### Pattern 2: Mono-Aware processBlock

**What:** When the host negotiates a mono layout, `buffer.getNumChannels()` returns 1. The SPU core always operates in stereo internally.

**When to use:** Inside processBlock, at the input-read and output-write boundaries.

**Example (input side -- already mostly implemented):**
```cpp
// Existing code at PluginProcessor.cpp:572-575 already does this:
const float* rawL = buffer.getReadPointer(0);
const float* rawR = buffer.getNumChannels() > 1
                        ? buffer.getReadPointer(1)
                        : buffer.getReadPointer(0);  // mono: duplicate ch[0]
```

**Example (output side -- needs mono-output summing):**
```cpp
// Current code at lines 596-597:
float* hostOutPtrs[2] = {
    buffer.getWritePointer(0),
    buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr
};

// After SRC out, when hostOutPtrs[1] == nullptr (mono output):
// Sum the stereo SPU output into the single mono channel.
if (hostOutPtrs[1] == nullptr)
{
    float* outMono = hostOutPtrs[0];
    for (int i = 0; i < hostNOut; ++i)
        outMono[i] = outMono[i] * 0.5f;  // L already written; add R
    // Wait -- SrcChain.processOut writes L to hostOutPtrs[0] and R to
    // hostOutPtrs[1]. If [1] is nullptr, R is lost. See Pitfall 1 below.
}
```

**This reveals Pitfall 1 (see below).** The current SrcChain.processOut writes to two pointers and jasserts that both are non-null (line 240-241 of SrcChain.cpp). Mono output requires either: (a) providing a scratch buffer as hostOutPtrs[1] and summing afterward, or (b) modifying SrcChain to handle a single output channel.

### Pattern 3: SrcChain Mono Output Handling

**What:** SrcChain.processOut currently asserts two non-null output pointers. For mono output, the caller must supply a scratch buffer for the R channel, then sum L+R into the mono buffer.

**Recommended approach:**
```cpp
// In processBlock, plugin path, mono output case:
float monoScratchR[kMaxBlock];  // stack scratch for discarded R channel
float* hostOutPtrs[2] = {
    buffer.getWritePointer(0),
    buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : monoScratchR
};
int hostNOut = 0;
srcChain_.processOut(coreOutL, coreOutR, coreN, hostOutPtrs, hostNOut);

// If mono output, sum L+R -> mono
if (buffer.getNumChannels() == 1)
{
    float* out = buffer.getWritePointer(0);
    for (int i = 0; i < hostNOut; ++i)
        out[i] = (out[i] + monoScratchR[i]) * 0.5f;
}
```

[ASSUMED: The (L+R)/2 summing convention. Standard practice for stereo-to-mono downmix. The 0.5 factor prevents clipping on correlated stereo signals.]

### Pattern 4: Side-Channel Limiter Mono Awareness

**What:** The existing side-channel limiter at the end of processBlock's plugin path (lines 619-632) computes `mid = 0.5*(L+R)` and `side = 0.5*(L-R)`, then clamps the side. For mono output, side is zero by definition (L == R after the mono summing step), so the limiter becomes a no-op identity transform. It should be skipped for mono output.

**Example:**
```cpp
if (buffer.getNumChannels() > 1)
{
    // existing side-channel limiter (unchanged)
    float* outL = hostOutPtrs[0];
    float* outR = hostOutPtrs[1];
    for (int i = 0; i < n; ++i) { /* ... mid-side limiting ... */ }
}
// For mono: side limiter is identity -- skip entirely.
```

### Anti-Patterns to Avoid
- **Changing BusesProperties constructor to declare mono default:** Not necessary and can break existing sessions that saved with stereo layout. The override handles everything. [CITED: JUCE bus layout tutorial]
- **Checking `getTotalNumInputChannels()` instead of `buffer.getNumChannels()`:** These return different values in some edge cases. Use `buffer.getNumChannels()` for the actual buffer handed to processBlock. [ASSUMED]
- **Running auval in CI on Linux/Windows runners:** auval is macOS-only (Apple system utility). It will simply not exist on other platforms.
- **Building lv2lint from source on macOS/Windows:** LV2 is Linux-only in this project. lv2lint is a Linux-only CI step.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Plugin validation | Custom test harness for load/scan | pluginval | Tests hundreds of host behaviors (parameter fuzz, state restore, RT-safety) that a custom harness would miss |
| AU validation | Manual Logic testing | auval CLI | auval is what Logic actually uses internally; if auval passes, Logic will load the plugin |
| LV2 metadata validation | Manual Turtle inspection | lv2lint + sord_validate | Catches RDF violations, missing properties, broken URIs that manual review would miss |
| VST3 conformance | Manual host testing | Steinberg SDK validator | Tests VST3 interface contract compliance at the API level |

**Key insight:** Validators catch bugs that no amount of "it works in my DAW" testing would find -- they exercise edge cases in the format spec that real hosts trigger unpredictably.

## Common Pitfalls

### Pitfall 1: SrcChain.processOut Asserts Two Non-Null Output Pointers
**What goes wrong:** When processBlock is called with a mono buffer (`getNumChannels() == 1`), passing `nullptr` as hostOutPtrs[1] to `srcChain_.processOut` hits the `jassert(hostIn[1] != nullptr)` at SrcChain.cpp:240.
**Why it happens:** SrcChain was designed for stereo-only operation (Phase 22).
**How to avoid:** Provide a stack-allocated scratch buffer for the R channel in mono-output mode. Sum L + scratch_R into the mono output after SrcChain returns. Do NOT modify SrcChain.processOut's contract -- it's simpler to adapt the caller.
**Warning signs:** jassert failure in debug builds; undefined behavior in release builds (writes to nullptr).

### Pitfall 2: AU Bus Layout Mismatch Between Constructor and isBusesLayoutSupported
**What goes wrong:** AU wrapper reports different channel configurations than what `isBusesLayoutSupported` accepts. auval fails the channel-config phase with "doesn't support mono" even though the override says it does.
**Why it happens:** Known JUCE bug area -- the AU wrapper has historically had issues where it probes layouts that `isBusesLayoutSupported` should accept but the wrapper rejects due to internal caching. Multiple JUCE forum threads (#34326, #68058, #61894) document this. [CITED: forum.juce.com/t/isbuseslayoutsupported-auval-and-mono-mono-configuration-solved/34326]
**How to avoid:** (1) Keep the override simple and deterministic. (2) Run auval in CI immediately after implementing the override. (3) If auval fails despite correct code, check JUCE version-specific workarounds on the forum.
**Warning signs:** "auval -v aufx Sp94 Spu9" returns nonzero; Logic silently refuses to show the plugin.

### Pitfall 3: pluginval Strictness-7 Parameter Thread Safety Test
**What goes wrong:** The "Parameter thread safety" test at strictness 7 calls `setValue` on parameters from multiple threads concurrently. If any parameter read/write path touches non-atomic state or allocates, pluginval reports a failure.
**Why it happens:** Phase 24's parameter bridge uses `std::atomic` reads from the audio thread and `setValueNotifyingHost` from the file-preset-load path inside processBlock. The `setValueNotifyingHost` call is documented as safe from the audio thread in JUCE, but it triggers a notification chain that could allocate in some JUCE versions.
**How to avoid:** Verify Phase 24's parameter implementation passes strictness-7 before adding the hard gate. If it fails, the fix belongs in a pre-phase-25 remediation, not in this phase.
**Warning signs:** pluginval reports "thread safety" failure; may be intermittent (concurrency-dependent).

### Pitfall 4: lv2lint "Not in Apt" on Ubuntu CI Runner
**What goes wrong:** `apt-get install lv2lint` fails because lv2lint is not in Ubuntu's standard repositories.
**Why it happens:** lv2lint is packaged in Arch Linux, Alpine, and Gentoo but not in Debian/Ubuntu. [VERIFIED: apt-cache search on Ubuntu returns no lv2lint package]
**How to avoid:** Build lv2lint from source in the CI workflow using meson/ninja. Cache the built binary for speed. Dependencies: `liblilv-dev`, `libcurl4-openssl-dev`, `libelf-dev`, `libx11-dev`, `meson`, `ninja-build`.
**Warning signs:** CI step fails with "package not found."

### Pitfall 5: VST3 Validator Must Be Built From Source
**What goes wrong:** There is no prebuilt binary of the Steinberg VST3 validator available for download. Unlike pluginval (which ships prebuilt zips per OS), the validator must be compiled from the VST3 SDK source.
**Why it happens:** Steinberg distributes it as source code only, integrated into their CMake build system. [CITED: steinbergmedia.github.io/vst3_dev_portal/pages/What+is+the+VST+3+SDK/Validator.html]
**How to avoid:** Add a CI step that clones the VST3 SDK, configures with CMake (`-DSMTG_ADD_VST3_HOSTING_SAMPLES=ON`), builds the `validator` target, then runs it against the built .vst3 bundle. Cache the built validator binary between CI runs.
**Warning signs:** Missing binary; long initial CI build time.

### Pitfall 6: CLAP Features List Missing "mono" Tag
**What goes wrong:** CLAP hosts that filter by capabilities may not show the plugin for mono tracks because the CLAP_FEATURES list only declares "stereo".
**Why it happens:** Current CMakeLists.txt line 106 declares `CLAP_FEATURES audio-effect stereo reverb` -- no "mono" tag.
**How to avoid:** Add "mono" to the CLAP_FEATURES: `CLAP_FEATURES audio-effect mono stereo reverb`. [VERIFIED: CLAP spec defines `CLAP_PLUGIN_FEATURE_MONO = "mono"` in plugin-features.h]
**Warning signs:** Plugin invisible on mono CLAP tracks in Bitwig or other CLAP hosts.

### Pitfall 7: Standalone Wrapper's Mono Behavior
**What goes wrong:** The standalone path (lines 482-541) has its own channel handling that's separate from the plugin path. Adding mono support to the plugin path doesn't automatically fix the standalone path.
**Why it happens:** The standalone is a separate code branch gated by `isStandalone`.
**How to avoid:** Leave the standalone path unchanged -- it's a dev testbed, not a user deliverable. The standalone always runs stereo (its audio device is configured for stereo). Only the plugin path needs mono handling.
**Warning signs:** None expected -- standalone is out of scope for PLUG-32..36.

## Code Examples

### Example 1: Complete isBusesLayoutSupported Implementation
```cpp
// Source: Pattern derived from JUCE docs + tutorial
// (docs.juce.com/master/classjuce_1_1AudioProcessor.html)
bool SPU94AudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // Never accept disabled buses
    if (mainIn  == juce::AudioChannelSet::disabled()) return false;
    if (mainOut == juce::AudioChannelSet::disabled()) return false;

    // Mono in -> mono out:   OK (PLUG-32, PLUG-34)
    // Mono in -> stereo out: OK (PLUG-32, PLUG-33)
    if (mainIn == juce::AudioChannelSet::mono())
        return (mainOut == juce::AudioChannelSet::mono()
             || mainOut == juce::AudioChannelSet::stereo());

    // Stereo in -> stereo out: OK (PLUG-32)
    if (mainIn == juce::AudioChannelSet::stereo())
        return (mainOut == juce::AudioChannelSet::stereo());

    // Everything else (surround, Atmos, sidechain, multi-bus): rejected (PLUG-35)
    return false;
}
```

### Example 2: processBlock Mono-Output Summing
```cpp
// Inside the plugin path of processBlock, after SRC output:
const bool monoOutput = (buffer.getNumChannels() == 1);

// Provide scratch for R channel when output is mono
float monoRScratch[kMaxBlock];
float* hostOutPtrs[2] = {
    buffer.getWritePointer(0),
    monoOutput ? monoRScratch : buffer.getWritePointer(1)
};
int hostNOut = 0;
srcChain_.processOut(coreOutL, coreOutR, coreN, hostOutPtrs, hostNOut);

if (monoOutput)
{
    // PLUG-34: sum stereo SPU output to mono
    float* out = buffer.getWritePointer(0);
    for (int i = 0; i < hostNOut; ++i)
        out[i] = (out[i] + monoRScratch[i]) * 0.5f;
}
```

### Example 3: auval CI Step
```yaml
# macOS only -- auval is an Apple system utility
- name: Validate AU (auval)
  if: matrix.name == 'macos'
  run: |
    auval -v aufx Sp94 Spu9
```

### Example 4: pluginval Strictness-7 Hard Gate
```yaml
- name: Validate (pluginval strictness-7)
  run: |
    pluginval --validate-in-process --strictness-level 7 \
      --timeout-ms 120000 \
      "${PLUGIN_ARTEFACTS_DIR}/VST3/SPU-94.vst3"
```

### Example 5: lv2lint CI Step (Linux only)
```bash
# sord_validate: checks RDF/Turtle conformance
sord_validate \
  $(find /usr/lib/lv2 -name '*.ttl') \
  "${PLUGIN_ARTEFACTS_DIR}/LV2/SPU-94.lv2"/*.ttl

# lv2lint: checks LV2 spec conformance
lv2lint -I "${PLUGIN_ARTEFACTS_DIR}/LV2/SPU-94.lv2" \
  -M nopack \
  "https://spu94project.org/spu94"
```

### Example 6: VST3 SDK Validator CI Step
```bash
# After building the validator from the VST3 SDK:
./validator "${PLUGIN_ARTEFACTS_DIR}/VST3/SPU-94.vst3"
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| pluginval strictness-1 smoke only | strictness-7 as hard gate | Phase 25 (this phase) | Catches RT-safety violations, parameter thread safety, state restoration bugs |
| No AU validation in CI | auval wired as hard gate | Phase 25 | Prevents Logic/GarageBand silent-reject regressions |
| No LV2 validation | lv2lint + sord_validate in CI | Phase 25 | Catches LV2 metadata/spec violations |
| No VST3 SDK validation | Steinberg validator in CI | Phase 25 | Catches VST3 interface contract violations |
| Stereo-only bus layout | Three-config whitelist | Phase 25 | Plugin usable on mono tracks in all hosts |

## Validator Details

### pluginval Strictness-Level 7 Test Map

Tests at strictness 7 include everything from levels 1-6 plus:

| Test Name | Min Level | What It Checks |
|-----------|-----------|----------------|
| Plugin info | 1 | Basic metadata, name, categories |
| Automatable parameters | 2 | Parameter enumeration, ranges |
| Plugin programs | 2 | Preset enumeration |
| Editor | 2 | GUI creation/destruction |
| Plugin state | 2 | State save/load basics |
| Audio processing | 3 | Process block with note events, NaN/Inf/subnormal detection |
| Automation | 3 | Parameter changes during processing |
| Editor whilst processing | 4 | GUI + audio simultaneous |
| auval | 5 | AU validation (macOS only, `auval -strict -stress 20 -v`) |
| vst3 validator | 5 | VST3 validation with `-e` flag |
| Editor automation | 5 | GUI during automation |
| Non-releasing audio processing | 6 | Process without releaseResources between sample rate changes |
| Plugin state restoration | 6 | Exact binary state matching after restore |
| **All parameters** | **7** | **Exhaustive parameter fuzz test** |
| **Background thread state** | **7** | **State save/load from background thread** |
| **Parameter thread safety** | **7** | **Concurrent setValue from multiple threads** |

[CITED: github.com/Tracktion/pluginval/blob/develop/Source/tests/BasicTests.cpp]

**RT-safety note:** pluginval's malloc interception is currently only fully supported on macOS (uses `rtcheck` library). Linux support is still under development. On Linux, the strictness-7 gate catches concurrency and state bugs but may not catch all malloc-in-audio-thread violations. [CITED: forum.juce.com/t/pluginval-real-time-safety-checking/67439]

### auval Command Format

```
auval -v aufx <subtype> <manufacturer>
```

For SPU-94:
- Type: `aufx` (Audio Unit Effect)
- Subtype: `Sp94` (from PLUGIN_CODE in CMakeLists.txt)
- Manufacturer: `Spu9` (from PLUGIN_MANUFACTURER_CODE in CMakeLists.txt)

Full command: `auval -v aufx Sp94 Spu9`

[VERIFIED: CMakeLists.txt lines 31-32 confirm PLUGIN_MANUFACTURER_CODE=Spu9, PLUGIN_CODE=Sp94]

### lv2lint Installation on Ubuntu CI

lv2lint is NOT available in Ubuntu's apt repositories. [VERIFIED: `apt-cache search lv2lint` returns nothing on the local system]

Build-from-source steps for CI:
```bash
sudo apt-get install -y meson ninja-build liblilv-dev libcurl4-openssl-dev
git clone --depth 1 https://gitlab.com/drobilla/lv2lint.git
cd lv2lint
meson setup build -Donline-tests=disabled -Delf-tests=disabled -Dx11-tests=disabled
ninja -C build
sudo ninja -C build install
```

Alternatively, `sord_validate` IS available via apt:
```bash
sudo apt-get install -y sordi
```
[VERIFIED: `apt-cache policy sordi` shows candidate 0.16.18-1]

### VST3 SDK Validator

The validator is a command-line host that checks VST3 conformity. It is source-only (no prebuilt binaries). [CITED: steinbergmedia.github.io/vst3_dev_portal/pages/What+is+the+VST+3+SDK/Validator.html]

CLI flags:
- `-e` : run extensive tests
- `-q` : only print errors
- `-suite [name]` : run only a specific test suite
- `-l` : use local instance per test
- `-cid` : only test a specific class ID

For CI, the standard invocation is:
```bash
./validator "${PLUGIN_ARTEFACTS_DIR}/VST3/SPU-94.vst3"
```

Building requires cloning the VST3 SDK repo, configuring with CMake, and building the `validator` target. The SDK uses git submodules. On macOS, Xcode generator is standard; on Linux/Windows, Ninja works.

**Practical consideration:** The VST3 SDK validator is heavyweight to build in CI (full SDK compile). Consider building it once and caching the binary, or evaluating whether pluginval's built-in VST3 validation at strictness >= 5 is sufficient (pluginval internally invokes the Steinberg validator logic). If pluginval at strictness 7 already covers the VST3 interface contract, a separate Steinberg validator step may be redundant.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | (L+R)/2 is the correct mono summing convention | Pattern 3, Example 2 | Mono output 6 dB too quiet or too loud; easily corrected post-verification |
| A2 | `buffer.getNumChannels()` reliably reflects the negotiated layout in processBlock | Pattern 2 | Mono path never triggers; would need `getTotalNumOutputChannels()` instead |
| A3 | pluginval at strictness >= 5 already invokes VST3 validator logic internally | Validator Details | If not, the standalone Steinberg validator step is mandatory rather than potentially redundant |
| A4 | Side-channel limiter is identity for mono output and can be skipped | Pattern 4 | If not skipped, it still produces correct output (identity); just wastes CPU |
| A5 | The standalone path does not need mono handling changes | Pitfall 7 | Standalone crashes on mono -- but standalone is stereo-only, so no practical risk |

## Open Questions

1. **Does pluginval's built-in VST3 validation at strictness >= 5 fully replicate the Steinberg validator?**
   - What we know: pluginval's BasicTests.cpp shows a "vst3 validator" test at strictness 5+ that invokes validation with `-e` flag.
   - What's unclear: Whether this is byte-identical to the standalone Steinberg validator, or a subset.
   - Recommendation: Implement the Steinberg validator CI step as specified by PLUG-40, but if it proves too expensive to build, verify that pluginval's built-in VST3 test at strictness 7 provides equivalent coverage.

2. **Will Phase 24's setValueNotifyingHost calls from processBlock survive pluginval's Parameter thread safety test?**
   - What we know: Phase 24 calls `setValueNotifyingHost` from the audio thread during file-preset loads.
   - What's unclear: Whether JUCE's notification chain allocates or locks in some code paths.
   - Recommendation: Run pluginval strictness-7 on the current build BEFORE implementing Phase 25's hard gate. If it fails, fix Phase 24's code first.

3. **lv2lint build-from-source reliability on GitHub Actions ubuntu-22.04 runner**
   - What we know: lv2lint requires meson, liblilv-dev, and builds with ninja. The upstream repo is maintained.
   - What's unclear: Whether the build is stable on ubuntu-22.04's meson/lilv versions.
   - Recommendation: Test the build locally on ubuntu-22.04 first. Cache the built binary in CI.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| pluginval | PLUG-37, PLUG-41 | In CI (downloaded) | v1.0.4 | -- |
| auval | PLUG-38 | macOS system tool | System | -- (macOS-only) |
| sordi (sord_validate) | PLUG-39 | In apt | 0.16.18-1 | -- |
| lv2lint | PLUG-39 | NOT in apt | -- | Build from source in CI |
| VST3 SDK validator | PLUG-40 | NOT prebuilt | -- | Build from SDK source in CI |
| meson + ninja | lv2lint build | In apt | System | -- |
| liblilv-dev | lv2lint build | In apt | System | -- |

**Missing dependencies with no fallback:**
- None blocking

**Missing dependencies with fallback:**
- lv2lint: build from source (meson/ninja, ~30s compile)
- VST3 validator: build from SDK source (CMake target, cacheable)

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest (CMake) + pluginval + auval + lv2lint + VST3 validator |
| Config file | `tests/plugin/CMakeLists.txt` (CTest), `.github/workflows/plugins.yml` (CI) |
| Quick run command | `ctest --test-dir build -R state` |
| Full suite command | `ctest --test-dir build` + pluginval strictness-7 on all built formats |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PLUG-32 | isBusesLayoutSupported whitelist | unit | `ctest -R bus_layout` | No -- Wave 0 |
| PLUG-33 | Mono input duplication | integration | pluginval mono-track test (implicit) | Covered by pluginval |
| PLUG-34 | Mono output summing | unit | `ctest -R mono_sum` | No -- Wave 0 |
| PLUG-35 | Reject non-whitelisted configs | unit | `ctest -R bus_layout` (same as PLUG-32) | No -- Wave 0 |
| PLUG-36 | Logic mono track load | integration | `auval -v aufx Sp94 Spu9` | CI step |
| PLUG-37 | pluginval strictness-7 | integration | `pluginval --strictness-level 7 ...` | CI step (existing advisory promoted) |
| PLUG-38 | auval | integration | `auval -v aufx Sp94 Spu9` | CI step (new) |
| PLUG-39 | lv2lint + sord_validate | integration | `lv2lint -I ... URI` + `sord_validate ...` | CI step (new) |
| PLUG-40 | VST3 validator | integration | `./validator SPU-94.vst3` | CI step (new) |
| PLUG-41 | RT-safety zero allocations | integration | pluginval strictness-7 (implicit) | Covered by PLUG-37 |
| PLUG-42 | Warnings/errors fail CI | CI config | `continue-on-error: false` | CI config change |

### Sampling Rate
- **Per task commit:** Quick CTest run on bus layout unit tests
- **Per wave merge:** Full CTest suite + pluginval strictness-7 on VST3
- **Phase gate:** All validators green on all 3 OS runners

### Wave 0 Gaps
- [ ] `tests/plugin/test_bus_layout.cpp` -- covers PLUG-32, PLUG-35 (unit test instantiating the processor and checking `checkBusesLayoutSupported` for all expected configs)
- [ ] `tests/plugin/test_mono_sum.cpp` -- covers PLUG-34 (unit test verifying (L+R)/2 summing accuracy)
- [ ] Update `tests/plugin/CMakeLists.txt` to build and register the new test targets

## Sources

### Primary (HIGH confidence)
- JUCE AudioProcessor API docs: `isBusesLayoutSupported`, `BusesLayout`, `AudioChannelSet` [CITED: docs.juce.com/master/classjuce_1_1AudioProcessor.html]
- JUCE bus layout tutorial [CITED: juce.com/tutorials/tutorial_audio_bus_layouts]
- pluginval BasicTests.cpp strictness levels [CITED: github.com/Tracktion/pluginval/blob/develop/Source/tests/BasicTests.cpp]
- Existing project source: PluginProcessor.cpp, SrcChain.h/cpp, BoundaryConverter.h, CMakeLists.txt [VERIFIED: local codebase]
- Existing CI workflow: .github/workflows/plugins.yml [VERIFIED: local codebase]

### Secondary (MEDIUM confidence)
- JUCE forum thread on AU + isBusesLayoutSupported mono bug [CITED: forum.juce.com/t/isbuseslayoutsupported-auval-and-mono-mono-configuration-solved/34326]
- JUCE forum thread on AU ignoring bus layout support [CITED: forum.juce.com/t/au-ignores-isbuseslayoutsupported-only-shows-stereo-despite-vst3-working-correctly/68058]
- Steinberg VST3 validator portal [CITED: steinbergmedia.github.io/vst3_dev_portal/pages/What+is+the+VST+3+SDK/Validator.html]
- VST3 validator CI article [CITED: dev.classmethod.jp/en/articles/vst3-ci-using-validator-on-github-actions/]
- lv2lint README [CITED: github.com/sfztools/lv2lint]
- pluginval RT-safety discussion [CITED: forum.juce.com/t/pluginval-real-time-safety-checking/67439]
- CLAP plugin-features.h [VERIFIED: build/_deps/clap-juce-extensions-src/clap-libs/clap/include/clap/plugin-features.h]

### Tertiary (LOW confidence)
- pluginval's internal VST3 validation coverage vs standalone Steinberg validator [needs verification -- see Open Question 1]
- `buffer.getNumChannels()` vs `getTotalNumOutputChannels()` reliability in processBlock [needs practical verification]

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all tools identified and verified against existing project infrastructure
- Architecture: HIGH -- bus layout API well-documented; existing code already handles most mono cases
- Pitfalls: HIGH -- grounded in specific code analysis (SrcChain jassert, auval AU wrapper bugs, lv2lint unavailability in apt)
- Validation: MEDIUM-HIGH -- pluginval strictness levels verified from source; VST3 validator build process needs practical CI testing

**Research date:** 2026-05-12
**Valid until:** 2026-06-12 (stable domain; JUCE API and validator tools change slowly)
