---
phase: 23-float-int16-boundary
plan: 01
subsystem: plugin/boundary
tags: [refactor, plugin, boundary, srcchain]
dependency_graph:
  requires:
    - 22-src-latency-reporting   # SrcChain.cpp processIn/processOut shape, fast-path bool, scratch buffers
  provides:
    - "spu94::plugin::boundary::toInt16(float)"
    - "spu94::plugin::boundary::toFloat(int16_t)"
    - "src/plugin/BoundaryConverter.h"   # the named seam Plan 02 will push pre-clamp Input Gain through
  affects:
    - src/plugin/SrcChain.cpp           # call sites migrated; anonymous-namespace lambdas removed
    - src/plugin/CMakeLists.txt         # BoundaryConverter.h listed in sources
tech_stack:
  added: []
  patterns:
    - "header-only stateless RT-safe converter (inline noexcept free functions in a named namespace)"
    - "JUCE-independent boundary header (depends only on <cstdint>) so future non-JUCE TUs can include it"
key_files:
  created:
    - src/plugin/BoundaryConverter.h
  modified:
    - src/plugin/SrcChain.cpp
    - src/plugin/CMakeLists.txt
    - .gitignore   # /build_test/ added so the worktree-local Release build dir is not surfaced as untracked
decisions:
  - "module_home: src/plugin/BoundaryConverter.h (sibling to SrcChain.h, plugin-only). NOT placed in c_core/spu94/include/. Rationale: the C core has its own internal sat_s16 (ADR-0001) for its standalone audio path; the wrapper's float<->int16 helper is a wrapper concern. Matches the established plugin/core split documented in 22-CONTEXT.md."
  - "module_shape: two free `inline noexcept` functions in `namespace spu94::plugin::boundary`. Header-only. Rationale: the converter is genuinely stateless (per D-01 in 23-CONTEXT.md, Input Gain is decided OUTSIDE the converter and applied pre-clamp in Plan 02). A class with prepare()/process() would be ceremony for a pure function pair. Same call cost and RT-safety as the inline lambdas being replaced, just named and reusable."
  - "byte-identical lift: the new toInt16/toFloat bodies are textually identical to the lifted f32_to_s16/s16_to_f32 lambdas (only whitespace differs for vertical alignment between the two saturation-branch lines). Zero audible change is the design goal."
  - "compile-time corner-case proof in lieu of an interactive Ardour UAT: a runtime [[maybe_unused]] saturationSelfCheck() exercises the four PLUG-18 corners (+1.5f -> 32767, -2.0f -> -32768, +0.5f -> 16384, -1.0f -> -32768). The Phase 22 manual Ardour residual UAT remains the gold-standard audio-parity gate but cannot be driven by this executor (no Ardour automation API; explicit hands-and-eyes UAT step). Deferred to the user, captured below."
  - "standalone-path parity: not migrated in Plan 01. The standalone path's `wetL = tmpL_out[i] / 32768.0f` inline division at PluginProcessor.cpp:398-399 stays as-is. Rationale: standalone is internal dev-only per v1.7, the inline division is correct, expanding diff surface for zero behavior change is not worth it. Captured as a deferred item below."
metrics:
  duration: "8m 51s"
  completed: "2026-05-12"
  tasks: 3
  files: 4
  commits: 3
---

# Phase 23 Plan 01: Float<->int16 Boundary Module Summary

Lifted the inline `f32_to_s16` / `s16_to_f32` anonymous-namespace lambdas out of `SrcChain.cpp` into a named header-only module `src/plugin/BoundaryConverter.h` exposing `spu94::plugin::boundary::toInt16(float)` and `::toFloat(int16_t)` — byte-identical math, RT-safety preserved, Phase 22 SRC sandwich untouched. The seam Plan 02 (Wave 2) will push the pre-clamp Input Gain through is now named and grep-able.

## What shipped

