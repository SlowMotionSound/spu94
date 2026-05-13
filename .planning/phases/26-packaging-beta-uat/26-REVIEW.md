---
phase: 26-packaging-beta-uat
reviewed: 2026-05-13T19:42:00Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - src/plugin/PluginEditor.cpp
  - BETA-README.md
  - .github/workflows/release.yml
  - packaging/linux/install.sh
  - packaging/macos/create_dmg.sh
  - packaging/macos/create_pkg.sh
  - packaging/windows/spu94.iss
findings:
  critical: 1
  warning: 2
  info: 1
  total: 4
status: issues_found
---

# Phase 26: Code Review Report

**Reviewed:** 2026-05-13T19:42:00Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Seven files reviewed covering the Phase 26 packaging and beta UAT deliverables: the CI release workflow, three platform-specific packaging scripts (Linux, macOS x2, Windows), the Inno Setup installer definition, the beta README, and the plugin editor C++ source.

The packaging shell scripts (Linux, macOS) are well-structured with proper error handling (`set -e` / `set -euo pipefail`), source validation, and cleanup traps. The CI workflow has good practices (SHA-pinned actions, concurrency control, fail-fast disabled for cross-platform matrix). The PluginEditor.cpp is mature and correctly structured.

One critical defect was found: the Windows Inno Setup script will fail at build time because its `Source:` paths resolve relative to the `.iss` file location, not the repository root where the workflow stages artifacts. Two warnings cover a missing macOS architecture breadth decision and an edge case in the PluginEditor touch-create cleanup logic.

## Critical Issues

### CR-01: Windows Inno Setup Source path mismatch -- build will fail

**File:** `packaging/windows/spu94.iss:24-26` cross-referenced with `.github/workflows/release.yml:165-169`
**Issue:** The Inno Setup `.iss` file has no `SourceDir` directive. By default, Inno Setup resolves `Source:` paths relative to the directory containing the `.iss` file itself (`packaging\windows\`). The `[Files]` section references:

```
Source: "artifacts\VST3\SPU-94.vst3\*"
Source: "artifacts\CLAP\SPU-94.clap"
```

These resolve to `packaging\windows\artifacts\VST3\...` and `packaging\windows\artifacts\CLAP\...`.

However, the release workflow (line 165-168) stages artifacts at the **repository root**:

```cmd
mkdir artifacts\VST3
mkdir artifacts\CLAP
xcopy /E /I "%PLUGIN_ARTEFACTS_DIR%\VST3\SPU-94.vst3" "artifacts\VST3\SPU-94.vst3\"
copy "%PLUGIN_ARTEFACTS_DIR%\CLAP\SPU-94.clap" "artifacts\CLAP\SPU-94.clap"
```

Then invokes `iscc ... packaging\windows\spu94.iss`. The iscc compiler will look for files at `packaging\windows\artifacts\...`, find nothing, and fail with a "Source file not found" error. The Windows installer will never build successfully.

**Fix:** Add a `SourceDir` directive pointing to the repository root. Since the workflow invokes iscc from the repo root, use `{#SourcePath}\..\..\` to climb from `packaging\windows\` to the repo root:

```ini
[Setup]
; ... existing setup directives ...
SourceDir=..\..\
OutputDir=packaging\windows\Output
OutputBaseFilename=SPU-94-Setup
```

Alternatively, change the workflow to stage artifacts into `packaging\windows\artifacts\` instead of the repo root. The `SourceDir` approach is cleaner since it keeps the `.iss` file self-contained.

## Warnings

### WR-01: macOS build produces arm64-only binaries -- no Intel Mac support

**File:** `.github/workflows/release.yml:43`
**Issue:** The macOS matrix entry uses `macos-14`, which is an Apple Silicon (ARM) runner. The CMake configuration has no `CMAKE_OSX_ARCHITECTURES` override, so the build produces arm64-only binaries. Intel Mac users (still a significant portion of audio production setups) will be unable to load these plugins. The plugin will either fail to load silently or show an architecture mismatch error in the DAW's plugin scanner.

**Fix:** Either build a universal binary by adding the CMake flag, or add a second macOS matrix entry for x86_64. Universal binary approach (recommended for plugin distribution):

```yaml
      - name: Configure (Linux/macOS)
        if: matrix.name != 'windows'
        run: |
          cmake -S . -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
            ${{ matrix.name == 'macos' && '-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"' || '' }}
```

Note: this may require additional testing since the C core DSP uses fixed-point arithmetic that should be architecture-neutral, but endianness and alignment assumptions should be validated on x86_64.

### WR-02: PluginEditor touch-create file left behind on async callback failure

**File:** `src/plugin/PluginEditor.cpp:462-463`
**Issue:** In `showPresetNamePrompt()`, a zero-byte file is touch-created at `~/Documents/preset.spu94` (line 463) to work around kdialog not pre-filling filenames for non-existent files. The cleanup in the async callback (line 479) only runs if the callback fires. If JUCE's `launchAsync` fails internally, or the file chooser is destroyed before the callback fires (e.g., the editor is closed while the dialog is open), the zero-byte file is never cleaned up and remains permanently in the user's Documents directory. The same pattern exists in `exportSingleSlot()` (lines 613-614, 627).

The `fileChooser` member is a `unique_ptr` that gets reassigned on each dialog open (lines 21, 62, 465, 616, 644). If a user rapidly clicks Save then Load (or vice versa), the previous `fileChooser` is destroyed, potentially orphaning the first dialog's callback and its touch-created file.

**Fix:** Use RAII or a destructor cleanup to remove any zero-byte touch-created file. A simpler approach: track the touch-created path as a member and clean it up in the destructor:

```cpp
// In destructor:
SPU94AudioProcessorEditor::~SPU94AudioProcessorEditor()
{
    stopTimer();
    // Clean up any orphaned touch-created preset files
    if (touchCreatedFile.existsAsFile() && touchCreatedFile.getSize() == 0)
        touchCreatedFile.deleteFile();
}
```

## Info

### IN-01: BETA-README Pro Tools section heading level inconsistency

**File:** `BETA-README.md:129`
**Issue:** The "Pro Tools" section uses `##` (h2), making it a peer of "Resetting Plugin Cache" and "Known Issues". Since it is a standalone topic about format support (not a sub-item of cache resetting), the heading level is technically correct. However, the content is a brief compatibility note that reads like it belongs under the DAW-specific guidance in the preceding section. Consider whether demoting it to `###` under "Resetting Plugin Cache" or keeping it at `##` with a more descriptive heading like "## Pro Tools Compatibility" would improve scanability.

**Fix:** If keeping as `##`, rename for clarity:

```markdown
## Pro Tools Compatibility
```

---

_Reviewed: 2026-05-13T19:42:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
