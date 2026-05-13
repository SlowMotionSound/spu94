# Phase 26: Packaging & Beta UAT - Context

**Gathered:** 2026-05-13
**Status:** Ready for planning

<domain>
## Phase Boundary

Build per-OS installer packages (macOS .pkg + .dmg, Windows Inno Setup, Linux tarball + install.sh) that place plugin binaries in host-standard scan paths. Write a beta README covering cache-reset and unsigned-binary workarounds. Select the DAW × OS UAT matrix, define the per-combination test checklist, and execute it — Anthony tests Linux + macOS personally, then ships to beta testers for macOS DAW coverage (Logic, Pro Tools via wrapper).

</domain>

<decisions>
## Implementation Decisions

### DAW Test Matrix
- **D-01:** 10 DAW-OS combinations across 6 DAWs:
  - Reaper: Linux, macOS, Windows
  - Ardour: Linux
  - Logic: macOS
  - Ableton Live: macOS, Windows
  - FL Studio: macOS, Windows
  - Bitwig: Linux (native CLAP validator)
- **D-02:** Pro Tools coverage via beta testers using a VST3 wrapper (e.g., Blue Cat PatchWork). Not a packaging target — AAX format is out of scope.

### macOS Packaging
- **D-03:** Ship BOTH a .pkg installer (wizard, auto-places in /Library/Audio/Plug-Ins/) AND a .dmg drag-install (fallback for testers who prefer manual). Two artifacts per macOS release.

### UAT Process
- **D-04:** UAT checklist per DAW-OS combination: plugin loads, processes audio without glitches, state survives save/close/reopen. Not a full feature sweep.
- **D-05:** Anthony tests personally on Linux and macOS. Beta testers cover macOS DAWs (Logic, Pro Tools via wrapper). Windows coverage comes from testers or remote access.
- **D-06:** UAT is manual — no automated DAW-level testing framework.

### Claude's Discretion
- Inno Setup configuration and install paths for Windows
- Linux install.sh script design and target paths (~/.vst3, ~/.lv2, ~/.clap)
- .dmg layout and background image (if any)
- .pkg component selection (which formats to include)
- Beta README structure and wording (polished tone per project convention)
- CI/CD artifact upload and GitHub Release automation
- UAT checklist exact wording and pass/fail criteria

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements
- `.planning/milestones/v1.7-REQUIREMENTS.md` §"Packaging & Distribution" (PLUG-43..48)
- `.planning/milestones/v1.7-REQUIREMENTS.md` §"Open Items / Known Unknowns" items #2, #5, #7
- `.planning/milestones/v1.7-ROADMAP.md` Phase 26 entry

### Architecture
- `.planning/research/ARCHITECTURE-v1.7.md` — build system, binary output paths

### CI Baseline
- `.github/workflows/plugins.yml` — existing 3-OS build matrix with artifact upload; Phase 26 extends with packaging steps and GitHub Release automation

### Prior Phase Context
- `.planning/phases/25-buses-validator-gates/25-CONTEXT.md` — validator gates (direct dependency; packaging must produce binaries that pass these gates)
- `.planning/phases/21-build-skeleton-ci-matrix/21-PLAN.md` — build skeleton and JUCE + clap-juce-extensions integration

### Deferred Items (from STATE.md)
- UI gate: Hide preset Save/Load buttons in plugin formats (flagged during Phase 22 UAT)
- Bug: Plugin GUI loses parameter state when window closed/reopened in LV2/Ardour (flagged during Phase 22 UAT)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `plugins.yml` CI workflow already builds all 11 binaries and uploads per-OS artifacts with 14-day retention — packaging extends this with installer creation and GitHub Release publishing.
- `CMakeLists.txt` JUCE target `spu94_plugin` produces all format binaries under `build/src/plugin/spu94_plugin_artefacts/Release/`.
- clap-juce-extensions integration (Phase 21) already handles CLAP binary generation.

### Established Patterns
- SHA-pinned GitHub Actions (Phase 21 convention) — any new CI steps must follow.
- Pluginval strictness-7 + auval + lv2lint + VST3 SDK validator gates (Phase 25) — packaging must not regress these.

### Integration Points
- CI artifact upload step → extend with installer packaging before upload.
- GitHub Release creation — new CI job triggered on tag push (v1.7-beta.N pattern).
- `build/src/plugin/spu94_plugin_artefacts/Release/` — source directory for all packaging.

</code_context>

<specifics>
## Specific Ideas

- Pro Tools support via VST3 wrapper is a tester-side concern, not a build target. Beta README should mention this as an option for PT users.
- Bitwig selected specifically for native CLAP validation on Linux.
- macOS .pkg is the primary install method; .dmg is the "I don't trust installers" fallback.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 26-packaging-beta-uat*
*Context gathered: 2026-05-13*