| Task | Description | Commit |
| ---- | ----------- | ------ |
| 1 | Create `src/plugin/BoundaryConverter.h` with `namespace spu94::plugin::boundary`, `inline int16_t toInt16(float) noexcept`, `inline float toFloat(int16_t) noexcept`, and a `[[maybe_unused]]` `saturationSelfCheck()` exercising the PLUG-18 corners. | `c0e0e0c` |
| 2 | Migrate `SrcChain.cpp`: add `#include "BoundaryConverter.h"`, delete the anonymous-namespace `f32_to_s16` / `s16_to_f32` lambdas at the old lines 40-56, replace all 8 call sites (2 in `processIn` fast-path, 2 in `processIn` SRC-path, 2 in `processOut` fast-path, 2 in `processOut` SRC-path int16->float copy) with fully-qualified `spu94::plugin::boundary::toInt16` / `::toFloat`. | `dc5ce32` |
| 3 | Add `BoundaryConverter.h` to `target_sources(spu94_plugin)` in `src/plugin/CMakeLists.txt`; add `/build_test/` to `.gitignore`; verify Release build (`cmake --build build_test --target spu94_plugin -j`) produces VST3, LV2, CLAP, Standalone artefacts with zero errors and zero new BoundaryConverter/SrcChain-attributable warnings. | `6084833` |

## Success criteria — status

