---
phase: 08-m4-juce-plugin-product-v1-0
plan: 01
subsystem: build-system
tags: [juce, cmake, fetchcontent, standalone, audio-processor, cpp17]

# Dependency graph
requires:
  - phase: 07-verification-hardening
    provides: "82-test ctest suite, libspu94 shared library (spu94_shared)"
provides:
  - "JUCE 8.0.12 integrated via FetchContent with pinned SHA"
  - "Standalone executable target (spu94_standalone_Standalone)"
  - "Empty SPU94AudioProcessor shell with processBlock/prepareToPlay stubs"
  - "Empty SPU94AudioProcessorEditor shell (800x600 window, 'SPU-94' title text)"
  - "C++17 build support alongside existing C11 core"
affects: [08-02, 08-03, 08-04]

# Tech tracking
tech-stack:
  added: [JUCE 8.0.12, C++17]
  patterns: [FetchContent pinned-SHA dependency, juce_add_plugin standalone format, extern-C linkage for C library in C++ target]

key-files:
  created:
    - src/standalone/CMakeLists.txt
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "JUCE 8.0.12 pinned to SHA 29396c22 via GIT_TAG for byte-reproducibility"
  - "juce_generate_juce_header used for JuceHeader.h (JUCE 8 CMake API)"
  - "BUNDLE_ID set explicitly to avoid JUCE warning about spaces in auto-generated ID"
  - "JUCE_DISPLAY_SPLASH_SCREEN define removed (deprecated in JUCE 8, generates pragma warning)"
  - "using AudioProcessor::processBlock added to suppress -Woverloaded-virtual for double processBlock overload"

patterns-established:
  - "FetchContent for JUCE: placed after CMAKE_POSITION_INDEPENDENT_CODE, before include(spu94_warnings.cmake)"
  - "Standalone CMake: juce_add_plugin with FORMATS Standalone only, linking spu94_shared PRIVATE"
  - "C API in C++ context: extern \"C\" { #include <spu94/spu94.h> } pattern for type-safe interop"

requirements-completed: [STANDALONE-01, STANDALONE-08, STANDALONE-09]

# Metrics
duration: 61min
completed: 2026-04-25
---

# Phase 8 Plan 01: JUCE Build Scaffolding Summary

**JUCE 8.0.12 standalone build target producing a launchable Linux binary with empty AudioProcessor/Editor shell linking unmodified libspu94**

## Performance

- **Duration:** ~61 min (dominated by JUCE FetchContent clone + full ctest run)
- **Started:** 2026-04-25T19:51:23Z
- **Completed:** 2026-04-25T20:52:22Z
- **Tasks:** 2 (1 checkpoint:human-action pre-resolved, 1 auto)
- **Files modified:** 6 (1 modified, 5 created)

## Accomplishments
- JUCE 8.0.12 integrated into existing CMake build via FetchContent with pinned SHA for reproducibility
- Root CMakeLists.txt extended (not replaced) with CXX language, C++17 standard, and add_subdirectory(src/standalone)
- Standalone target builds and produces a 30MB Linux ELF executable at build/src/standalone/spu94_standalone_artefacts/Standalone/SPU-94
- Empty AudioProcessor shell with buffer.clear() processBlock and empty prepareToPlay/releaseResources stubs ready for Plan 02
- Empty AudioProcessorEditor shell with 800x600 window rendering "SPU-94" centered text ready for Plan 03
- All 80/82 existing tests pass (2 pre-existing packaging test timeouts unrelated to changes)
- Zero changes to C core (src/spu94/, include/spu94/)

## Task Commits

Each task was committed atomically:

1. **Task 1: Install JUCE Linux build dependencies** - Pre-resolved checkpoint (all 7 packages confirmed installed)
2. **Task 2: Create JUCE build scaffolding and empty AudioProcessor shell** - `abce83c` (feat)

## Files Created/Modified
- `CMakeLists.txt` - Extended with cmake 3.22, CXX language, C++17, JUCE FetchContent, add_subdirectory(src/standalone)
- `src/standalone/CMakeLists.txt` - juce_add_plugin standalone target linking spu94_shared with JUCE compile definitions
- `src/standalone/PluginProcessor.h` - SPU94AudioProcessor class declaration with all AudioProcessor overrides
- `src/standalone/PluginProcessor.cpp` - Shell implementation: buffer.clear() processBlock, getName returns "SPU-94", createPluginFilter entry point
- `src/standalone/PluginEditor.h` - SPU94AudioProcessorEditor class declaration with paint/resized overrides
- `src/standalone/PluginEditor.cpp` - Shell implementation: 800x600 window, dark grey background, centered "SPU-94" text

## Decisions Made
- **JUCE 8.0.12 pinned SHA:** Used `GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850` (resolved via git ls-remote) for byte-reproducibility, not the tag name
- **juce_generate_juce_header:** JUCE 8's CMake API does not auto-generate JuceHeader.h; explicit call required after juce_add_plugin
- **BUNDLE_ID explicit:** JUCE auto-generates BUNDLE_ID from COMPANY_NAME, but "SPU-94 Project" contains a space which triggers a CMake warning; set explicitly to "com.spu94project.spu94standalone"
- **JUCE_DISPLAY_SPLASH_SCREEN removed:** JUCE 8 dropped the splash screen entirely; the define triggers a pragma message warning
- **using processBlock declaration:** JUCE 8's AudioProcessor has both float and double processBlock overloads; overriding only float hides the double one, triggering -Woverloaded-virtual; `using` declaration resolves it

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] JuceHeader.h not found with JUCE 8 CMake API**
- **Found during:** Task 2 (first build attempt)
- **Issue:** JUCE 8's CMake API does not auto-generate JuceHeader.h; `#include <JuceHeader.h>` fails with "fatal error: JuceHeader.h: No such file or directory"
- **Fix:** Added `juce_generate_juce_header(spu94_standalone)` after `juce_add_plugin` in src/standalone/CMakeLists.txt
- **Files modified:** src/standalone/CMakeLists.txt
- **Verification:** Build succeeds with JuceHeader.h generated at build/src/standalone/spu94_standalone_artefacts/JuceLibraryCode/JuceHeader.h
- **Committed in:** abce83c

