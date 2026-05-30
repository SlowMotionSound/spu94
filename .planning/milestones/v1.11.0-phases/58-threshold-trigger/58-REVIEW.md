---
phase: 58-threshold-trigger
reviewed: 2026-05-28T21:50:00Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - src/plugin/PluginProcessor.h
  - src/plugin/PluginProcessor.cpp
  - src/plugin/PluginEditor.h
  - src/plugin/PluginEditor.cpp
findings:
  critical: 1
  warning: 3
  info: 1
  total: 5
status: issues_found
---

# Phase 58: Code Review Report

**Reviewed:** 2026-05-28T21:50:00Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

Phase 58 adds threshold-triggered recording: a new REC_ARMED state in the recording state machine, a threshold knob (dB-to-linear conversion), and tri-state record button visuals. The core implementation is clean -- the trigger-from-exact-sample approach correctly preserves attack transients, and the armed/disarm lifecycle is well-structured. However, there is one critical issue where the armed-to-recording transition on the audio thread races with the staging buffer being freed by a concurrent disarm on the message thread, plus several robustness warnings around the auto-stop path and threshold-knob-while-armed behavior.

## Critical Issues

### CR-01: Race between audio-thread ARMED->RECORDING transition and message-thread disarm freeing staging buffer

**File:** `src/plugin/PluginProcessor.cpp:785-795`
**Issue:** When the audio thread is in the REC_ARMED branch processing samples (lines 760-801), it reads `recordingThreshold`, scans the buffer for threshold crossings, and upon finding one transitions to REC_RECORDING (line 785) and immediately writes into `recordStagingBuffer` (line 794). Meanwhile, `stopRecording()` on the message thread (line 2061-2079) handles REC_ARMED by calling `recordStagingBuffer.clear()` and `shrink_to_fit()`, freeing the underlying memory.

The race scenario: audio thread enters the REC_ARMED branch at line 760, reads threshold and begins scanning samples. Concurrently, the user clicks the record button (message thread), which calls `stopRecording()`. The message thread sees `state == REC_ARMED` at line 2061, proceeds to `recordStagingBuffer.clear()` (line 2065) and `shrink_to_fit()` (line 2066), deallocating the buffer. The audio thread then finds a threshold crossing, stores REC_RECORDING (line 785), and writes to the now-freed `recordStagingBuffer[recordStagingCount]` at line 794 -- use-after-free.

The existing REC_RECORDING branch has the same race shape (audio thread writing to staging buffer while message thread could free it), but REC_ARMED makes it worse because the transition and the write happen in the same audio block, giving the message thread a wider window.

**Fix:** The message thread's disarm path should transition the state to REC_IDLE FIRST (using a release store), then let the audio thread see the state change and stop writing. The buffer deallocation should be deferred to after the state is confirmed idle. One robust pattern:

```cpp
void SPU94AudioProcessor::stopRecording()
{
    const int state = recordingState.load(std::memory_order_acquire);

    if (state == REC_ARMED)
    {
        // Transition to IDLE first -- audio thread will see this and skip the ARMED branch.
        recordingState.store(REC_IDLE, std::memory_order_release);

        // Re-mute standalone input
        if (wrapperType == wrapperType_Standalone)
        {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->getMuteInputValue().setValue(true);
        }

        // Now safe to free -- audio thread is no longer in the ARMED branch
        // (it reads recordingState with acquire at the top of the block).
        recordStagingBuffer.clear();
        recordStagingBuffer.shrink_to_fit();
        recordStagingCount = 0;
        recordStagingCapacity = 0;
        recordBytesUsed.store(0, std::memory_order_relaxed);
        return;
    }
    // ... rest unchanged
}
```

Note: Even with this fix, the window is narrow but real -- if the audio thread has ALREADY entered the ARMED branch and read the old state before the store lands, it could still trigger. A fully correct fix would also check `recordStagingCapacity` (which is only zeroed after the store) in the audio-thread trigger path, providing a second guard. The existing `recordStagingCount < recordStagingCapacity` check at line 793 partially mitigates this, but `recordStagingCapacity` is a non-atomic `uint64_t` being written from the message thread while potentially read from the audio thread -- that is undefined behavior under the C++ memory model.

## Warnings

### WR-01: No auto-stop check in the ARMED->RECORDING transition block

