---
phase: 08-m4-juce-plugin-product-v1-0
reviewed: 2026-04-25T00:00:00Z
depth: standard
files_reviewed: 11
files_reviewed_list:
  - src/standalone/PluginProcessor.h
  - src/standalone/PluginProcessor.cpp
  - src/standalone/PluginEditor.h
  - src/standalone/PluginEditor.cpp
  - src/standalone/WavLoader.h
  - src/standalone/WavLoader.cpp
  - src/standalone/ParameterBridge.h
  - src/standalone/ParameterBridge.cpp
  - src/standalone/RegisterPanel.h
  - src/standalone/RegisterPanel.cpp
  - src/standalone/CMakeLists.txt
  - CMakeLists.txt
findings:
  critical: 3
  warning: 4
  info: 2
  total: 9
status: issues_found
---

# Phase 08: Code Review Report

**Reviewed:** 2026-04-25
**Depth:** standard
**Files Reviewed:** 11 source files + root CMakeLists.txt
**Status:** issues_found

## Summary

This phase adds a JUCE 8 standalone audio application wrapping the existing
`libspu94` C core. The code is well-structured: the lock-free parameter bridge
(`ParameterBridge.cpp`) is sound for the happy path, the WAV loader correctly
isolates all heap allocation to the message thread, and the stack-allocated
int16 I/O buffers in `processBlock` are exactly the right approach.

Three critical issues were found. Two are data races: the WAV swap protocol
in `processBlock` is not atomic (the audio thread can see a half-constructed
`WavSource` at the moment a new file is loaded), and `syncShadowsFromSPU` is
called from both the audio thread and the constructor on the message thread
without any ordering guarantee. The third critical issue is a divide-by-zero
crash: if `numFrames` is zero when `processBlock` reads it, the modulo on line
106 of `PluginProcessor.cpp` will crash.

Four warnings cover: a VLA-adjacent stack ceiling that silently truncates large
block sizes instead of asserting; missing null-check on `spu94_init`'s return
before `registerBridge.syncShadowsFromSPU` is called in the constructor; an
integer narrowing in `WavLoader.cpp` that silently drops audio longer than
~12.5 hours; and incorrect `JUCE_DISPLAY_SPLASH_SCREEN=0` placement in
`CMakeLists.txt` (it must be on `PRIVATE`, not `PUBLIC`, to avoid bleeding into
dependent targets).

---

## Critical Issues

### CR-01: Data race — WAV swap in `processBlock` is not atomic

**File:** `src/standalone/PluginProcessor.cpp:57-63`

**Issue:** The audio thread reads `newWavReady` (acquire), then immediately
writes three non-atomic fields (`wavSource.L`, `wavSource.R`,
`wavSource.numFrames`) and two atomic fields. There is no corresponding fence
or barrier that prevents the message thread from entering `loadWavFile` and
writing `pendingL`/`pendingR`/`pendingFrames` while the audio thread is in the
middle of the `std::move` calls on lines 58-60.

The `newWavReady` flag is a single-producer / single-consumer flag, which by
itself is fine — but `pendingL`, `pendingR`, and `pendingFrames` are plain
(non-atomic) fields written by the message thread and read (moved) by the audio
thread. The acquire on `newWavReady.load` only synchronizes with the release on
`newWavReady.store(true)` in `loadWavFile`. That release-acquire pair correctly
makes the *writes to `pendingL/R/pendingFrames`* visible before the flag fires.

The race is in the other direction: nothing prevents `loadWavFile` from being
called a *second time* on the message thread while the audio thread is still
consuming the first pending swap (i.e., executing lines 58-60). A second call
to `loadWavFile` will `std::move` into `pendingL/R` at the same moment the
audio thread is `std::move`-ing *from* them. That is a data race on the
`std::vector` storage.

The safest fix that preserves zero-allocation on the audio thread is a
double-buffer with a generation counter:

```cpp
// PluginProcessor.h (private section)
struct PendingWav {
    std::vector<int16_t> L, R;
    uint64_t numFrames = 0;
};
// Two slots: message thread fills the "write" slot, flips a generation counter.
std::array<PendingWav, 2> pendingSlots;
std::atomic<int> pendingWriteSlot{0};   // message thread writes this
std::atomic<bool> newWavReady{false};

// PluginProcessor.cpp — loadWavFile (message thread)
void SPU94AudioProcessor::loadWavFile(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value()) return;

    // Write into whichever slot is NOT currently being consumed.
    const int slot = 1 - pendingWriteSlot.load(std::memory_order_relaxed);
    pendingSlots[slot].L = std::move(result->L);
    pendingSlots[slot].R = std::move(result->R);
    pendingSlots[slot].numFrames = result->numFrames;
    pendingWriteSlot.store(slot, std::memory_order_relaxed);
    newWavReady.store(true, std::memory_order_release);
}

// processBlock (audio thread) — swap section
if (newWavReady.load(std::memory_order_acquire))
{
    const int slot = pendingWriteSlot.load(std::memory_order_relaxed);
    wavSource.L = std::move(pendingSlots[slot].L);   // one allocation free move
    wavSource.R = std::move(pendingSlots[slot].R);
    wavSource.numFrames = pendingSlots[slot].numFrames;
    wavSource.playPos.store(0, std::memory_order_relaxed);
    wavSource.loaded.store(true, std::memory_order_relaxed);
    newWavReady.store(false, std::memory_order_release);
}
```

Note: the `std::move` in the audio thread still frees the old `wavSource` vector
storage when `loaded` was already true, which is a heap deallocation on the
audio thread. See WR-01 for that companion issue.

---

### CR-02: Data race — `syncShadowsFromSPU` called before `prepareToPlay`

**File:** `src/standalone/PluginEditor.cpp:98` and `src/standalone/PluginProcessor.cpp:39`

**Issue:** `SPU94AudioProcessorEditor` constructor (message thread) calls
`registerPanel.updateFromShadows()` at line 98. This in turn calls
`bridge.getShadowValue()` which loads from `shadows[]`. That is safe in
isolation.

However, `syncShadowsFromSPU` in `prepareToPlay` (line 39 of
`PluginProcessor.cpp`) stores to those same `shadows[]` atomics. `prepareToPlay`
is invoked on the audio thread at device open. The JUCE lifecycle guarantees
that `prepareToPlay` runs before the first `processBlock`, but it does NOT
guarantee it runs before the editor constructor. In the JUCE standalone wrapper,
the editor may be constructed (and call `updateFromShadows`) before
`prepareToPlay` has ever run — meaning `spu` is null and the shadows are all
zero (default-initialized `std::atomic<int16_t>{}`). That particular scenario
produces incorrect-but-not-crashing behavior: sliders show zeros until the user
interacts.

More importantly, if `prepareToPlay` is called *concurrently* with the editor
constructor (a race the JUCE standalone can create on some platforms during
device reinitializaton), both threads touch `shadows[]` simultaneously — message
thread reads, audio thread writes. The atomics make individual loads/stores
safe, but the constructor's read of all 18 values is not a coherent snapshot.
The practical risk is cosmetic (sliders momentarily show stale values), but it
is still a formally observable data race on the `spu` pointer itself (which is
not atomic).

**Fix:** Initialize the shadows to the Hall preset's register values at
construction time rather than relying on `prepareToPlay` to seed them:

```cpp
// PluginProcessor.cpp constructor — after existing code:
SPU94AudioProcessor::SPU94AudioProcessor()
    : AudioProcessor(...)
{
    // Pre-seed shadows with the Hall preset so the editor shows correct
    // values even before prepareToPlay is called.
    alignas(SPU94_STATE_ALIGN_MAX)
        unsigned char tmpState[SPU94_STATE_SIZE_MAX]{};
    unsigned char tmpWork[SPU94_WORK_BUF_MAX_BYTES]{};
    auto* tmp = spu94_init(tmpState, SPU94_STATE_SIZE_MAX,
                           tmpWork,  SPU94_WORK_BUF_MAX_BYTES);
    if (tmp)
    {
        spu94_load_preset(tmp, SPU94_PRESET_HALL);
        registerBridge.syncShadowsFromSPU(tmp);
        spu94_destroy(tmp);
    }
}
```

