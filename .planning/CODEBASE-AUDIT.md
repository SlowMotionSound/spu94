# Codebase Cleanup Audit — Master Verification & Checklist

**Created:** 2026-05-29
**Status:** Audit complete, verified. No fixes applied yet.
**Scope:** Full-codebase cleanup pass — dead code, unused/stale code, dead paths, duplicate code, inefficiencies. Framework: `~/.claude/agents/Code Optimization Agent.md` (measure-first, behavior-preserving).

## How this was produced (two-pass blind audit)

1. **Pass 1 (Opus 4.6):** Three parallel agents audited C core, JUCE plugin, CLI/Python/tests. Produced a 29-item tiered list.
2. **Pass 2 (Opus 4.8):** Three fresh agents re-audited the same domains **blind** (no access to pass-1 findings), for an independent second opinion.
3. **Reconciliation (manual grep verification):** Every disagreement between the two passes was settled against the actual code. `✓` marks items confirmed by direct grep in this session.

**Key lesson:** neither pass alone was complete or correct. 4.8 found 9 verified items 4.6 missed; 4.6 caught one item (`checkModified`) that 4.8 wrongly declared live. Blind-pass + verify was decisively better than either single pass.

## Disagreements resolved by ground truth (do not re-litigate)

| Dispute | 4.6 | 4.8 | Verified truth |
|---------|-----|-----|----------------|
| C core stale includes | "none" | 2 in `spu94_process.c` | **4.8 right** — `string.h` + `spu94_spu_ram.h` both unused |
| JUCE `checkModified()` | dead | live | **4.6 right** — zero call sites; feature half-wired (baseline captured, never compared) |
| `spu94_zero_bytes` | remove | keep | **Both right on facts** — it IS called, it IS a hand-rolled `memset`. Judgment call |
| 3 JUCE `.h` includes | move to `.cpp` | fine | **4.6 finding valid** — symbols confirmed absent from `.h` body |

## Constraints for execution