**File:** `src/plugin/PluginProcessor.cpp:787-800`
**Issue:** When the threshold triggers mid-block, the code captures samples from `triggerSample` through the end of the block (lines 787-796) and updates `recordBytesUsed` (lines 798-800). However, it does not check whether `recordStagingCount >= recordStagingCapacity` after this capture loop to trigger auto-stop. If the staging buffer was nearly full when the threshold triggered, the trigger block could fill it to capacity without transitioning to REC_STOPPED.

In practice, the next processBlock call will enter the REC_RECORDING branch and auto-stop at line 754. The impact is small -- at most one extra block (~4096 samples) could be silently dropped (the bounds check at line 793 prevents writes past capacity). But the `recordBytesUsed` counter at line 799 could report a value higher than what was actually written, since `recordStagingCount` increments unconditionally at line 795 even when the bounds check at line 793 skips the write.

**Fix:** Add the same auto-stop check after the trigger capture loop:
```cpp
if (recordStagingCount >= recordStagingCapacity)
{
    recordingState.store(REC_STOPPED, std::memory_order_release);
    recordingJustStopped.store(true, std::memory_order_release);
}
```

### WR-02: recordStagingCount increments past capacity, inflating byte estimates

**File:** `src/plugin/PluginProcessor.cpp:743,795`
**Issue:** In both the REC_RECORDING branch (line 741-743) and the ARMED->RECORDING trigger capture (line 793-795), the pattern is:

```cpp
if (recordStagingCount < recordStagingCapacity)
    recordStagingBuffer[recordStagingCount] = static_cast<int16_t>(scaled);
++recordStagingCount;  // increments regardless
```

When `recordStagingCount >= recordStagingCapacity`, the write is correctly skipped but the counter still increments. This means `recordBytesUsed` (computed from `recordStagingCount` at lines 750 and 799) will overcount, and `encodeRecordedSample()` at line 2138 uses `recordStagingCount` as the nominal sample count. The clamp at line 2138 (`count < recordStagingCapacity ? count : recordStagingCapacity`) catches this for the encoder, but the GUI's live byte counter and time display will show values beyond 512KB during the brief window between overflow and auto-stop.

**Fix:** Move the increment inside the bounds check:
```cpp
if (recordStagingCount < recordStagingCapacity)
{
    recordStagingBuffer[recordStagingCount] = static_cast<int16_t>(scaled);
    ++recordStagingCount;
}
```

### WR-03: Threshold knob disabled during ARMED state prevents user adjustment before trigger

**File:** `src/plugin/PluginEditor.cpp:1319`
**Issue:** When the recording state is REC_ARMED (line 1311-1320), the threshold knob is disabled (`thresholdKnob.setEnabled(false)` at line 1319). This prevents the user from adjusting the threshold sensitivity while waiting for audio to cross it. This is a usability concern -- if the user arms, then realizes the threshold is too sensitive or not sensitive enough, they must disarm, adjust, and re-arm. Allowing threshold adjustment while armed would be more natural.

The underlying processor atomic (`recordingThreshold`) is safely readable from the audio thread regardless of GUI state, so there is no technical barrier.

**Fix:** Remove the `thresholdKnob.setEnabled(false)` line from the REC_ARMED block so the knob remains interactive while armed:
```cpp
else if (recState == 3) // REC_ARMED
{
    recordButton.setButtonText("Armed");
    recordButton.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0xFFD4A017)); // amber
    loadSampleButton.setEnabled(false);
    encodeRateKnob.setEnabled(false);
    encodeRateBox.setEnabled(false);
    // thresholdKnob stays enabled so user can adjust sensitivity while waiting
}
```

## Info

### IN-01: Magic numbers for RecState enum values in editor code

**File:** `src/plugin/PluginEditor.cpp:64,68,1301,1311`
**Issue:** The editor uses raw integer literals (`1`, `3`) to test recording state instead of referencing the enum values. The enum `RecState` is private to the processor class, so the editor cannot use it directly. The comments explain what each value means, but this is fragile -- if the enum values change, the editor comparisons silently break.

**Fix:** Either expose the enum constants as `static constexpr int` members on the public API, or add helper methods like `isIdle()` to complement the existing `isRecording()` and `isArmed()`:
```cpp
// In PluginProcessor.h, public:
static constexpr int kRecIdle = 0;
static constexpr int kRecRecording = 1;
static constexpr int kRecStopped = 2;
static constexpr int kRecArmed = 3;
```

---

_Reviewed: 2026-05-28T21:50:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