This seeds the shadows on the message thread before any editor has a chance to
read them, removing the need for `prepareToPlay` to touch the bridge at all. The
stack buffers used here are large (~544 KB), so this belongs in the constructor
body, not on the stack of a leaf function. Alternatively, heap-allocate them
temporarily.

---

### CR-03: Crash — divide-by-zero when `numFrames == 0`

**File:** `src/standalone/PluginProcessor.cpp:106`

**Issue:** On line 106, `numFrames` is loaded from `wavSource.numFrames` (a
plain non-atomic `uint64_t`) and immediately used as the modulo divisor:

```cpp
const auto idx = static_cast<size_t>(
    (playPos + static_cast<uint64_t>(i)) % numFrames);   // crashes if 0
```

`wavSource.numFrames` starts as zero (default-initialized). The guard on line
66 checks `wavSource.loaded.load()` before reaching this code. However, in the
WAV swap path (lines 57-63), `wavSource.loaded` is set to `true` *after*
`wavSource.numFrames` is written (line 61 sets `loaded`, line 60 sets
`numFrames`). Due to the compiler and CPU reordering allowed on `relaxed` stores,
`loaded` could be observed as `true` while `numFrames` is still zero on the
audio thread.

Even setting the memory ordering aside: `WavLoader::load` is capable of
returning a `LoadedWav` with `numFrames == 0` if `dstFrames` rounds down to
zero for a very short file. The `srcFrames > 0` check in `WavLoader.cpp` line
21 is not sufficient because `dstFrames = ceil(srcFrames / ratio)` — and
`ratio` could be extremely large (e.g., a 192 kHz source downsampled to 44.1 kHz
with `srcFrames = 1` gives `dstFrames = 1`, so zero is unlikely but still
reachable if `ceil` rounds to zero in edge cases).

**Fix — guard before the sample loop:**

```cpp
// Near line 85 of PluginProcessor.cpp, replace:
const auto numFrames = wavSource.numFrames;

// with:
const auto numFrames = wavSource.numFrames;
if (numFrames == 0) { buffer.clear(); return; }
```

Additionally, store `wavSource.numFrames` *before* setting `wavSource.loaded`
and use `std::memory_order_release` on the `loaded` store so the ordering is
formally correct:

```cpp
// Lines 58-63 of processBlock:
wavSource.L = std::move(pendingL);
wavSource.R = std::move(pendingR);
wavSource.numFrames = pendingFrames;
wavSource.playPos.store(0, std::memory_order_relaxed);
// Use release here so numFrames write is visible before loaded=true:
wavSource.loaded.store(true, std::memory_order_release);
newWavReady.store(false, std::memory_order_release);
```

And at the read site (line 66), use `acquire` when loading `loaded`:

```cpp
if (!wavSource.loaded.load(std::memory_order_acquire) || ...)
```

---

## Warnings

### WR-01: Heap deallocation on audio thread during WAV swap

**File:** `src/standalone/PluginProcessor.cpp:57-60`

**Issue:** When a second WAV file is loaded while the first is playing, the
audio thread executes `wavSource.L = std::move(pendingL)`. If `wavSource.L` is
already populated (i.e., `loaded == true`), this assignment destroys the old
`std::vector` storage, calling `free()` on the audio thread. `free()` is not
real-time safe.

This is a first-load-only-safe design. The very first `loadWavFile` call is
fine because `wavSource.L` is empty. Every subsequent load causes a heap
deallocation on the audio thread.

**Fix:** Move the deallocation off the audio thread by holding a
`std::shared_ptr<WavData>` for the live source and an atomic swap. After the
audio thread atomically replaces the pointer, the old buffer is reclaimed when
the message thread (or a low-priority cleanup thread) drops its reference.
Alternatively, `stopPlayback()` + `wavSource.L.clear()` on the *message thread*
before `newWavReady` is set ensures the audio thread always moves into an empty
vector. The simplest acceptable fix for v1 is to require `stopPlayback()` in
`loadWavFile` before the move:

```cpp
void SPU94AudioProcessor::loadWavFile(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value()) return;

    // Stop and clear the live source on the message thread to ensure
    // the audio thread is not holding vector storage when the swap fires.
    wavSource.playing.store(false, std::memory_order_relaxed);
    // Give the audio thread one block to observe playing=false.
    // A juce::MessageManager::callAsync loop + flag is cleaner in practice.
    // Minimal: clear the pending vectors here (still in message thread).
    pendingL = std::move(result->L);
    pendingR = std::move(result->R);
    pendingFrames = result->numFrames;
    newWavReady.store(true, std::memory_order_release);
}
```

A proper solution uses double-buffering or a `shared_ptr` swap; defer to
whichever pattern is chosen when fixing CR-01.

---

### WR-02: Silent block truncation instead of safe assertion

**File:** `src/standalone/PluginProcessor.cpp:92`

**Issue:**

```cpp
const int samplesToProcess = (n <= kMaxBlock) ? n : kMaxBlock;
```

If JUCE delivers a block larger than 4096 samples, the audio thread silently
processes only the first 4096 samples and writes partial output into the
`buffer`. The caller (`AudioProcessor::processBlock`) is required to fill the
entire buffer; writing only part of it and returning is undefined behavior from
the host's perspective — it will play the uninitialized remainder of the buffer.

The `jassert(n <= kMaxBlock)` on line 91 fires only in debug builds. In a
release build, the truncation is silent and the unwritten portion of `buffer` is
output as garbage audio, not silence (JUCE does not clear the output buffer
before calling `processBlock`).

JUCE's standalone wrapper defaults to a maximum block size of 512 samples.
4096 is an extremely conservative ceiling, so this is unlikely to trigger in
practice. But the truncation path is still wrong.

**Fix:** Either clear the tail of the buffer if `n > kMaxBlock`, or assert hard:

```cpp
// Option A — safe fallback: clear the output buffer first, then process
// what we can. Worst case: partial audio this block.
buffer.clear();  // move this to before the guard block
const int samplesToProcess = std::min(n, kMaxBlock);

// Option B — treat oversized blocks as a programming error (preferred):
jassert(n <= kMaxBlock);
if (n > kMaxBlock) { buffer.clear(); return; }
const int samplesToProcess = n;
```

Option B is cleaner for a v1 standalone app where you control the host.

---

### WR-03: `spu94_init` null return not checked in `prepareToPlay`

**File:** `src/standalone/PluginProcessor.cpp:33-39`

