---
phase: 08-m4-juce-plugin-product-v1-0
fixed_at: 2026-04-26T02:51:03Z
review_path: .planning/phases/08-m4-juce-plugin-product-v1-0/08-REVIEW.md
iteration: 1
findings_in_scope: 7
fixed: 7
skipped: 0
status: all_fixed
---

# Phase 08: Code Review Fix Report

**Fixed at:** 2026-04-26T02:51:03Z
**Source review:** .planning/phases/08-m4-juce-plugin-product-v1-0/08-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 7
- Fixed: 7
- Skipped: 0

## Fixed Issues

### CR-01 + WR-01: Data race in WAV swap + heap deallocation on audio thread

**Files modified:** `src/standalone/PluginProcessor.h`, `src/standalone/PluginProcessor.cpp`
**Commit:** 5f6eb43
**Applied fix:** Replaced single pending buffer with double-buffered `PendingWav` slots. Message thread writes to the opposite slot from the one last consumed. Audio thread uses `std::swap` (not `std::move`) to move old wavSource vectors into the consumed slot, deferring their heap deallocation to the next message-thread load. Added `<array>` include to header. The `loaded` store now uses `memory_order_release` (also supports CR-03 ordering fix).

### CR-02: Data race -- syncShadowsFromSPU called before prepareToPlay

**Files modified:** `src/standalone/PluginProcessor.cpp`
**Commit:** 5cd749e
**Applied fix:** Constructor now creates a temporary SPU instance (heap-allocated buffers), loads the Hall preset, and calls `syncShadowsFromSPU` to pre-seed all 18 register shadows. The editor always sees valid slider values regardless of `prepareToPlay` timing. Added `<cstring>` and `<memory>` includes.

### CR-03: Crash -- divide-by-zero when numFrames == 0

**Files modified:** `src/standalone/PluginProcessor.cpp`
**Commit:** 6c6c7d3
**Applied fix:** Added `if (numFrames == 0) { buffer.clear(); return; }` guard after loading `numFrames` and before the sample loop. Changed `wavSource.loaded` load from `relaxed` to `acquire` ordering so it pairs with the `release` store in the swap section, guaranteeing `numFrames` is visible when `loaded` reads as true.

### WR-02: Silent block truncation instead of safe assertion

**Files modified:** `src/standalone/PluginProcessor.cpp`
**Commit:** 14466ca
**Applied fix:** Changed oversized-block path from silent truncation to `buffer.clear(); return;` -- outputs silence instead of garbage in the unwritten tail. Simplified `samplesToProcess` to just `n` since the guard guarantees `n <= kMaxBlock`.

### WR-03: spu94_init null return not checked in prepareToPlay

**Files modified:** `src/standalone/PluginProcessor.cpp`
**Commit:** a1dafe4
**Applied fix:** Added `spu94_destroy(spu)` teardown at the top of `prepareToPlay` before reallocating buffers, preventing internal bookkeeping leak on JUCE audio device reinit. Changed the null check to early-return pattern (`if (spu == nullptr) return;`) so preset load and shadow sync only run on success.

### WR-04: JUCE_DISPLAY_SPLASH_SCREEN=0 on PUBLIC scope

**Files modified:** `src/standalone/CMakeLists.txt`
**Commit:** 0cb4d07
**Applied fix:** Changed `target_compile_definitions` scope from `PUBLIC` to `PRIVATE` so `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0` do not bleed into future plugin format targets.

## Skipped Issues

None -- all findings were fixed.

---

_Fixed: 2026-04-26T02:51:03Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