**2. [Rule 1 - Bug] JUCE_DISPLAY_SPLASH_SCREEN deprecated pragma warning**
- **Found during:** Task 2 (first build attempt)
- **Issue:** JUCE 8 no longer uses a splash screen; defining JUCE_DISPLAY_SPLASH_SCREEN=0 emits `#pragma message` warning
- **Fix:** Removed `JUCE_DISPLAY_SPLASH_SCREEN=0` from target_compile_definitions
- **Files modified:** src/standalone/CMakeLists.txt
- **Verification:** Rebuild produces no warnings from JUCE splash screen code
- **Committed in:** abce83c

**3. [Rule 1 - Bug] BUNDLE_ID space warning from COMPANY_NAME**
- **Found during:** Task 2 (cmake configure)
- **Issue:** JUCE auto-generates BUNDLE_ID from COMPANY_NAME "SPU-94 Project", producing "com.SPU-94 Project.spu94_standalone" which contains spaces
- **Fix:** Added explicit `BUNDLE_ID "com.spu94project.spu94standalone"` to juce_add_plugin
- **Files modified:** src/standalone/CMakeLists.txt
- **Verification:** cmake configure produces no BUNDLE_ID warning
- **Committed in:** abce83c

**4. [Rule 1 - Bug] -Woverloaded-virtual warning on processBlock**
- **Found during:** Task 2 (build with warnings)
- **Issue:** Overriding only processBlock(float) hides the virtual processBlock(double) overload in JUCE's AudioProcessor base class
- **Fix:** Added `using juce::AudioProcessor::processBlock;` in PluginProcessor.h
- **Files modified:** src/standalone/PluginProcessor.h
- **Verification:** Rebuild produces zero warnings in our source files
- **Committed in:** abce83c

**5. [Rule 1 - Bug] -Wshadow warning on constructor parameter**
- **Found during:** Task 2 (build with warnings)
- **Issue:** Constructor parameter named `processor` shadows JUCE's `AudioProcessorEditor::processor` member
- **Fix:** Renamed constructor parameter from `processor` to `p`
- **Files modified:** src/standalone/PluginEditor.cpp
- **Verification:** Rebuild produces zero warnings in our source files
- **Committed in:** abce83c

---

**Total deviations:** 5 auto-fixed (5 Rule 1 bugs)
**Impact on plan:** All fixes necessary for clean compilation with JUCE 8's CMake API. No scope creep.

## Issues Encountered
- JUCE FetchContent clone takes ~4 minutes on first run (JUCE repo is large even with GIT_SHALLOW TRUE). Subsequent cmake configure runs are near-instant.
- Two pre-existing packaging tests (test_packaging_editable_install, test_packaging_wheel_tag) timeout at 300s. These are unrelated to our changes -- they test scikit-build-core wheel operations which are inherently slow. All 80 other tests pass.

## User Setup Required
None - JUCE Linux build dependencies were pre-installed (libasound2-dev, libjack-jackd2-dev, libcurl4-openssl-dev, libfontconfig1-dev, libwebkit2gtk-4.1-dev, libglu1-mesa-dev, mesa-common-dev).

## Known Stubs
- `PluginProcessor::prepareToPlay` - empty body; Plan 02 fills with spu94_init + work buffer allocation
- `PluginProcessor::releaseResources` - empty body; Plan 02 fills with spu94_destroy + teardown
- `PluginProcessor::processBlock` - calls buffer.clear() only; Plan 02 fills with float-to-int16 conversion + spu94_process
- `PluginProcessor::getStateInformation` - empty body; Plan 03 fills with register state serialization
- `PluginProcessor::setStateInformation` - empty body; Plan 03 fills with register state deserialization
- `PluginEditor::resized` - empty body; Plan 03 fills with register slider + preset dropdown layout

All stubs are intentional shell placeholders per the plan's scaffolding objective. Each has a documented future plan that will wire the real implementation.

## Next Phase Readiness
- Build infrastructure complete: Plans 02-04 can link against spu94_shared via the established CMake target
- AudioProcessor shell ready for Plan 02 to fill prepareToPlay/processBlock with spu94_init + spu94_process
- AudioProcessorEditor shell ready for Plan 03 to add register sliders, preset dropdown, and Wet/Dry knob
- JUCE is cached in build/_deps/juce-src after first configure; subsequent builds are fast

## Self-Check: PASSED

All 7 files exist. Commit abce83c verified. All 19 acceptance criteria pass. Binary at build/src/standalone/spu94_standalone_artefacts/Standalone/SPU-94 is a 30MB ELF executable. No changes to C core (src/spu94/, include/spu94/).

---
*Phase: 08-m4-juce-plugin-product-v1-0*
*Completed: 2026-04-25*