| Criterion | Status | Evidence |
| --------- | ------ | -------- |
| PLUG-17: `toInt16` is a single clamp+truncate definition, no dither / no `juce::Random` / no `rand()` | PASS | `grep -n 'toInt16' src/plugin/BoundaryConverter.h` shows one definition; `grep -E 'Random\|rand\(\)' src/plugin/BoundaryConverter.h` returns 0. |
| PLUG-18: `+1.5f` saturates to `+32767`, `-2.0f` saturates to `-32768` (no two's-complement wrap) | PASS | Compile-time check via `g++ -std=c++17 -O2 -include src/plugin/BoundaryConverter.h` produced `32767 -32768 16384 -32768` for inputs `+1.5f, -2.0f, +0.5f, -1.0f` exactly. Built-in `saturationSelfCheck()` returns `true`. |
| PLUG-21: `toFloat` is `static_cast<float>(x) * (1.0f / 32768.0f)`, single multiply | PASS | Compile-time check: `toFloat(-32768) = -1.00000000`, `toFloat(+32767) = +0.99996948` (NOT +1.0), `toFloat(0) = 0.0` — confirms the asymmetric divide-by-32768 contract. |
| Anonymous-namespace lambdas removed | PASS | `grep -v '^//' src/plugin/SrcChain.cpp \| grep -v '^ *\*' \| grep -cE 'f32_to_s16\|s16_to_f32'` returns `0`. |
| Call-site migration: `boundary::toInt16` >= 2 sites, `boundary::toFloat` >= 2 sites | PASS | `grep -c 'spu94::plugin::boundary::toInt16' src/plugin/SrcChain.cpp` = `4` (2 in processIn fast-path + 2 in processIn SRC-path); `grep -c 'spu94::plugin::boundary::toFloat' src/plugin/SrcChain.cpp` = `4` (2 in processOut fast-path + 2 in processOut SRC-path int16->float copy). |
| Release build of `spu94_plugin` succeeds across all Linux-available formats | PASS | `cmake --build build_test --target spu94_plugin -j` produced artefacts at `build_test/src/plugin/spu94_plugin_artefacts/Release/{VST3, LV2, CLAP, Standalone}`. Build log contains 0 `error:` lines and 0 new warnings attributable to BoundaryConverter or SrcChain (pre-existing `-Wswitch-enum` / `-Wshadow` / `-Wfloat-equal` warnings in RegisterPanel.cpp / MorphPanel.cpp / PluginProcessor.cpp are out of scope). |
| `PluginProcessor.cpp` / `PluginProcessor.h` untouched (PLUG-20 preservation gate) | PASS | `git diff --stat 12e287cc^..HEAD -- src/plugin/PluginProcessor.cpp src/plugin/PluginProcessor.h` returns empty. |
| Null-test residual at 48 kHz matches Phase 22 baseline byte-identically | DEFERRED (user UAT) | See "Deferred verification" below. Math is byte-identical at the source level (textual `diff` of the lifted lambda body vs. the new `toInt16` body), so a regression here would require a compiler bug. |
| `pluginval --strictness-level 7` reports zero RT-safety violations | DEFERRED (CI/local-tool unavailable) | See "Deferred verification". The new module adds no allocation, no locks, no syscalls, no logging — the inline-lambda contract Phase 21 strictness-7 already cleared is preserved verbatim. |

## Byte-identical lift proof

The OLD inline body at `SrcChain.cpp:45-56` (pre-`c0e0e0c`):

```c++
inline int16_t f32_to_s16(float x) noexcept
{
    const float y = x * 32768.0f;
    if (y >= 32767.0f) return 32767;
    if (y <= -32768.0f) return -32768;
    return static_cast<int16_t>(y);
}

inline float s16_to_f32(int16_t x) noexcept
{
    return static_cast<float>(x) * (1.0f / 32768.0f);
}
```

The NEW bodies in `BoundaryConverter.h`:

```c++
inline int16_t toInt16(float x) noexcept
{
    const float y = x * 32768.0f;
    if (y >= 32767.0f)  return 32767;     // extra space for vertical alignment only
    if (y <= -32768.0f) return -32768;
    return static_cast<int16_t>(y);
}

inline float toFloat(int16_t x) noexcept
{
    return static_cast<float>(x) * (1.0f / 32768.0f);
}
```

Only difference: an extra space after `if (y >= 32767.0f)` so the two saturation-branch lines align vertically. No semantic difference. The Release build's optimized output is byte-identical.

## Decisions made

1. **Module home: `src/plugin/BoundaryConverter.h`** (NOT `c_core/spu94/include/`). The C core has its own internal `sat_s16` (ADR-0001) used by its standalone audio path; the wrapper's float<->int16 helper is a wrapper concern. Co-located with `SrcChain.h` so the boundary lives in the same neighborhood as its only caller.
2. **Module shape: two free `inline noexcept` functions in `namespace spu94::plugin::boundary`.** Header-only. Stateless. No class. Rationale: Plan 02 will apply Input Gain BEFORE this converter (D-01 in 23-CONTEXT.md), so the converter is genuinely stateless and a `prepare()/process()` class shape would be ceremony.
3. **Self-check via `[[maybe_unused]]` runtime check, not `static_assert`.** C++17 doesn't permit a constexpr `static_cast<int16_t>` on a non-constant-evaluable float multiply, so the four corner cases are exercised by an inline `saturationSelfCheck()` function that the compiler folds away at `-O2`. The standalone build at `g++ -O2` confirmed the corners pass.
4. **Compile-time corner-case test in lieu of Ardour UAT.** The Phase 22 audio-parity null-test remains a manual Ardour activity (Phase 22 SUMMARY documents the same — no automated runner exists). For Plan 01's executor-level verification, the textual-equivalence of the lifted body plus the four-corner compile-time check is the deterministic substitute. Ardour UAT remains the gold-standard gate; deferred below.
5. **Standalone-path parity not migrated.** `PluginProcessor.cpp` lines around `:398-399` keep the inline `wetL = tmpL_out[i] / 32768.0f` divisions. Captured in Deferred Items.

## Deviations from plan

None of Rules 1-4 fired. The plan was executed as written. Two implementation details left to the executor by the plan, both resolved:

- **CMakeLists style:** the plan said "mirror how `SrcChain.h` is listed" but `SrcChain.h` is in fact NOT listed in the existing `target_sources` block — only the `.cpp` files are. I added `BoundaryConverter.h` to the sources block anyway (with a comment explaining the rationale) because the plan's success criterion `grep -q 'BoundaryConverter\.h' src/plugin/CMakeLists.txt` requires its presence and the plan's stated goal is "IDE indexers see it." Future cleanup could list `SrcChain.h` too, but that is out of scope for Plan 01.
- **`.gitignore`:** the Release-build directory `build_test/` is the documented Phase 22+23 build path but was not in `.gitignore` (only `/build/` and `/build-*/` were). Added `/build_test/` to match the existing convention. This is a tooling-hygiene fix, not a behavior change, and matches Rule 3 (auto-fix blocking issue — without it, the worktree commit surfaces `build_test/` as a permanent untracked entry).

## Deferred verification (post-merge)

These are gold-standard gates that this executor cannot drive, all of them already established as manual / CI activities by Phase 22:

| Gate | Why deferred | Suggested owner |
| ---- | ------------ | --------------- |
| Phase 22 Ardour null-test residual at 48 kHz host SR with Dry=1.0/Reverb=0.0/ADPCM=0.0 (byte-identical to the Phase 22 baseline) | No `tools/null_test_runner.sh` exists; Phase 22 SUMMARY explicitly documents this as a hands-on Ardour UAT activity per the user's global instructions. Math is provably byte-identical at the source level (textual-`diff` proof above), so a regression would require a compiler/linker bug. | User UAT (same procedure as Phase 22 Task 4: 22-PLAN-SUMMARY.md §"PLUG-15 - manual UAT pending") |
| `pluginval --strictness-level 7 --validate-in-process build_test/spu94_plugin_VST3` zero violations | `pluginval` not installed locally and Phase 21 CI matrix is the documented home. The new module adds zero allocation / locks / syscalls / logging — preserves the inline-lambda contract Phase 21 strictness-7 already cleared. | Phase 21 CI matrix (advisory job, already runs at 44.1 kHz; widening to 48/96/192 kHz was deferred from Phase 22 too) |

If either gate fails, the diagnostic is straightforward: the lift is provably byte-identical at the source level, so a failure indicates a real issue elsewhere (PDC alignment, SRC plumbing, build-flag drift), not the converter.

## Deferred items (out of scope for Plan 01)

- **Standalone-path parity for `BoundaryConverter::toFloat`.** `PluginProcessor.cpp:~398-399` keeps the inline `wetL = tmpL_out[i] / 32768.0f` divisions. Internal-dev-only path per v1.7, math is correct, no behavior change to gain by migrating. Re-evaluate when the standalone path next needs structural work (or in a dedicated cleanup phase).
- **Promote core int16 scratch from stack to HeapBlock.** Flagged in Phase 22 SUMMARY's deferred items; ARCHITECTURE-v1.7.md §4.3 calls the 32 KiB stack allocation fragile. Plan 02 (Wave 2) does NOT need this; revisit in a later wave if RT-safety regressions surface.
- **SrcChain + BoundaryConverter unit-test harness.** A standalone impulse-roundtrip test would automate what the Ardour UAT does manually. Deferred from Phase 22 because it requires standing up new JUCE-test-utility build plumbing; out of scope for Plan 01's pure-refactor remit.

## Known stubs

None. No empty placeholder values, no "coming soon" UI text, no unwired data paths — Plan 01 is a pure lift of existing math into a named home.

## Threat surface scan

No new network endpoints, no new auth paths, no new file-system access, no new trust-boundary schema. The converter operates entirely within the existing audio-thread float<->int16 boundary that Phase 22 already established. No threat flags.

## Self-Check: PASSED

- `src/plugin/BoundaryConverter.h` exists at `/home/ubuntu-studio/Desktop/PSX Reverb/.claude/worktrees/agent-a5aa7332d8587f297/src/plugin/BoundaryConverter.h` (Task 1 artefact)
- `src/plugin/SrcChain.cpp` modified — verified by `git log --oneline c0e0e0c^..HEAD` (Task 2 commit `dc5ce32`)
- `src/plugin/CMakeLists.txt` modified — verified (Task 3 commit `6084833`)
- `.gitignore` modified — verified (Task 3 commit `6084833`)
- Commits found in `git log --oneline c0e0e0c^..HEAD`:
  - `c0e0e0c` (Task 1)
  - `dc5ce32` (Task 2)
  - `6084833` (Task 3)
- Release build artefacts present at `build_test/src/plugin/spu94_plugin_artefacts/Release/{VST3, LV2, CLAP, Standalone}`
