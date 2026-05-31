# Phase 61: Coherent Controls - Research

**Researched:** 2026-05-30
**Domain:** JUCE↔C-core control fan-out (sampler per-voice volume/pan/INV/NON/PMON), Q15 fixed-point voice mixer, real-time-safe message→audio plumbing
**Confidence:** HIGH (all findings verified by direct code read of the canonical refs; no external libraries involved)

## Summary

Phase 61 is a **localized JUCE-layer change**, not a C-core change. The DSP engine (`src/spu94/spu94_voice.c`) already exposes every setter the fan-out needs (`base_vol_l/r` writes, `set_non`, `set_pmon`) and is already RT-safe and unit-tested. The work is in `PluginProcessor.cpp`: take the continuous control-apply block that today writes **voice 0 only** (lines 863–885) and the MIDI note-on dispatch that today keys on with **velocity-only, centered pan** (lines 1414–1438), and make both honor the GUI Level/Pan/INV/NON/PMON across `[0, activeVoiceCount)` — the active set Phase 60 established.

Two facts dominate the plan. **First, D-01a is confirmed:** the reverb send is tapped *after* per-voice `base_vol`→`vol` and *before* `master_vol` (verified in `spu94_voice_mixer_tick`, `spu94_voice.c:737–743` vs `:751`). So putting Level on `base_vol` ducks each voice's reverb feed exactly as voice 0 does today — and putting it on `master_vol` would not. **Second, the velocity×Level interaction is the real engineering content:** a naive fan-out of the apply block would overwrite each voice's velocity-derived `base_vol` every block, erasing velocity. To honor D-01 ("Level rides on top of velocity"), the per-note velocity must be retained in an audio-thread-only `int16_t[24]` array (exact precedent exists: `duckOrigLevel_l/r[24]`, `PluginProcessor.h:544-545`), and `base_vol` recomputed each block as `f(velocity[v], guiVoiceVol)`.

The validation strategy has a clean, proven template: Phase 60 shipped `tests/plugin/test_voice_alloc.cpp`, a headless `SPU94AudioProcessor` test using a `friend struct` seam to drive private methods directly and observe the process-wide mixer's state. Phase 61 mirrors it exactly. The catch the planner must internalize: **the C-core Unity tests and rt_safety nm-gates do NOT cover `PluginProcessor.cpp`** — the fan-out loop is new C++ in the audio callback that those gates never see. RT-safety for the new code is preserved by construction (no allocations/locks — it's atomic loads + a bounded loop of existing setters), and proven by a code-review assertion plus the fact that all called functions are already on the rt_safety link closure.

**Primary recommendation:** Extract the continuous apply logic into a private `applyContinuousVoiceControls()` method that loops `[0, activeVoiceCount)`, reads each voice's stored velocity, and writes `base_vol`/NON/PMON; store per-note velocity in a new audio-thread-only `int16_t noteVelocity[24]` array set at MIDI key-on; unify the velocity range with the GUI `base_vol` range; test it with a new `tests/plugin/test_voice_controls.cpp` modeled 1:1 on `test_voice_alloc.cpp`.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Level/Pan/INV → combined signed `base_vol_l/r` | JUCE message thread (`PluginEditor::updateVoiceVolumes`) | — | Already pre-combined into `guiVoiceVolL/R` on knob change (`PluginEditor.cpp:1570-1604`); engine never re-derives Pan/Level/INV. **Do not move this math.** |
| Fan-out of combined `base_vol` to active voices | JUCE audio thread (`PluginProcessor::processBlock` apply block) | — | The fan-out *loop* is host glue; it reads atomics + per-voice velocity and writes engine fields. Not DSP. |
| Per-note velocity capture | JUCE audio thread (MIDI note-on handler) | — | Velocity is a MIDI/host concept; SPU hardware has no velocity register. Stored host-side. |
| Applying `base_vol`→`vol`→output (incl. reverb send tap) | C core (`spu94_voice_tick` STEP 3 + `spu94_voice_mixer_tick`) | — | All DSP signal flow lives in the C core (PROJECT.md invariant). Already correct; Phase 61 only feeds it different `base_vol`. |
| NON/PMON per-voice flag storage + effect | C core (`non_flags`/`pmon_flags` + mixer tick) | JUCE (sets the bit per voice) | Engine owns the flag→behavior mapping; host fans the setter call across the active set. |
| ADSR shape per voice | C core (per-voice `adsr` struct) | JUCE (`buildAdsrConfig` at each key-on) | **Already fanned out** (D-07); every MIDI key-on passes the config. No new wiring. |

## Standard Stack

Not applicable in the usual sense — Phase 61 introduces **zero new dependencies**. It uses only:

