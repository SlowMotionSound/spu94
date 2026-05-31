# Phase 60: Engine Voice-Count & Allocation - Context

**Gathered:** 2026-05-30
**Status:** Ready for planning
**Source:** v1.12.0 roadmap + inline design Q&A

<domain>
## Phase Boundary

Engine-side voice-count and allocation logic for the **sampler** path (MIDI-played
notes routed through `allocateVoice` → `spu94_voice_mixer_key_on`). Phase 60 teaches
the sampler engine to (a) know how many voices are currently active (1–24) and
(b) allocate played notes only among those active voices, reusing voices in
round-robin order when more notes play than the active count allows.

**In scope:** internal active-voice-count state (default 24) with a programmatic
setter, and the bounded round-robin allocation/stealing behavior that respects it.

**Out of scope:** the user-facing voice-count control in the sampler window — that
is VCOUNT-01 / **Phase 62**. Phase 60 ships the engine behavior; Phase 62 wires the
GUI knob to it. This phase is driven/tested programmatically.

</domain>

<decisions>
## Implementation Decisions

### Allocation & stealing — round-robin, bounded to the active count
- Keep the existing round-robin allocator. Today `allocateVoice` advances its cursor
  with `nextVoice = (nextVoice + 1) % 24` — the hardcoded `24` becomes the active
  count so the cursor cycles only among voices `[0 … count-1]`.
- Overflow (more simultaneous notes than the active count) wraps the cursor and reuses
  the least-recently-allocated active voice. No "find quietest / oldest-release" search —
  plain round-robin index cycling, as the existing code does.
- Satisfies VALLOC-01 (allocate only among active voices), VALLOC-02 (overflow reuses a
  sounding voice), VALLOC-03 (count = 1 → always voice 0).

### Mono retrigger — hard envelope restart
- count = 1 → every note key-ons voice 0; the key-on restarts the ADSR from attack.
  No legato / no glide. This is the current key-off-then-key-on takeover and is
  PS1-faithful (a key-on resets the envelope).
- The same hard restart applies on any steal in poly mode, because stealing is a fresh
  key-on on a reused voice.

### Steal click handling — hard cut for now
- Keep the current immediate takeover (key-off the prior note, then key-on the new note
  on the same voice). Do NOT add anti-click fade logic in Phase 60.
- We listen for clicks during testing and decide afterward whether a short (~1–2 ms)
  fade is worth adding. (See Deferred.)

### Active-voice-count state — internal, default 24
- Add the active count as processor/engine state with a programmatic setter (tests drive
  it directly). Clamp to [1, 24].
- Default 24 = today's behavior exactly, so nothing changes audibly until the count is
  lowered (regression safety — Phase 42 verified full 24-voice operation).

### Lowering the count while voices are sounding — DEFAULT (confirm if wrong)
- Held notes ring out naturally; lowering the count affects **future** allocation only
  (the round-robin modulus shrinks). A note already sounding on a now-out-of-range voice
  is not force-silenced at the instant of the change — it plays until its own note-off,
  or until round-robin reuse steals it on a later keypress.
- This is the recommended, least-surprising default. Immediate culling of out-of-range
  voices on a count decrease is the alternative if Anthony prefers it.

### Claude's Discretion (developer details — not user-facing)
- Storage/threading of the count: `allocateVoice` runs on the audio thread (processBlock
  MIDI dispatch); any setter is message-thread → must be realtime-safe (atomic or
  parameter). Implementer's call.
- Whether to re-base/clamp `nextVoice` on a count change vs. lazy `% count` at allocation.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Allocator (the core change lives here)
- `src/plugin/PluginProcessor.cpp:2593` — `allocateVoice(int note)`; round-robin cursor,
  `nextVoice = (nextVoice + 1) % 24` is the line to bound to the active count.
- `src/plugin/PluginProcessor.h:420` — `int8_t noteForVoice[24]` (note-per-voice, -1 = free)
  and `int nextVoice{0}` (round-robin cursor).
- `src/plugin/PluginProcessor.cpp:1414` — MIDI note-on/off dispatch: calls `allocateVoice`,
  then `spu94_voice_mixer_key_on` / `..._key_off`. The steal/takeover happens across these.
- `src/plugin/PluginProcessor.cpp:2603` — `findVoiceForNote(int note)` (note-off lookup;
  scans all 24 by note number — works regardless of active count).

### Voice mixer (key-on/off = the envelope restart + takeover)
- `include/spu94/spu94_voice.h`, `src/spu94/spu94_voice.c` — `spu94_voice_mixer_key_on` /
  `spu94_voice_mixer_key_off`, `spu94_voice_mixer_t`.

### Tests (pattern to follow for new allocation tests)
- `tests/unit/voice/test_voice_tick.c` — existing voice engine unit tests.

### Do NOT confuse
- `state->voice_counter` in `src/spu94/spu94_process.c` is the **pitch accumulator**
  (4.12 fixed-point), unrelated to voice allocation. Leave it alone.

</canonical_refs>

<specifics>
## Specific Ideas

- The change is small and localized: introduce the count + bound the round-robin modulus.
  The steal mechanism (key-off → immediate key-on on the reused voice) already exists and
  already produces the hard-restart takeover we want.
- Default count 24 MUST reproduce current behavior bit-for-bit (no regression vs. Phase 42).

</specifics>

<deferred>
## Deferred Ideas

- **Anti-click fade on steal** — evaluate by ear during testing; add a short (~1–2 ms) fade
  only if hard-cut clicks are objectionable. Untested sound-shaping → listening session.
- **VCOUNT-01 user-facing voice-count control** — Phase 62.

</deferred>

---

*Phase: 60-engine-voice-count-allocation*
*Context gathered: 2026-05-30 via inline design Q&A*