- **DSP math is untouchable.** No changes to truncation, fixed-point, or reverb/voice algorithms. Cleanup only.
- **One change per build+verify cycle.** Build Release, verify, commit, then next item. No batching (esp. GUI).
- **Dead C functions in Tier 2 (#6-11) are public API.** Removing them is an ABI change — batch on the next intentional ABI bump, not piecemeal.
- **Do not remove protective code** (limiters/guards) even if it looks unused — flag and ask first.
- The CLI is **not** cuttable: `scripts/regenerate_goldens.py` and `scripts/ci/witness_diff.py` drive the CLI binary to build/validate the golden corpus.

---

## Tier 1 — Dead weight to delete (whole files / orphans)

- [x] 1. Delete `tests/golden_v1.2/` — 110 orphaned files (~19 MB), referenced by no test [Both ✓]
- [x] 2. Delete `src/plugin/AdsrDisplay.h` — 154-line component, never included/instantiated [Both ✓]
- [x] 3. Delete `main.sh` — 71-byte scaffold stub from project init [4.6 ✓]
- [x] 4. Delete `tests/.gitkeep` — vestigial; dir has 100+ tracked files [4.6 ✓]
- [x] 5. Delete orphan `tests/cli/__pycache__/*.pyc` (incl. `test_cli_tempo`, source only on archived branch) [4.6 ✓]

## Tier 2 — Dead code compiled into binaries

### C core (ABI change — batch on next ABI bump)
- [x] 6. Remove `spu94_dac_noise_step_8x` — `spu94_dac_noise.c:89` + header [Both ✓]
- [x] 7. Remove `spu94_get_gauss_enabled` — `spu94_io_chain.c:189` + `spu94.h:235` [4.8 ✓]
- [x] 8. Remove `spu94_get_voice_pitch` — `spu94_io_chain.c:201` + `spu94.h:243` [4.8 ✓]
- [x] 9. Remove `spu94_get_aa_filter_enabled` — `spu94_io_chain.c:211` + `spu94.h:250` [4.8 ✓]
- [x] 10. Remove `spu94_voice_mixer_set_noise_freq` — `spu94_voice.c:544` + `spu94_voice.h:193` [4.8 ✓]
- [x] 11. Remove `spu94_slew_cancel` — `spu94_slew.c:75` + `spu94.h:655` [4.8 ✓]

### JUCE plugin / standalone
- [x] 12. Remove `checkModified()` + `PresetSnapshot`/`baseline`/`modifiedState` cluster — `PluginEditor.cpp:1933` + `PluginEditor.h:237`. (Half-wired feature: baseline captured, never compared. Alternative: finish it by wiring a title-bar modified indicator.) [4.6 ✓]
- [x] 13. Remove `adpcmEnabled` atomic + `getAdpcmEnabled()` — `PluginProcessor.h:174,325` (replaced by implicit fader-level activation) [4.6 ✓]
- [x] 14. Remove `kInputGainDefault` / `kInputGainMax` consts — `PluginProcessor.h:315-316` (hardcoded inline instead) [4.6 ✓]
- [x] 15. Remove `getGuiVoiceVolL()` / `getGuiVoiceVolR()` reference getters — `PluginProcessor.h:209-210` (setters are the live path) [Both ✓]
- [x] 16. Remove `SrcChain::reset()` — `SrcChain.h:53` (never called) [4.6 ✓]
- [x] 17. Remove `getSrcCallbacksThisBlock()` + `srcCallbacksThisBlock_` counter — `SrcChain.h:73` (dev debug accessor; keep `resetSrcCallbacksCounter()` which IS called) [Both ✓]
- [x] 18. Remove `BoundaryConverter::saturationSelfCheck()` — `BoundaryConverter.h:97` (already `[[maybe_unused]]`; low priority) [Both ✓]
- [x] 19. Remove `WaveformDisplay::clear()`, `getTotalFrames()`, `getLoopMode()` — `WaveformDisplay.h` [4.6 ✓]
- [x] 20. Remove `WaveformDisplay::sRate` (write-only) + `onZoomChange` callback (never assigned, always null) — `WaveformDisplay.h:288,72` [4.6 ✓]
- [x] 21. Remove `LoadedWav::originalSampleRate`/`originalNumChannels`/`originalBitsPerSample` (write-only) — `WavLoader.h:14-16` + assignments `WavLoader.cpp:88-90`. (Alternative: surface in UI later.) [Both ✓]

## Tier 3 — Code hygiene

- [ ] 22. Delete 2 stale includes `<string.h>` + `<spu94/spu94_spu_ram.h>` — `spu94_process.c:38,33` [4.8 ✓]
- [ ] 23. Move 3 includes used only in `.cpp` out of header — `PluginProcessor.h:6` (StateSerializer.h), `:16` (spu94_voice.h), `:17` (spu94_sample_loader.h) [4.6 ✓]
- [ ] 24. (Judgment) Replace `spu94_zero_bytes` with `memset`, drop the private fn — `spu94_state.c:49-57`. It IS called; this is consolidation, not dead-code removal [4.6 ✓]
- [ ] 25. Make `spu94_cli_preset_canonical_name` `static`, drop header decl — `preset_names.c:55` + `preset_names.h:29` (used internally only) [4.8 ✓]
- [ ] 26. Remove ~17 stale Python imports across 12 files (`api.py`, `fuzz_process.py`, `test_modulation_harness.py`, `modulation_harness.py`, `binding/conftest.py`, `test_binding_adpcm.py`, `test_binding_numpy_contract.py`, `test_cli_adpcm.py`, `test_cli_preset_hall_roundtrip.py`, `scripts/ci/test_check_coverage.py`) [Both ✓]
- [ ] 27. Remove dead test helper `run_cli()` — `tests/cli/conftest.py:56-63` [Both ✓]
- [ ] 28. Fix `reverb.py` `_reg_type` annotation `-> int` → `-> Tuple[int,int]` (returns a tuple) — `reverb.py:137` [4.8]
- [ ] 29. Update ~6 stale "Phase N will…" comments describing already-shipped code — `spu94_process.c`(io comments), `spu94_reverb.c:202-214` (stub header), `spu94.h:163`, `spu94_registers.c:9`, `spu94_write_policy.c:18-22`, `spu94_io_chain.c:27-45` [Both ✓]
- [ ] 30. Delete orphaned Phase-41 comment header (no members under it) — `PluginProcessor.h:463` [4.8]
- [ ] 31. Renumber param comments to close the "7" gap (cosmetic — do NOT touch `addParameter` call order) — `PluginProcessor.cpp:256-266` [4.8]
- [ ] 32. Rewrite/remove "DIAGNOSTIC" investigative comment + duplicate comment line — `PluginProcessor.cpp:555` and `:2391-2392` [4.6]

## Tier 4 — Duplicate code (needs its own focused pass + test guard)

- [ ] 33. Consolidate four VCA-effect activation blocks into one helper — ~460 ln → ~120 — `PluginProcessor.cpp:900-1359` [Both ✓]
- [ ] 34. Extract `ratioToRShift()` log2 block (4× copies) — within #33 [Both ✓]
- [ ] 35. Extract shape-clamp (8× copies) — within #33 [4.6]
- [ ] 36. Extract depth→Q15 + `sweep_depth` write (4× copies) — within #33 [4.6]
- [ ] 37. Extract resample-to-target-rate block (2× copies) — `encodeRecordedSample()` + `loadVoiceSample()` [4.6]
- [ ] 38. Factor recording-state enable/disable for 5 controls (2× copies) — `PluginEditor.cpp:1369-1401` [4.6]
- _Informational, NOT actionable:_ io_chain fader/send accessors (8 pairs, `spu94_io_chain.c:244-314`) look duplicated but are required distinct ABI symbols. Leave as-is. [4.8]

## ⚠ Separate from cleanup — real test gap (highest value)

- [ ] 39. **Wire `test_cli_mixer_dac.py` into `tests/cli/CMakeLists.txt` `_cli_tests` and RUN it.** 12 tests unregistered → never executed since 2026-05-21. Guards the `--dac`/`--adpcm`/`--preset` CLI flags that `regenerate_goldens.py` + `witness_diff.py` depend on. May surface real failures. [4.8 ✓]

---

## Recommended execution order

1. **#39 first** — only item that can expose a hidden bug. Wire + run before changing anything else.
2. **Tier 1** — pure deletions, ~285 lines + 19 MB, near-zero risk.
3. **Tier 3** (excl. #24 judgment call) — mechanical hygiene.
4. **Tier 2 C core (#6-11)** — bundle as one ABI-bump commit.
5. **Tier 2 JUCE (#12-21)** — one change per build+verify cycle.
6. **Tier 4** — last; dedicated pass with the existing VCA-effect tests as a regression guard.

## Totals

38 cleanup items + 1 test gap. All 29 of pass-1's items hold up; pass-2 added 9 verified net-new and corrected 1 false negative.