| Component | Location | Purpose | Why standard here |
|-----------|----------|---------|-------------------|
| `spu94_voice_mixer_t` API | `include/spu94/spu94_voice.h` | Per-voice setters + tick | Already the engine's control plane; Phase 61 just calls existing setters on more indices |
| `q15_mul_truncate` / `sat_s16` | `include/spu94/spu94_q15.h` | Q15 velocity×Level combine | The project's canonical fixed-point multiply (ASR>>15, saturate); matches every other volume calc in the codebase |
| `std::atomic` release/acquire | `PluginProcessor.h` | `activeVoiceCount`, `guiVoiceVolL/R`, `guiVoiceNon/Pmon` | Phase 60's established message→audio pattern; reused verbatim |
| Unity + `friend struct` seam | `tests/plugin/test_voice_alloc.cpp` | Headless processor test | The exact harness Phase 60 used for the analogous allocator change |

**Installation:** None. No `npm`/`pip`/`cargo`. No new CMake `FetchContent`. The new test reuses the `test_voice_alloc` CMake recipe (`tests/plugin/CMakeLists.txt`).

## Package Legitimacy Audit

**Not applicable.** Phase 61 installs no external packages. All code is first-party C/C++ already in the repository. slopcheck / registry verification steps are skipped because there is nothing to verify. (Recorded explicitly per the package-legitimacy protocol's "nothing to install" path.)

## Architecture Patterns

### System Architecture Diagram (the control path Phase 61 changes)

```
 GUI knobs (message thread)                         Engine (C core, audio thread)
 ┌─────────────────────────┐                        ┌──────────────────────────────┐
 │ Pan / Level / INV        │                        │ voices[v].base_vol_l/r        │
 │  updateVoiceVolumes()    │  store (relaxed)       │   │ (STEP 0: sweep ceiling)   │
 │   → guiVoiceVolL/R  ●────┼──────────────┐         │   ▼                           │
 │ NON toggle → guiVoiceNon │              │         │ voices[v].vol_l/r             │
 │ PMON toggle→ guiVoicePmon│              │         │   │ (STEP 3: Q15 multiply)    │
 └─────────────────────────┘              │         │   ▼                           │
                                          │         │ per-voice out_l/out_r ────┐   │
 MIDI note-on (audio thread)              │         │                           │   │
 ┌─────────────────────────┐              │         │  dry_sum += out  ◄────────┤   │
 │ note → pitch (per-note)  │              │         │  ┌── REVERB SEND TAP ─────┘   │
 │ velocity ──────────────● │              │         │  │  rev_sum += out (if EON)    │
 │  store noteVelocity[v]   │              │         │  ▼  [D-01a: AFTER base_vol,    │
 │ allocateVoice() → v      │              │         │      BEFORE master_vol]        │
 └───────────┬─────────────┘              │         │  master_vol ► dry_l/dry_r      │
             │                            │         └──────────────────────────────┘
             ▼                            ▼
   ┌───────────────────────────────────────────────┐
   │ applyContinuousVoiceControls()  (NEW, extracted)│
   │  for v in [0, activeVoiceCount):                │   ← runs every processBlock,
   │    base = combine(noteVelocity[v], guiVoiceVol) │     audio thread, NO alloc/lock
   │    voices[v].base_vol_l/r = base                │
   │    set_non(v, guiVoiceNon); set_pmon(v, …)      │
   └───────────────────────────────────────────────┘
```

The reader can trace: a knob move stores a combined value on the message thread → the audio-thread apply loop reads it + the per-voice velocity → writes `base_vol` for every active voice → the engine's existing tick applies it (dry + reverb send) identically for all of them.

### Pattern 1: Extract the apply block into a callable private method
**What:** Move the body of `PluginProcessor.cpp:863-885` into `void SPU94AudioProcessor::applyContinuousVoiceControls()`, called from `processBlock` where the block is today.
**When to use:** Required so the `friend struct` test can drive the fan-out without running a whole `processBlock` (which is gated by `voiceSampleLoaded` and pulls in SRC/reverb).
**Example:**
```cpp
// Mirrors how allocateVoice (PluginProcessor.cpp:2601) is independently callable.
// Source: pattern verified in tests/plugin/test_voice_alloc.cpp:50 (t.alloc forwards to private)
void SPU94AudioProcessor::applyContinuousVoiceControls()
{
    auto* mx = spu94_get_voice_mixer();
    const int count = activeVoiceCount.load(std::memory_order_acquire);  // Phase 60 acquire
    const int16_t guiL = guiVoiceVolL.load(std::memory_order_relaxed);
    const int16_t guiR = guiVoiceVolR.load(std::memory_order_relaxed);
    const bool nonOn   = guiVoiceNon.load(std::memory_order_relaxed);
    const bool pmonOn  = guiVoicePmon.load(std::memory_order_relaxed);
    for (int v = 0; v < count; ++v) {
        // base_vol = velocity ⊗ (Level·Pan·INV).  noteVelocity[v] is audio-thread-only.
        mx->voices[v].base_vol_l = combineVel(noteVelocity[v], guiL);
        mx->voices[v].base_vol_r = combineVel(noteVelocity[v], guiR);
        spu94_voice_mixer_set_non(mx, v, nonOn ? 1 : 0);
        spu94_voice_mixer_set_pmon(mx, v, pmonOn ? 1 : 0);
    }
    // pitch stays voice-0-only (NOT fanned out — D-scope): keep set_pitch(0) call as-is.
}
```

### Pattern 2: Per-note velocity retained in an audio-thread-only array
**What:** Add `int16_t noteVelocity[24]` (NOT atomic — audio-thread-only, exactly like `duckOrigLevel_l[24]`). Set it in the MIDI note-on handler at allocation time; read it in the fan-out.
**When to use:** This is the mechanism that lets Level ride on top of velocity (D-01). Without it, the per-block fan-out erases velocity.
**Example:**
```cpp
// In MIDI note-on (PluginProcessor.cpp:1426 region):
int voice = allocateVoice(note);
int vel = msg.getVelocity();                       // 0..127
noteVelocity[voice] = velToQ15(vel);               // store normalized velocity for this voice
int16_t base_l = combineVel(noteVelocity[voice], guiVoiceVolL.load(...));
int16_t base_r = combineVel(noteVelocity[voice], guiVoiceVolR.load(...));
spu94_voice_mixer_key_on(mx, voice, 0, pitch, base_l, base_r, 1, adsrPtr);
// The Trigger/voice-0 audition path (line 830) should set noteVelocity[0] = full
// (D-08: Trigger = full velocity × Level) so the apply loop keeps voice 0 consistent.
```
**Precedent:** `int16_t duckOrigLevel_l[24]`, `duckOrigLevel_r[24]` (`PluginProcessor.h:544-545`) — plain int16 per-voice arrays, audio-thread-only, no atomics, storing volume snapshots. Same shape, same threading discipline.

### Anti-Patterns to Avoid
- **Putting Level on `master_vol` instead of `base_vol`** — breaks D-01a (would NOT duck the reverb send; the tap is upstream of master). Also `master_vol` is a single post-sum gain, not per-voice; it can't carry per-voice INV sign.
- **Re-deriving Pan/Level/INV on the engine side per voice** — the GUI already collapses all three into one signed `base_vol` (`PluginEditor.cpp:1583-1597`). Fan out the *combined* value. Splitting it back apart duplicates math and invites drift.
- **Overwriting `base_vol` every block without folding in velocity** — silently kills velocity sensitivity (the naive fan-out trap). See Pitfall 1.
- **Fanning out pitch** — explicitly out of scope (CONTEXT domain): each MIDI note keeps its own pitch from the note number; the GUI pitch knob stays a voice-0/Trigger control. Keep `set_pitch(mx, 0, …)` as-is.
- **Adding allocations/locks in the apply loop** — would breach the RT invariant. The loop must be atomic loads + existing O(1) setters only.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Pan→L/R gain law | A new pan curve | Existing `updateVoiceVolumes()` (`PluginEditor.cpp:1570`) | Already implements the linear pan law + INV sign flip + clamp; combined value is what the engine wants |
| Q15 velocity×Level multiply | Custom fixed-point mul with ad-hoc rounding | `q15_mul_truncate()` (`spu94_q15.h:112`) | Canonical ASR>>15 + `sat_s16`; bit-consistent with every other volume calc; ADR-0001 documented |
| Per-voice flag set | Manual bitmask twiddling in JUCE | `spu94_voice_mixer_set_non/_set_pmon` (`spu94_voice.c:527,509`) | Already validate the index and set the right bit; PMON bit-0 ignore rule lives inside the engine |
| Message→audio count read | New sync primitive | `activeVoiceCount.load(acquire)` (Phase 60, `PluginProcessor.cpp:2609`) | Established pattern; pairs with the `release` store in `setActiveVoiceCount` |
| Headless processor test harness | New test scaffold | Clone `tests/plugin/test_voice_alloc.cpp` + its CMake recipe | Solves the singleton-mixer isolation + private-member-access problems already |

**Key insight:** Every primitive Phase 61 needs already exists and is tested. The phase is *wiring*, not *building*. The only genuinely new value-add is the velocity-retention array and the velocity×Level combine — both have direct precedents in the same file.

## Runtime State Inventory

Phase 61 is **not** a rename/refactor/migration phase — it changes runtime control behavior, not stored identifiers or external service config. The five categories below are answered for completeness because the change touches a process-wide singleton.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — no persisted keys/IDs change. (Voice-count *persistence* is Phase 63; Phase 61 stores nothing new to disk.) | None |
| Live service config | None — no external service. | None |
| OS-registered state | None. | None |
| Secrets/env vars | None. | None |
| Build artifacts | The new `test_voice_controls` target adds a CTest case; CMake re-config needed (same as Phase 60's `test_voice_alloc` add). Process-wide mixer singleton `s_mixer` (in `spu94_process.c`) is shared across test cases → each case MUST `spu94_voice_mixer_init()` to re-isolate (verified pattern: `test_voice_alloc.cpp:70`). | Re-run cmake; follow the per-case `isolate()` discipline |

## Common Pitfalls

### Pitfall 1: Velocity erased by the per-block fan-out
**What goes wrong:** The continuous apply block runs **every** `processBlock` and writes `base_vol` directly. If it writes `guiVoiceVolL/R` unconditionally to voices `[0, count)`, then on the block *after* a MIDI note-on, the note's velocity-derived `base_vol` (set at `key_on`) is overwritten with the flat GUI value — velocity sensitivity vanishes. This contradicts D-01.
**Why it happens:** `key_on` sets `base_vol = vol_l` (`spu94_voice.c:62-63`), but the apply loop clobbers it next block. Today this is invisible because the loop only touches voice 0 and the Trigger path intends a flat level there.
**How to avoid:** Store per-note velocity (`noteVelocity[24]`), and have the apply loop compute `base_vol = combineVel(noteVelocity[v], guiVoiceVol)` instead of writing the bare GUI value. Velocity and Level then both survive every block.
**Warning signs:** A unit test that keys on two voices at different velocities, runs the apply method, then reads `base_vol_l` — if both come out equal, velocity was erased.

### Pitfall 2: Velocity full-scale (0x7FFF) ≠ base_vol full-scale (0x3FFF)
**What goes wrong:** Today's MIDI note-on computes `vol = (vel * 0x7FFF) / 127` → at velocity 127, `vol = 0x7FFF` (32767). But `base_vol_l/r` is documented `±0x3FFF` (16383) and the GUI caps Level at `0x3FFF`. So a full-velocity MIDI note currently lands `base_vol = 0x7FFF` — **roughly twice** the GUI Trigger's max level (pre-master saturation). Unifying the two paths without reconciling the ranges will make the rig jump in level the moment Level/velocity start co-governing.
**Why it happens:** The two code paths were written independently (velocity path uses `0x7FFF` full-scale; GUI path uses `0x3FFF`). Verified: `PluginProcessor.cpp:1425` vs `PluginEditor.cpp:1583`.
**How to avoid:** Pick ONE full-scale for `base_vol` (the engine's documented `0x3FFF`) and express both inputs in it. Cleanest: treat velocity as a Q15 *scalar* `velQ15 = (vel * 0x7FFF)/127` in `[0, 0x7FFF]`, treat `guiVoiceVol` as the `0x3FFF`-scaled ceiling, and combine `base_vol = q15_mul_truncate(guiVoiceVol, velQ15)`. Full velocity (`0x7FFF`) × full Level (`0x3FFF`) → `≈0x3FFE` (the GUI's own max), so the Trigger button and a velocity-127 MIDI note converge — which is exactly D-08. This is a **developer-discretion math decision** (CONTEXT explicitly delegates "Exact Q15 math for combining velocity × Level × Pan × INV"); flag it for the planner to lock, not the user.
**Warning signs:** Velocity-127 note noticeably louder than the Trigger button at Level=100% after the change.

### Pitfall 3: Process-wide mixer singleton bleeds state across test cases
**What goes wrong:** `spu94_get_voice_mixer()` returns a file-scope static (`s_mixer`). A test case that keys on voices leaves `pending_kon`/`base_vol`/`non_flags` set for the next case → false passes/fails.
**Why it happens:** The mixer is intentionally a singleton (~518 KB, can't live on the stack). Same issue Phase 60 hit (documented `test_voice_alloc.cpp:24-26`).
**How to avoid:** Every case calls `spu94_voice_mixer_init()` AND resets the processor's per-voice arrays (`noteForVoice`, `nextVoice`, and the new `noteVelocity`) via the friend seam — copy the `isolate()` helper from `test_voice_alloc.cpp:68`.
**Warning signs:** Tests pass alone but fail when run in sequence, or vice versa.

### Pitfall 4: Fan-out fights the VCA sweep / sidechain duck for ownership of base_vol
**What goes wrong:** `base_vol` is the **ceiling the sweep scales against** (`spu94_voice.c:57` comment; STEP 0 at `:130-157`). The sidechain duck also snapshots/restores per-voice level (`duckOrigLevel_l/r[24]`). If the apply loop rewrites `base_vol` every block while a sweep (tremolo/auto-pan) or a duck ramp is mid-flight on that voice, the two can stomp each other.
**Why it happens:** Multiple subsystems write the same field at audio rate. Today only voice 0 is fanned out, so the collision surface is tiny; widening to N voices widens it.
**How to avoid:** Confirm the intended precedence (CONTEXT D-01a implies Level *is* the ceiling and the sweep modulates *relative to it*, so writing `base_vol` each block is correct — the sweep reads it as the ceiling on the same tick). The planner should add a regression check: enable tremolo on a fanned voice, move Level, assert the sweep still tracks the new ceiling and doesn't reset. The duck interaction is narrower (duck targets specific voices via `duckSource`); verify the duck's `base_vol` restore still lands after the apply loop runs (ordering within `processBlock`).
**Warning signs:** Tremolo depth or duck depth changes audibly when Level is moved; sweep "jumps" on Level change.

### Pitfall 5: "No audible change at 24" is about the *bound*, not the per-voice volume
**What goes wrong:** The planner reads "default 24 must reproduce current behavior with no audible change" and assumes fanning out the apply block to 24 voices is a no-op. It is **not** — MIDI voices 1–23 today play velocity-centered and ignore Level/Pan; after the fix they honor Level/Pan. That IS an audible change, and it's the entire point (VCTRL-01/02/03).
**Why it happens:** The regression-safety phrasing is inherited from Phase 60, where bounding the allocator to 24 genuinely is a no-op. For Phase 61 the invariant is narrower.
**How to avoid:** Interpret the regression invariant precisely (see State of the Art table). The genuinely-must-not-change behaviors are: (a) the **voice-0/Trigger audition** path, and (b) **no voice is excluded** at count 24 (the loop reaches all 24, same set as the allocator). The per-voice *volume* of MIDI voices changes by design.
**Warning signs:** A "regression" test that asserts MIDI voice 5's `base_vol` is unchanged after the fix — that test encodes the bug.

## Code Examples

### Combine velocity × Level into base_vol (the discretion math, one defensible form)
```cpp
// Source: derived from PluginProcessor.cpp:1425 (velocity) + PluginEditor.cpp:1583 (gui scale)
//         + q15_mul_truncate semantics (spu94_q15.h:112). Range-unified to 0x3FFF.
static inline int16_t velToQ15(int vel) {                 // 0..127 -> 0..0x7FFF
    if (vel < 0) vel = 0; if (vel > 127) vel = 127;
    return static_cast<int16_t>((vel * 0x7FFF) / 127);
}
static inline int16_t combineVel(int16_t velQ15, int16_t guiVol /* signed ±0x3FFF */) {
    // q15_mul keeps the sign of guiVol (so INV's negative survives) and scales by velocity.
    return q15_mul_truncate(guiVol, velQ15);
}
```

### Headless test case (clone of the Phase 60 pattern) proving Level reaches every active voice
```cpp
// Source: structure copied verbatim from tests/plugin/test_voice_alloc.cpp:88-137
// Friend seam adds: void setGuiVolLR(int16_t l, int16_t r); void applyControls();
//                   int16_t baseL(int v) const { return spu94_get_voice_mixer()->voices[v].base_vol_l; }
bool test_level_reaches_all_active(VoiceControlsTest& t) {
    isolate(t);                       // mixer_init + reset noteForVoice/nextVoice/noteVelocity
    t.setCount(6);
    for (int i = 0; i < 6; ++i) t.keyOnMidi(60 + i, /*vel*/100);  // fill voices 0..5
    t.setGuiVolLR(0x2000, 0x2000);    // Level ~50%, pan center
    t.applyControls();                // drive the extracted fan-out method directly
    for (int v = 0; v < 6; ++v)
        if (t.baseL(v) == 0 || t.baseL(v) > 0x2000) return false;  // every active voice scaled
    // And a voice OUTSIDE the active set is untouched:
    return t.baseL(7) == /* its key-on value, not the new Level */ EXPECTED_UNTOUCHED;
}
```

## State of the Art

| Old Approach (shipped v1.11.0) | Current Approach (Phase 61) | When Changed | Impact |
|--------------------------------|------------------------------|--------------|--------|
| Apply block writes `voices[0].base_vol` only | Apply loop writes `voices[0..count).base_vol` | this phase | Level/Pan/INV govern the whole rig (VCTRL-01/02) |
| MIDI note-on: `key_on(…, vol, vol, …)` — velocity-only, centered pan | `key_on(…, combine(vel,guiL), combine(vel,guiR), …)` | this phase | Velocity *and* Level/Pan co-govern (D-01) |
| NON/PMON set on voice 0 only | NON/PMON set on `[0, count)` | this phase | Toggles change character of every active voice (VCTRL-03) |
| ADSR via `buildAdsrConfig()` at every key-on | **unchanged** | already shared (D-07) | VCTRL-03 ADSR clause already satisfied — add regression check only |
| Velocity full-scale `0x7FFF`, GUI full-scale `0x3FFF` (divergent) | Single `0x3FFF` base_vol scale | this phase | Resolves a latent ~2× level mismatch between MIDI and Trigger paths (Pitfall 2) |

**Deprecated/outdated:** Nothing removed. No engine API changes. The voice-0-only writes are *widened*, not replaced.

**Regression invariant (precise statement):** At `activeVoiceCount == 24`, (a) the loop reaches the same voice set the allocator does (all 24 — no exclusion), and (b) the voice-0/Trigger audition path is bit-identical to v1.11.0. The per-voice `base_vol` of *MIDI-played* voices 1–23 changes by design (velocity-centered → velocity×Level/Pan/INV); that is the fix, not a regression.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The cleanest velocity×Level combine is `base_vol = q15_mul(guiVol, velQ15)` on a unified `0x3FFF` scale, making velocity-127 ≈ Trigger-at-100% | Pitfall 2, Code Examples | LOW — CONTEXT explicitly delegates this math to Claude's discretion; planner locks the exact form. A different curve (e.g., velocity as additive trim) is possible but contradicts the "fader on top of velocity" framing. |
| A2 | Writing `base_vol` every block while a sweep is active is the intended precedence (sweep reads it as ceiling same tick) | Pitfall 4 | MEDIUM — verified the sweep *reads* base_vol as ceiling (`spu94_voice.c:133,147`), but the duck/apply ordering inside `processBlock` is a behavior the planner should add an explicit regression test for rather than assume. |
| A3 | The extracted `applyContinuousVoiceControls()` method is the right testability seam (vs. testing through `processBlock`) | Pattern 1, Validation Architecture | LOW — directly mirrors how `allocateVoice` is exposed and tested in Phase 60; the alternative (full processBlock) is gated by `voiceSampleLoaded` and far heavier. |
| A4 | Per-note velocity belongs in a non-atomic `int16_t noteVelocity[24]` (audio-thread-only) | Pattern 2 | LOW — exact precedent `duckOrigLevel_l/r[24]` is non-atomic audio-thread-only; note-on and the apply loop both run on the audio thread, so no cross-thread hazard. |

## Open Questions (RESOLVED)

1. **Exact velocity×Level curve** — *What we know:* CONTEXT delegates the Q15 math to discretion; A1 gives a defensible default (multiplicative, unified `0x3FFF`). *What's unclear:* whether Anthony wants velocity-127 to *exactly* equal Trigger-at-100% (A1's result) or to retain some headroom. *Recommendation:* the planner locks A1 as the default and notes it as a sound-affecting choice; if a listening session later disagrees, it's a one-line change to `combineVel`. (Do not surface as a user decision — it's developer math per CONTEXT and the "no dev choices in discuss" feedback.)
   *RESOLVED:* Plan 61-02 Task 1 locks A1 — `combineVoiceVol` via `q15_mul_truncate` on the unified `0x3FFF` scale; velocity-127 × Level-100% ≈ `0x3FFE` = Trigger max.

2. **Apply-loop vs. sidechain-duck ordering** — *What we know:* both write per-voice level on the audio thread; the duck snapshots `duckOrigLevel`. *What's unclear:* if the apply loop runs before or after the duck restore within `processBlock`, and whether a fanned voice under active duck behaves correctly. *Recommendation:* the planner adds an explicit ordering decision + a regression test (duck a fanned voice, move Level, assert duck depth preserved). Phase 46 duck code is at `PluginProcessor.cpp:1441+`.
   *RESOLVED:* Plan 61-02 Task 3 fixes apply-before-duck ordering with a required comment at the call site, plus the `sweep_interaction` regression case.

3. **Voice-0 Trigger velocity seeding** — *What we know:* D-08 says the Trigger button plays voice 0 at "full velocity × Level". *What's unclear:* the mechanism — set `noteVelocity[0] = 0x7FFF` whenever the Trigger fires (line 822-858 block) so the unified apply loop keeps voice 0 consistent. *Recommendation:* planner wires `noteVelocity[0] = full` in the Trigger key-on path; low risk, one line.
   *RESOLVED:* Plan 61-02 Task 1 seeds `noteVelocity[0] = 0x7FFF` in the Trigger key-on path.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| C/C++ toolchain (gcc/clang) | Build | ✓ (project builds today) | — | — |
| CMake + CTest | Test harness | ✓ (existing suite) | — | — |
| JUCE (vendored/fetched) | `test_voice_controls` console app | ✓ (used by `test_voice_alloc`) | — | — |
| Unity | C-core voice tests (regression) | ✓ (`tests/unit/vendor/Unity`) | — | — |
| strace | rt_safety syscall gate | ✓ on Linux (dev host is Linux) | — | gate skips gracefully off-Linux |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None — all tooling for this phase already runs the existing suite.

## Validation Architecture

> `nyquist_validation: true` in config → this section is REQUIRED.

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C core) + JUCE console-app harness (plugin layer) |
| Config file | `tests/plugin/CMakeLists.txt` (plugin tests), `tests/unit/voice/CMakeLists.txt` (C-core) |
| Quick run command | `ctest -R voice_controls --output-on-failure` (new cases) |
| Full suite command | `ctest --output-on-failure` (from the build dir) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VCTRL-01 | Level changes loudness of EVERY active voice, not just voice 0 | unit (headless processor) | `ctest -R voice_controls_level_all_active` | ❌ Wave 0 |
| VCTRL-02 | Pan places EVERY active voice at the same stereo position | unit | `ctest -R voice_controls_pan_all_active` | ❌ Wave 0 |
| VCTRL-03 (toggles) | NON/PMON land on EVERY active voice (`non_flags`/`pmon_flags` bits `[0,count)` set) | unit | `ctest -R voice_controls_non_pmon_all_active` | ❌ Wave 0 |
| VCTRL-03 (ADSR) | Every triggered voice shares the GUI envelope (already true — D-07) | unit (regression guard) | `ctest -R voice_controls_adsr_shared` | ❌ Wave 0 |
| D-01 | Velocity survives the per-block fan-out (two velocities → two `base_vol`) | unit | `ctest -R voice_controls_velocity_rides_level` | ❌ Wave 0 |
| D-06 / regression | At count=24 the loop reaches all 24; voice-0/Trigger path unchanged | unit | `ctest -R voice_controls_default24_regression` | ❌ Wave 0 |
| D-06 (bound) | A voice OUTSIDE `[0,count)` does NOT receive control updates | unit | `ctest -R voice_controls_out_of_range_untouched` | ❌ Wave 0 |
| Pitfall 4 | Fanned voice under active tremolo: Level move re-bases ceiling, no reset | unit | `ctest -R voice_controls_sweep_interaction` | ❌ Wave 0 |
| RT-safety | C-core link closure stays heap/lock-free (engine unchanged) | gate | `ctest -L rt_safety` | ✅ exists (must stay green) |