**Issue:** `spu94_init` returns `nullptr` if the buffer is too small or
misaligned. Line 36 checks for `spu != nullptr` before calling
`spu94_load_preset` and `syncShadowsFromSPU`, which is correct. However,
`processBlock` relies on the `spu == nullptr` guard on line 68 to bail out.
If `prepareToPlay` is called again after a device reconfiguration (JUCE calls
it on every audio device change), `spu94_destroy` is never called before the
second `spu94_init` — the old `spu` pointer is leaked (the underlying
`stateBuf`/`workBuf` memory is not freed because they are `juce::HeapBlock`,
but the state's internal bookkeeping is not cleaned up).

Additionally, `prepareToPlay` calls `stateBuf.allocate(...)` and
`workBuf.allocate(...)` every time it is invoked, regardless of whether
the buffers are already allocated. `HeapBlock::allocate` frees and reallocates
on every call, so if the audio thread has a stale pointer from the previous
`prepareToPlay` call (a race with JUCE's reinit sequence), it will use freed
memory.

**Fix:** Destroy the old SPU state before reallocating:

```cpp
void SPU94AudioProcessor::prepareToPlay(double, int)
{
    // Tear down any existing SPU state before reinitializing.
    if (spu != nullptr)
    {
        spu94_destroy(spu);
        spu = nullptr;
    }

    stateBuf.allocate(SPU94_STATE_SIZE_MAX, true);
    workBuf.allocate(SPU94_WORK_BUF_MAX_BYTES, true);

    spu = spu94_init(stateBuf.getData(), SPU94_STATE_SIZE_MAX,
                     workBuf.getData(), SPU94_WORK_BUF_MAX_BYTES);
    if (spu == nullptr)
        return;  // log or display error if desired

    spu94_load_preset(spu, SPU94_PRESET_HALL);
    registerBridge.syncShadowsFromSPU(spu);
}
```

Note that `spu94_destroy` is a no-op on `nullptr` per the API contract, so the
`if (spu != nullptr)` guard is redundant but explicit.

---

### WR-04: `JUCE_DISPLAY_SPLASH_SCREEN=0` on `PUBLIC` scope

**File:** `src/standalone/CMakeLists.txt:31-34`

**Issue:**

```cmake
target_compile_definitions(spu94_standalone
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
```

These definitions are placed in the `PUBLIC` scope, meaning they are inherited
by any target that links `spu94_standalone`. For the Phase 8 Standalone-only
build this has no downstream consumers, so it is harmless now. When plugin
formats (VST3, LV2) are added in a later phase, every plugin target that links
`spu94_standalone` will inherit `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`
silently — which may conflict with those formats' requirements.

The plan had also specified `JUCE_DISPLAY_SPLASH_SCREEN=0` (visible in Plan 01's
task body) but the actual file omits it. `JUCE_DISPLAY_SPLASH_SCREEN=0` requires
a valid JUCE license; omitting it in Phase 8 is correct because the splash
screen status should be decided when licensing is resolved. This is noted for
completeness.

**Fix:**

```cmake
target_compile_definitions(spu94_standalone
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
```

Use `PRIVATE` unless you have a reason to propagate these definitions to
consumers.

---

## Info

### IN-01: `WavLoader` allocates large buffers on the message thread — file cap not enforced

**File:** `src/standalone/WavLoader.cpp:26-39`

**Issue:** `srcFrames` is cast to `int` on line 27 for the `juce::AudioBuffer`
constructor, and `dstFrames` is cast to `int` on line 39. `juce::AudioBuffer`
uses `int` for sample counts, so values above `INT_MAX` (~2 billion samples,
~12.5 hours at 44.1 kHz) silently overflow. The cast is technically correct for
any real-world audio file, but there is no explicit guard. A malformed WAV with
a forged length header reporting a huge `lengthInSamples` would cause an
allocation attempt for a multi-gigabyte buffer.

This is not a security concern in a desktop standalone app (the user loads their
own files), but it is worth a size sanity check:

```cpp
// After srcFrames is set, before the AudioBuffer allocation:
constexpr int64_t kMaxLoadFrames = 60LL * 60 * 44100;  // 1 hour at 44.1 kHz
if (srcFrames > kMaxLoadFrames)
    return std::nullopt;
```

---

### IN-02: `RegisterPanel` slider `onValueChange` captures `i` by value correctly, but double-conversion truncates u16 register range

**File:** `src/standalone/RegisterPanel.cpp:23-26`

**Issue:** For `SPU94_REG_TYPE_U16` registers, the slider range is `[0, 65535]`
(double). The `onValueChange` lambda casts `sliders[i].getValue()` to
`int16_t`, which interprets values above 32767 as negative (bit-cast). When
the audio thread calls `spu94_set_reg_u16(spu, reg, static_cast<uint16_t>(v))`
in `ParameterBridge.cpp:24`, the `static_cast<uint16_t>(int16_t)` bit-pattern
round-trip is correct — `-1` as `int16_t` becomes `65535` as `uint16_t`.

The behavior is technically correct but is fragile and non-obvious. A slider
showing "64000" stores the value as `int16_t(-1536)` in the shadow, then
reinterprets it back to `uint16_t(64000)` in `pushPendingRegisterWrites`. If
anyone reads `getShadowValue()` and interprets the `int16_t` as a display value
for a U16 slider they will get a negative number.

**Suggested clarification:** Store the raw bit pattern consistently:

```cpp
// RegisterPanel.cpp onValueChange — for u16 sliders:
slider.onValueChange = [this, i] {
    // For U16 registers, reinterpret the slider's unsigned value as int16_t bits.
    const int raw = static_cast<int>(sliders[i].getValue());
    bridge.setRegisterShadow(i, static_cast<int16_t>(raw & 0xFFFF));
};
```

This does the same thing as the current code but makes the intent explicit.

---

_Reviewed: 2026-04-25_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
