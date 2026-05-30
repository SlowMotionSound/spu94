# To Do

Items that need to get done at some point. They live here instead of cluttering milestones and phase tracking — pick from here when looking for what to work on next.

## Outstanding

| Item | Origin | Added |
|------|--------|-------|
| Organize Noise section in sampler panel | v1.10.0 UAT | 2026-05-28 |
| Musical divisions for Speed encoder (note values, BPM sync) | v1.10.0 UAT | 2026-05-28 |
| Sidechain Duck UAT (needs MIDI controller) | v1.10.0 UAT | 2026-05-28 |
| Preset extension UAT — save/load round-trip verification | v1.10.0 UAT | 2026-05-28 |
| Hide preset Save/Load in plugin formats | v1.6 | 2026-05-11 |
| 2 packaging tests time out (`test_packaging_editable_install`, `test_packaging_wheel_tag`) — pip/wheel build; likely environmental, verify | cleanup test run | 2026-05-30 |
| Tier 4 dup-code optimization (deferred) — fold the 4 VCA-effect blocks (tremolo/auto-pan/ring-mod/AM) into one shared helper, ~460→120 ln; also #37 resample + #38 GUI dedup. Needs a dedicated **critical-listening session** (sound code, no test guard). See CODEBASE-AUDIT.md Tier 4. | post-v1.11.0 cleanup audit | 2026-05-30 |

## Ideas

| Item | Origin | Added |
|------|--------|-------|
| ADPCM filter pair LED indicators | v1.1 | 2026-04-29 |
| Codec re-sync effect | v1.2 | 2026-05-01 |
| Real-time room geometry visualizer | v1.3 | 2026-05-03 |
| "Bit Corrupt" mode — re-enable register-shadow overflow as opt-in toggle | v1.6 | 2026-05-11 |
| Pitch quantizer on mod bus — quantize LFSR noise to musical intervals | v1.10.0 | 2026-05-25 |
| Raw LFSR CV output — expose noise generator as patchable Eurorack output | v1.10.0 | 2026-05-25 |

## Done

| Item | Completed |
|------|-----------|
| plug15 null-test self-recursive `aligned_alloc` fix (hung the suite in Release; broken since 1c6dfa7 / 2026-05-14) | 2026-05-30 |
| `adsr_unit` sustain-zero test updated to expect true silence (level 0, voice OFF) — matches the reach-zero behavior; engine already did it, the test was stale | 2026-05-30 |
| Removed the stale `init` preset (the 11th) — C core back to the 10 Sony presets, now consistent with the Python side; both CLI tests green; morph dial unaffected | 2026-05-30 |