**Test design (PROVE each control reaches all active voices):** The harness instantiates a real `SPU94AudioProcessor`, uses a `friend struct VoiceControlsTest` to (1) set `activeVoiceCount`, (2) key on N voices at chosen velocities via a thin forwarder, (3) set `guiVoiceVolL/R`/`guiVoiceNon`/`guiVoicePmon`, (4) call the extracted `applyContinuousVoiceControls()` directly, then (5) read `spu94_get_voice_mixer()->voices[v].base_vol_l/r`, `->non_flags`, `->pmon_flags` and assert across all `[0,count)`. Observing engine state directly (no audio render) is the same proof technique Phase 60 used for allocation.

### Sampling Rate
- **Per task commit:** `ctest -R voice_controls --output-on-failure` (the new cases — seconds)
- **Per wave merge:** `ctest --output-on-failure` (full suite incl. `voice_tick_unit`, `adsr_unit`, rt_safety)
- **Phase gate:** Full suite green + `ctest -L rt_safety` green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/plugin/test_voice_controls.cpp` — new headless processor test, cases above; clone structure from `tests/plugin/test_voice_alloc.cpp`
- [ ] `tests/plugin/CMakeLists.txt` — add `test_voice_controls` target + one `add_test` per case (clone the `test_voice_alloc` block at the end of the file)
- [ ] `src/plugin/PluginProcessor.h` — declare `friend struct VoiceControlsTest;` (alongside the existing `friend struct VoiceAllocTest;` at line 287) and the new private method `void applyContinuousVoiceControls();` + array `int16_t noteVelocity[24];`
- [ ] (Optional C-core guard) `tests/unit/voice/test_voice_tick.c` — a case asserting `base_vol` fans correctly through `mixer_tick` for several voices, if the planner wants engine-level coverage in addition to the processor-level proof. Not strictly required — the engine setters are already tested; the *new* logic is host-side.
- Framework install: none — all harness deps already build.

## Sources

### Primary (HIGH confidence — direct code read this session)
- `src/spu94/spu94_voice.c:634-757` (`spu94_voice_mixer_tick`) — **confirms D-01a**: reverb send (`rev_sum`) tapped at `:740-743` using the same post-`base_vol` `vl/vr`, BEFORE `master_vol` applied at `:751-752`. `master_vol` applied to dry only.
- `src/spu94/spu94_voice.c:105-407` (`spu94_voice_tick`) — STEP 0 sweep ceiling = `base_vol` (`:130-157`); STEP 3 per-voice volume multiply (`:356-357`); STEP 2.5 ADSR.
- `src/spu94/spu94_voice.c:47-91` (`spu94_voice_key_on`) — sets `base_vol_l/r = vol_l/r` (`:62-63`) — the velocity/Level crux.
- `include/spu94/spu94_voice.h:55-58,136-150,179-187` — `base_vol_l/r` semantics, `master_vol` (dry-only), `set_non`/`set_pmon`/`set_pitch` signatures, `eon_flags`/`non_flags`/`pmon_flags`.
- `src/plugin/PluginProcessor.cpp:822-885` — the voice-0-only continuous apply block (the fan-out target) + Trigger key-on.
- `src/plugin/PluginProcessor.cpp:1414-1438` — MIDI note-on dispatch: `vol = (vel*0x7FFF)/127`, `key_on(…vol,vol…)` centered, `buildAdsrConfig()` applied per note (proves D-07).
- `src/plugin/PluginProcessor.cpp:2593-2629` — `setActiveVoiceCount` (release), `allocateVoice` (acquire, `% count`), `findVoiceForNote`.
- `src/plugin/PluginProcessor.h:431-457,287,544-545,68-69` — `noteForVoice[24]`, `nextVoice`, `activeVoiceCount`, gui atomics, `friend struct VoiceAllocTest`, `duckOrigLevel_l/r[24]` precedent, `setGuiVoiceVolL/R`.
- `src/plugin/PluginEditor.cpp:536-577,1570-1604` — Pan/Level/INV controls → `updateVoiceVolumes()` → combined signed `base_vol` (`:1583-1597`).
- `include/spu94/spu94_q15.h:54-114` — `sat_s16`, `q15_mul_truncate` (ASR>>15, saturate) — the combine primitive.
- `tests/plugin/test_voice_alloc.cpp` (full) — the headless friend-seam test template Phase 61 clones.
- `tests/plugin/CMakeLists.txt` — `test_voice_alloc` target/recipe to clone; confirms processor-level tests link `PluginProcessor.cpp` + JUCE.
- `tests/rt_safety/CMakeLists.txt` — confirms rt_safety gates audit `spu94_shared`/`spu94_static` (C core) link closure, NOT `PluginProcessor.cpp`.
- `tests/unit/voice/test_voice_tick.c:635-707` — mixer-level Unity test patterns (`s_test_mixer`, `set_eon`/`set_non` bit assertions).
- `.planning/PROJECT.md:160-191` — RT-safety + "all DSP in C core / hosts are thin wrappers" invariants.

### Secondary (MEDIUM confidence)
- `git tag` — confirms `v1.11.0` is the shipped baseline; `git log` shows Phase 60 GREEN landed (`d1f19c1`).
- Golden test dirs (`tests/golden/*`) — confirmed to cover the **reverb** path only (presets), not the sampler voice path → no golden coverage to lean on for VCTRL; new mixer-state assertions are the proof.

### Tertiary (LOW confidence)
- None. No external/web sources needed; this is a closed-codebase wiring phase.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new deps; every primitive verified in-repo.
- Architecture (fan-out + velocity retention): HIGH — both have direct precedents read this session (`allocateVoice` callable pattern; `duckOrigLevel[24]` array).
- D-01a reverb-tap confirmation: HIGH — read the exact accumulation lines.
- Velocity×Level math (A1): MEDIUM — defensible default, but explicitly discretion; planner locks the form.
- Sweep/duck interaction (A2): MEDIUM — sweep-reads-ceiling confirmed; intra-block ordering flagged for an explicit test.
- Pitfalls: HIGH — Pitfalls 1, 2, 3 are derived from confirmed code facts; 4, 5 are confirmed-mechanism + interpretation.

**Research date:** 2026-05-30
**Valid until:** 2026-06-29 (stable — internal codebase; only invalidated by edits to the cited files)

## RESEARCH COMPLETE
