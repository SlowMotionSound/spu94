---
status: completed
date: 2026-04-22
scope: whole-codebase
total_findings: 22
critical: 3
high: 6
medium: 7
low: 4
info: 2
---

# SPU-94 Whole-Codebase Code Review

## Executive Summary

Three themes dominate this review:

1. **Tests check that machinery runs, not that sound comes out.** The reverb-not-in-audio-path bug is the canonical example, but the same pattern repeats in at least four other places: the CLI test suite only inspects output WAV headers, not audio content; `test_process_block_size.c` and `test_process_in_place.c` "prove" properties of the public API while running against silent output (all their comparisons are zero-vs-zero); `self_test()` in the Python binding runs silent-input through the reverb and asserts only that the output dtype is still int16. A broken reverb would pass every one of these.

2. **Unacknowledged product gaps shipped as promises.** README claims `pip install spu94` — no such PyPI package exists. README points to a clone URL `https://github.com/anthonyaccurso/spu94` — that repository doesn't exist. `pyproject.toml` lists the same bad URL in three places. None of this is detected by `verify-readme-sections.sh`, which only greps for the presence of the string `pip install spu94`, not whether the package is actually publishable or present on PyPI.

3. **A known-suspicious DSP result is on file but not yet investigated.** Post-fix empirical listening against a real WAV file shows Hall output pinned at ~-5 dBFS regardless of input level (0, -12, -24 dBFS all produced the same output peak), followed by a cliff-drop into the tail. This is surfaced as a CRITICAL finding for a dedicated debug session — not fixed here.

The reverb-wiring fix itself is sound. ADR-Phase-6-G added an output-mailbox mechanism symmetrical to the existing input mailbox, three test sub-functions in `test_process_reverb_audible.c` now gate "Hall primes and flushes non-zero" and "Off gates to silence" end-to-end, and CLI now sets vLOUT/vROUT=0x7FFF for non-Off presets. But the repair uncovered several old tests that became vacuous under the new wiring (they still pass but no longer prove what they were named to prove), two stale code comments that still describe the pre-fix broken behavior, and a fuzz harness whose "preset loads -> non-zero output" invariant silently depends on a race between random vLOUT writes and the 256-call patience window.

## Critical Findings

### CR-01: Hall preset output level is clamped to ~-5 dBFS regardless of input level (feedback-loop self-oscillation suspected)

**File:** `src/spu94/spu94_reverb.c` (primary suspect: vIIR/vWALL feedback path, lines 174-310) + interaction with `src/cli/main.c:194-197` (vLOUT/vROUT default to 0x7FFF)
**Line(s):** system-level; not localizable to one function
**Category:** dsp-correctness
**Evidence:**

Post-fix empirical test on a real piano WAV:
- Input at 0 dBFS → output Hall primed amplitude ~-5 dBFS
- Input at -12 dBFS → output Hall primed amplitude ~-5 dBFS
- Input at -24 dBFS → output Hall primed amplitude ~-5 dBFS
- Same output peak across three input levels spanning 24 dB. A linear reverb would show 24 dB output range between these.
- Tail after input ends: abrupt cliff drop, not an exponential decay.

This signature — output pinned regardless of input level, with a cliff in the tail — is characteristic of either (a) a feedback loop that has reached self-oscillation and is being clipped at saturation, or (b) the vIIR=0x6000 (Hall) + vWALL=0xC000 coefficient combination producing a denormalized IIR state that dominates the output until input stops.

**Why it matters:** This is the next bug to fix — but it's NOT the same class as the wiring bug. The reverb IS audibly in the audio path now (user approved the three-commit fix), but the reverb is producing unnatural output regardless. A "bit-faithful to the PS1" claim requires the output level to track input linearly until it hits known saturation, not to flatline at ~-5 dBFS.

**Suggested fix:** Out of scope for this review per the objective. Recommended in a follow-up debug session: instrument `state->overflow_magnitude`, `state->err_same_iir`, `state->err_diff_iir`, `state->err_comb` per block during the test input. Compare the same-IIR / diff-IIR results against DuckStation or lv2-psx-reverb as behavioral witnesses (audio output only, per licensing posture). Check whether the feedback path `(acc - tap_prev) * vIIR + tap_prev` with vIIR=0x6000 and the ADR-0015 sat_s16 cascade is producing accumulating quantization that pushes the IIR into a limit-cycle attractor. Recommend also testing with DC input to isolate the IIR coefficient behavior from the APF chain.

---

### CR-02: Block-size-invariance test compares silence to silence and no longer proves what its name says

**File:** `tests/unit/process/test_process_block_size.c`
**Line(s):** 46-55 (`fresh_state` helper)
**Category:** test-vacuity / silent-correctness
**Evidence:**

```c
static spu94_state *fresh_state(void) {
    spu94_state *s = spu94_init(state_buf, sizeof state_buf,
                                work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(s);
    spu94_reset(s);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(s, SPU94_PRESET_HALL));
    spu94_tick(s);
    return s;
}
```

Hall preset's factory table leaves vLOUT = vROUT = 0x0000. Under ADR-Phase-6-G wet-only wiring, vLOUT=0 gates the 44.1 kHz output to zero. The test loads Hall but does NOT set vLOUT/vROUT to a non-zero value, so every `spu94_process` call in this test returns all-zero output regardless of input.

The test body then compares "block-size-1 output" against "block-size-N output" and asserts bit-identity — but both sides are identically zero. The test passes, but it no longer pins the D-03 block-size-invariance contract. A block-boundary bug that produced different outputs across sizes would pass this test because silence equals silence.

The sibling `test_process_in_place.c` has exactly the same defect at lines 42-51.

**Why it matters:** `docs/DECISIONS.md:460` still claims this test proves block-size invariance for Hall's preset across `{1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096}`. That claim is now false — the test proves `0 == 0` across those sizes. Public API-03 ("any block size N >= 1 is legal, block-size-invariant output") has no behavioral regression guard anymore.

**Suggested fix:** In both files, add `spu94_set_vLOUT(s, 0x7FFF); spu94_set_vROUT(s, 0x7FFF);` after the preset load (matching the pattern in `test_process_reverb_audible.c`). Re-run both tests to verify they still pass (they should — the underlying invariance property is likely still intact, it just isn't being exercised). Deferred fix decision: whether DECISIONS.md's claim needs to be toned down or the test hardened depends on fix path.

---

### CR-03: README.md promises `pip install spu94` but the package is not on PyPI, and the clone URL returns 404

**File:** `README.md:34`, `README.md:40`, `pyproject.toml:44-46`
**Line(s):** README.md lines 33-36 (install section), pyproject.toml lines 44-46 (project URLs)
**Category:** unbacked product claim
**Evidence:**

README line 33-36:
```
Install the wheel (Linux x86_64, glibc 2.28 or newer):

    pip install spu94
```

README line 40: `git clone https://github.com/anthonyaccurso/spu94`

pyproject.toml 44-46:
```toml
Homepage = "https://github.com/anthonyaccurso/spu94"
Source = "https://github.com/anthonyaccurso/spu94"
Issues = "https://github.com/anthonyaccurso/spu94/issues"
```

The user reports that `pip install spu94` fails (PyPI package doesn't exist) and that the GitHub URL 404s. `scripts/ci/verify-readme-sections.sh` line 62 greps for the literal string `pip install spu94` and marks README green — it never tries to resolve the package or the URL.

**Why it matters:** The README's "Quick install" section is the first thing any user reads. All three promised install paths — PyPI wheel, GitHub clone, and CMake from-source — are presented as working, but only the third is actually reachable. A first-time reader who follows the README will hit "ERROR: No matching distribution found" on step one and, if they try to fall back to step two, get a 404. This is the promise-without-delivery pattern the user is most sensitized to right now.

**Suggested fix:** Either (a) defer the PyPI publish to a later phase and rewrite the README's Quick Install section to name only the CMake-from-source path as available today, OR (b) publish the package + create the GitHub repo before the next release snapshot. Cannot be fixed in this review — requires an outside-the-repo action (PyPI credentials, GitHub account, manual publish). Also remove or replace the three URLs in pyproject.toml.

---

## High Findings

### HI-01: Test-premise-documents-its-own-gap pattern survives in test_reverb_body.c and test_process_flush.c

**File:** `tests/unit/reverb/test_reverb_body.c:149-150`, `tests/unit/process/test_process_flush.c:61-67`
**Line(s):** see below
**Category:** stale comment / out-of-date test
**Evidence:**

`test_reverb_body.c:149-150`:
```c
    int32_t LeftOutput = 0, RightOutput = 0;
    spu94_reverb_output_scale(B, Lout, Rout, vLOUT_snap, vROUT_snap,
                              &LeftOutput, &RightOutput);
    (void)LeftOutput;
    (void)RightOutput;
```

This is the exact `(void)` pattern that previously lived in spu94_reverb.c and caused the audio-path bug. The test computes the wet output via the stage-by-stage path in Path B, then discards it. Combined with the fact that Path A (which calls `spu94_reverb_body`) now publishes to `state->reverb_out_l/r`, the equivalence check no longer validates that Path A and Path B produce the same wet output — it only checks that work_buf + err_* accumulators match.

`test_process_flush.c:61-67`:
```c
 * doesn't work: Phase 4's FIR chain (src/spu94/spu94_io_chain.c) wires
 * the interpolator directly to the decimator output -- the reverb body's
 * LeftOutput/RightOutput values are (void)-cast at spu94_reverb.c:614-615
 * (not yet fed into the interpolator). So after a non-silent process
 * pass, the FIR decimator delay lines hold non-zero samples that continue
 * to emit through the interpolator during flush regardless of vLOUT.
```

This comment still describes the pre-ADR-Phase-6-G broken state as if it were current. The referenced line 614-615 no longer contains `(void)` casts. Any reader examining this file gets misinformation about the current wiring.

**Why it matters:** The `(void)` pattern in test_reverb_body.c is the same anti-pattern that caused the six-phase silent bug. If Path B's wet output diverged from Path A's wet output (e.g., because of a future refactor that changed reverb_output_scale's semantics), the test would not detect it. And the stale comment in test_process_flush.c actively tells the next code-reviewer the opposite of the current truth — that's exactly the kind of out-of-date documentation that lets bugs hide.

**Suggested fix:** In test_reverb_body.c, assert `A->reverb_out_l == (int16_t)LeftOutput` and `A->reverb_out_r == (int16_t)RightOutput` instead of `(void)`-casting them — this pins the mailbox-publishing contract at the unit level. In test_process_flush.c, rewrite the comment block at lines 53-71 to describe the CURRENT ADR-Phase-6-G wiring (the test's behavior is still correct; only the explanation is wrong).

---

### HI-02: CLI test suite never inspects output WAV content — same pattern that let the reverb-not-wired bug ship

**File:** `tests/cli/test_cli_preset_hall_roundtrip.py`, `tests/cli/test_cli_config_and_list.py`, `tests/cli/test_cli_error_paths.py`
**Line(s):** all assertions in those files
**Category:** test-vacuity / silent-correctness
**Evidence:**

Every CLI test asserts one or more of: exit code, stderr shape, output-file existence, WAV header (sample rate, channel count, frame count). None of them reads output sample data. Example:

```python
def test_every_preset_roundtrips(spu94_cli_path, sample_wav_file, tmp_wav_out, preset_name):
    result = subprocess.run(
        [spu94_cli_path, "--preset", preset_name, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, ...
    with wave.open(tmp_wav_out, "rb") as w:
        assert w.getnchannels() == 2
        assert w.getframerate() == 44100
```

The reverb-not-wired bug produced output WAVs with correct headers, correct channel count, correct frame count, correct format — and silent (or near-silent) audio content. Every test in this suite would have passed. And every test in this suite still passes even if the underlying DSP is completely broken, as long as the binary writes a WAV.

**Why it matters:** This is the process-level mechanism that let the core bug hide for six phases. The CLI is the user-facing surface. The CLI tests are what a user thinks of as "the regression net." They don't actually check audio content.

**Suggested fix:** Add one per-preset audibility smoke test analogous to `test_process_reverb_audible.c` but at the CLI level: run the binary with a generated deterministic-noise WAV input, read the output WAV samples, and assert at least some fraction of output samples are non-zero for non-Off presets and all-zero for Off. This can be a tiny addition (~30 LOC) to the existing roundtrip test file. Phase 7's golden-file regressions would make this fully rigorous, but a weak check here costs almost nothing and closes the specific class of bug the user just spent a day debugging.

---

### HI-03: `self_test` asserts only that int16 stays int16 — doesn't check that Hall produces any non-zero output

**File:** `python/spu94/api.py:392-426`
**Line(s):** 392-426 (`self_test` function)
**Category:** test-vacuity / product-claim-vs-test-coverage
**Evidence:**

```python
def self_test() -> None:
    state = init(work_buf_size=8192)
    try:
        rc = load_preset(state, "hall")
        if rc != SPU94_OK: raise RuntimeError(...)
        tick(state)
        n = 44100
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        process(state, None, None, L_out, R_out)
        assert L_out.dtype == np.int16
        assert R_out.dtype == np.int16
        flush(state, L_out, R_out)
    finally:
        destroy(state)
```

Process is called with `None, None` inputs — silence in. No assertion checks that output is (or isn't) silence. `L_out.dtype == np.int16` just asserts that numpy didn't spontaneously change the dtype, which it physically can't do for an ndpointer-fed buffer. This is effectively `assert True`.

`self_test` is the cibuildwheel test-command (see pyproject.toml:88: `test-command = 'python -c "import spu94; spu94.self_test()"'`). It is the wheel-acceptance gate. A wheel with a fully broken reverb would pass this gate.

**Why it matters:** `self_test` is what `README.md` line 407 references as the sanity check users can run to verify their install. It also gates every wheel produced by cibuildwheel. Under the reverb-not-wired bug, self_test would have returned "all good" because silence-in → silence-out is a true statement on a broken reverb just as much as on a working one.

**Suggested fix:** Add a non-silent-input arm to `self_test`: feed ~256 samples of Hall-friendly test signal (e.g., a short deterministic noise buffer) through process with vLOUT/vROUT set to 0x7FFF, then assert at least one output sample is non-zero. This turns `self_test` from "the machinery links" into "the reverb has an audio path."

---

### HI-04: `fuzz_process.py` non-zero-output invariant depends on an implicit race between random vLOUT writes and a 256-call patience window

**File:** `tests/python/fuzz_process.py:275-369`
**Line(s):** 305 (patience constant), 354-369 (invariant check)
**Category:** test-coverage / fragile-invariant
**Evidence:**

The fuzz sets up a "non-Off preset must produce non-zero output within 256 contiguous process calls" invariant, but:

1. `op_load_preset` loads a preset whose table values include vLOUT = vROUT = 0x0000 for every preset.
2. The fuzz never explicitly writes vLOUT/vROUT to 0x7FFF — the only way vLOUT becomes non-zero is via a random `op_write_i16_reg` choosing the vLOUT index (1 in 12 chance per I16-write op, which is 1 in 5 op classes).
3. On expectation, between a `op_load_preset` and a random vLOUT write, roughly 60 ops occur, of which ~12 are process calls. So the patience of 256 is comfortable — but the math is an unstated statistical assumption.

A future change that (a) shrank the patience window, (b) made `op_write_i16_reg` less frequent, or (c) biased the RNG away from vLOUT, would flip this invariant from "passes" to "fails" without any substantive DSP regression. Equivalently, the invariant passes TODAY because of an incidental random-choice ratio, not because the underlying product contract is tested.

**Why it matters:** Under ADR-Phase-6-G, "non-Off preset produces non-zero output" actually requires either the user or the test to set vLOUT/vROUT to non-zero. The fuzz doesn't acknowledge that — its invariant description still reads as if preset=Hall alone is sufficient. This is the class-of-bug the review is hunting for: a test whose name says "proves the reverb makes sound" when the actual check is "eventually, a random write is going to cause the reverb to make sound."

**Suggested fix:** After `op_load_preset` in the fuzz, explicitly set vLOUT=vROUT=0x7FFF if the loaded preset is non-Off. This makes the invariant deterministic and matches the CLI's production pattern. Alternatively, document the statistical dependency in the comment near line 305 so future maintainers don't tune the patience down blindly.

---

### HI-05: `spu94_cli_json_apply` "flat config must have all 35 registers" check doesn't detect duplicate keys

**File:** `src/cli/json_config.c:356-377`
**Line(s):** 358-365 (size check)
**Category:** input-validation gap
**Evidence:**

```c
        int pairs = tokens[0].size;
        if (pairs != (int)SPU94_REG__COUNT) {
            free(json);
            snprintf(err_buf, err_buf_size,
                     "flat config '%s' must specify all %d registers (found %d)",
                     path, (int)SPU94_REG__COUNT, pairs);
            return 1;
        }
```

`pairs` is the count of top-level key-value pairs jsmn tokenized. If the JSON contains `{"vIIR": 0, "vIIR": 123, ..., 34 other regs}` — a malformed flat config with a duplicate `vIIR` and one less DIFFERENT register — jsmn counts 35 pairs (duplicate counted), the size check passes, and `apply_one` is called for each pair. Result: the second `vIIR` silently overrides the first; the missing register is never detected. The resulting state has a zero in the "missing" register slot (inherited from the post-reset zero), which may be a legal value and go undetected until the user hears weird audio.

**Why it matters:** Silent acceptance of malformed input. A JSON author who typo-duplicates one key (e.g., pastes a register block twice without noticing) gets a working CLI with quietly wrong register values. The error message "must specify all 35 registers" is misleading — the issue is duplicate keys, not count.

**Suggested fix:** Build a set of seen register IDs while iterating pairs; on a duplicate, return an error naming the duplicated register. Alternatively, assert no register is written twice by tracking which register IDs were applied. Either changes the code by ~10 lines.

---

### HI-06: pyproject.toml project URLs point at a non-existent GitHub repo in three places

**File:** `pyproject.toml:44-46`
**Line(s):** 44-46
**Category:** unbacked product claim
**Evidence:**

```toml
[project.urls]
Homepage = "https://github.com/anthonyaccurso/spu94"
Source = "https://github.com/anthonyaccurso/spu94"
Issues = "https://github.com/anthonyaccurso/spu94/issues"
```

User reports this repo does not exist. If the package were ever published to PyPI, these URLs would appear on the PyPI project page and all three would 404.

**Why it matters:** This is the packaging-level mirror of CR-03. The README is the human-readable side; pyproject.toml is the machine-readable side that pypi.org would consume. Both must be fixed together.

**Suggested fix:** Either create the GitHub repo (outside-the-repo action) or replace the URLs with "coming soon" / omit them until the repo exists. Cannot be fixed inside this review.

---

## Medium Findings

### MED-01: README has no venv / PEP 668 externally-managed-environment warning

**File:** `README.md:29-53` (Quick install section)
**Line(s):** 29-53
**Category:** user-experience / install-path correctness
**Evidence:**

The Quick install section shows bare `pip install spu94` and `pip install -e .` with no mention of virtual environments. On Debian/Ubuntu 23.04+ and other PEP 668-compliant distros, bare pip against the system Python returns `error: externally-managed-environment` with a suggestion to use `--break-system-packages` or a venv. Anthony is on Linux (per the project README's "Linux wheel" language); the user is likely on a distro that enforces PEP 668.

**Why it matters:** A first-time user on modern Ubuntu who follows the README literally gets an error on step one. Even experienced users on non-PEP-668 distros are better off with a venv. This is a trivial polish item but the user flagged it and it's unambiguous.

**Suggested fix:** Prepend the install commands with `python -m venv spu94-env && source spu94-env/bin/activate` or similar. Alternatively add a short note: "On modern Debian/Ubuntu, first create a virtual environment: `python -m venv spu94-env && source spu94-env/bin/activate`."

---

### MED-02: `spu94_reverb_output_scale` int32 signature is misleading — the output is already int16-saturated before the widening cast

**File:** `src/spu94/spu94_reverb_internal.h:83-90`, `src/spu94/spu94_reverb.c:131-143`
**Line(s):** internal.h 83-90 (header comment + declaration), reverb.c 131-143 (implementation)
**Category:** misleading-doc / API design
**Evidence:**

Internal header comment:
```c
/* Output scale: LeftOutput = Lout * vLOUT (int32 product).
 * Bit-faithful endpoint — emits int32 so callers can observe overflow
 * before any downstream saturation. */
```

Implementation:
```c
int16_t L = q15_mul_truncate_with_err(Lout, vLOUT_snap, &err_l);
int16_t R = q15_mul_truncate_with_err(Rout, vROUT_snap, &err_r);
...
*LeftOutput_out  = (int32_t)L;
*RightOutput_out = (int32_t)R;
```

`q15_mul_truncate_with_err` returns an int16 that has already been saturated via `sat_s16`. Then the function widens to int32 and stores that. There is NO overflow to observe — it was consumed inside the multiply helper. The header comment claims the opposite. Consumers of LeftOutput/RightOutput who trust the comment are misled about what the int32 carries.

The Phase 6 fix's `state->reverb_out_l = (int16_t)LeftOutput` at spu94_reverb.c:619 is correct — the back-cast is always safe because LeftOutput is already in int16 range. But the reason it's safe (and the reason the int32 widening was purposeless in the first place) deserves to be documented.

**Why it matters:** This is the class of doc drift that has caused the worst bugs in this codebase. The internal header should match the implementation. Either the output should actually be int32 and saturate later (currently false), or the function should return int16 and the internal header should say so.

**Suggested fix:** Either (a) change the signature to `int16_t *LeftOutput_out, int16_t *RightOutput_out` and remove the back-cast at reverb.c:619, or (b) update the internal header comment to say "returns int16 range carried in int32 slot for a historical reason that's now obsolete; back-cast is safe." Option (a) is cleaner.

---

### MED-03: `spu94_cli_preset_canonical_name` uses an unsynchronized module-local static cache

**File:** `src/cli/preset_names.c:55-72`
**Line(s):** 55-72
**Category:** thread-safety / style
**Evidence:**

```c
const char *spu94_cli_preset_canonical_name(int preset_id) {
    static char cache[SPU94_PRESET__COUNT][64];
    static int  initialized = 0;
    if (!initialized) {
        for (int i = 0; i < (int)SPU94_PRESET__COUNT; ++i) {
            const char *display = spu94_presets[i].name;
            normalize_name(display, cache[i], sizeof cache[i]);
        }
        initialized = 1;
    }
    ...
}
```

Two concurrent callers on the first invocation could both see `initialized == 0`, both start writing to `cache[]`, and race on `initialized = 1`. The CLI is single-threaded and the CLI binary never calls this concurrently, so the bug is latent. But the function is in a `.c` that compiles into a library target (CLI binary), and any future use in a multithreaded context (e.g., Phase-7's multi-preset golden-file generator if it parallelizes) would trigger it.

**Why it matters:** Concurrent-writer race with UB potential. Unlikely to fire in practice today, but also unnecessary — the cache could be eliminated entirely with negligible performance cost (the function is called on every error and every `--list-presets`, neither of which is hot-path).

**Suggested fix:** Either (a) remove the cache and re-normalize per call (simplest, and the cost is 10 strcmp's; not measurable on a CLI startup path), or (b) document single-threaded-only usage in the header declaration, or (c) use a pthread_once-style initialization if portability permits.

---

### MED-04: `spu94_cli_wav_load` doesn't defend against a maliciously-sized WAV causing `interleaved_count * sizeof(int16_t)` to overflow size_t on 32-bit platforms

**File:** `src/cli/wav_io.c:80-82`
**Line(s):** 80-82
**Category:** memory-safety / integer-overflow
**Evidence:**

```c
    size_t interleaved_count = (size_t)wav->num_frames * wav->num_channels;
    int16_t *interleaved = (int16_t *)malloc(interleaved_count * sizeof(int16_t));
```

`wav->num_frames` is uint64 (returned by dr_wav's `totalPCMFrameCount`). `wav->num_channels` is u32 (enforced to 2). On a 32-bit system with size_t == uint32, `num_frames * num_channels` could overflow if the WAV claims > 2^31 frames (not uncommon for corrupted or intentionally malformed headers). The subsequent `* sizeof(int16_t)` multiplication can overflow again. The malloc then gets a smaller buffer than needed and the `drwav_read_pcm_frames_s16` write over-runs it.

Same concern applies to `wav->L` / `wav->R` mallocs at lines 100-101, and to the `L_out` / `R_out` mallocs at `src/cli/main.c:218-219` where `total_out * sizeof(int16_t)` could overflow on 32-bit.

**Why it matters:** The CLI is built into a 64-bit binary on the user's current workstation (x86_64) where size_t is 64-bit and the overflow is functionally impossible for any realistic WAV. But the library promises MCU portability (arm-none-eabi-gcc, 32-bit), and the wav_io.c file is excluded from libspu94.so (CLI-only, confirmed by `verify-no-drwav-in-libspu94.sh`) so it doesn't technically apply to the library. However, "the CLI never builds on 32-bit in practice" is an unstated assumption worth flagging. Also a defensive bound would protect against intentionally malformed WAV inputs.

**Suggested fix:** Pre-check `wav->num_frames` against a project-defined maximum (e.g., 2^30 frames = ~6.7 hours of 44.1 kHz stereo, covering any realistic audio) and reject with a clear error before calling malloc. Same bound should apply to total_out in main.c.

---

### MED-05: `total_out * sizeof(int16_t)` overflow risk in CLI output buffer allocation

**File:** `src/cli/main.c:218-219`
**Line(s):** 218-219
**Category:** memory-safety / integer-overflow
**Evidence:**

```c
    uint64_t total_out = input.num_frames + tail_frames;
    int16_t *L_out = (int16_t *)malloc((size_t)total_out * sizeof(int16_t));
    int16_t *R_out = (int16_t *)malloc((size_t)total_out * sizeof(int16_t));
```

Additional risk: even on 64-bit, a very large `total_out` × `sizeof(int16_t)` × 2 buffers = large committed memory. No pre-check for "this is a reasonable amount of memory to request."

**Why it matters:** A maliciously-crafted large WAV header or a huge `--tail-seconds` value could trigger an OOM. `malloc` returning NULL is handled (lines 220-226), so this is bounded behavior — but the error message says "out of memory" when it's really "user asked for too much."

**Suggested fix:** Bound `total_out` with a sanity check (e.g., `< 2^30` frames = ~4GB of int16 pairs) and emit a clearer error message if the request is unreasonable. Low priority; mostly a polish item.

---

### MED-06: `spu94_presets.c` discards return values from `spu94_set_reg_i16` and `spu94_set_reg_u16`

**File:** `src/spu94/spu94_presets.c:471-479`
**Line(s):** 471-480
**Category:** error-path completeness
**Evidence:**

```c
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            (void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
        } else {
            (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
        }
```

The `(void)` cast says "I know this returns a value and I'm ignoring it." The comment above says the call cannot fail because the signedness is matched by construction. That's true as long as the preset table is correct. But:

1. Static analysis tools flag `(void)` of return values as suspicious.
2. If a future edit introduces a bug in `spu94_reg_type` or in the preset table (wrong signedness family on one register), `spu94_load_preset` would silently skip the register. No error returned. No test detects it (unless the test reads the register post-load).
3. The `test_preset_load_all.c` test DOES read each register post-load and assert the value, so in practice this class of bug would be caught — but only if the test runs. If someone ships a preset without running the test, the preset would silently misload one register.

**Why it matters:** Same "silent acceptance of a corrupted internal table" class as HI-05 but at a different layer. The comment at lines 472-473 argues why it's OK to ignore the return, but that argument depends on external invariants that a runtime `assert` would enforce for free.

**Suggested fix:** Either replace `(void)` with `assert(spu94_set_reg_*(...) == SPU94_OK)` (fires in debug builds, costs nothing in release), or aggregate the results and return an error code if any write failed. The latter preserves the "atomic" contract documented in the header.

---

### MED-07: mix_bus downsampling from 44.1 kHz to 22.05 kHz has no antialiasing filter

**File:** `src/spu94/spu94_process.c:40-43`, `src/spu94/spu94_reverb.c:579-580`
**Line(s):** process.c 40-43, reverb.c 579-580
**Category:** dsp-correctness (design concern, not bug)
**Evidence:**

`spu94_process` writes `state->mix_bus_l = l; state->mix_bus_r = r;` on every 44.1 kHz sample (line 40-41 of process.c). The reverb body reads those fields once per 22.05 kHz tick (reverb.c line 579-580). Because spu94_tick only fires on the retained decimator phase (every other 44.1 kHz call), the reverb body sees every OTHER 44.1 kHz input sample — a 50% cadence drop with no antialiasing.

The decimator output `(dec_l, dec_r)` IS antialiased (it's the output of the 39-tap half-band FIR). But the mix-bus path bypasses the decimator. The PS1 hardware, by contrast, feeds vLIN*LeftInput into the reverb network AFTER the decimator.

I don't have conclusive evidence whether this is (a) hardware-accurate (the PS1 docs I'm allowed to cite may not distinguish between the decimator output and the raw 44.1 kHz input feed on this specific path) or (b) a Phase 5 design shortcut that diverges from the hardware. ADR-0015/16 and the 04-RESEARCH / 05-RESEARCH documents would be the authoritative place to check.

**Why it matters:** If the hardware feeds decimator output into vLIN/vRIN (not raw input), SPU-94 is producing a subtly different reverb character because the IIR network sees aliased input. This would manifest as unexpected high-frequency energy in the reverb tail — exactly the kind of thing the user would hear and call "wrong."

**Suggested fix:** Verify against the research docs whether the mix-bus input should be the decimator output or the raw 44.1 kHz sample. If the former, change `spu94_process` to populate `mix_bus_l/r` with `dec_l/dec_r` inside `chain_step_impl` on the retained phase, not with `l/r` outside. Deferred to a dedicated research session before touching.

---

## Low Findings

### LO-01: `test_reverb_body.c` work_buf post-condition byte-compare doesn't verify the new `reverb_out_l/r` mailbox fields match between Path A and Path B

**File:** `tests/unit/reverb/test_reverb_body.c:152-165`
**Line(s):** 152-165
**Category:** test-coverage
**Evidence:**

After the Phase 6 fix added `state->reverb_out_l/r` fields, `spu94_reverb_body` now writes to these. Path A calls `spu94_reverb_body(A)` so A->reverb_out_l/r get set. Path B does NOT call spu94_reverb_body but instead runs the same sequence manually using B as scratch — it does not write to B->reverb_out_l/r. The test's byte-equality checks (lines 152-165) don't compare these two fields, so the divergence is invisible.

**Why it matters:** The composition-equivalence test is supposed to catch "Path A and Path B produce different outputs." After the wet-output mailbox was added, the test is silently blind to that specific divergence. Same HI-01 pattern, different symptom.

**Suggested fix:** Assert `A->reverb_out_l == (int16_t)LeftOutput && A->reverb_out_r == (int16_t)RightOutput` in Path B (after the manual stage sequence). Closes the gap.

---

### LO-02: Drift assertion in `python/spu94/__init__.py:42-49` only checks upper bound, not an exact size match

**File:** `python/spu94/__init__.py:42-49`
**Line(s):** 42-49
**Category:** defense-in-depth
**Evidence:**

```python
_state_size = _lib.spu94_state_size()
if _state_size > SPU94_STATE_SIZE_MAX:
    raise RuntimeError(...)
```

The check fires only when sizeof(spu94_state) grew PAST the macro. If the live library reports `state_size = 512` and the Python constant is `SPU94_STATE_SIZE_MAX = 16384`, the check passes. That's fine as long as the binding allocates a buffer of `SPU94_STATE_SIZE_MAX` bytes (which it does, in api.py line 82). But the fuzz_process.py script (line 181-190) does a stricter `_actual_state_size` check to defend byte-offset peeks.

**Why it matters:** Low risk because the binding overallocates to the macro value (which covers any smaller actual struct). But it means a library whose struct has been radically restructured (e.g., Phase 7 adds 100 new fields summing to 15.9KB) would silently pass the drift check as long as the total still fits the macro, even if field layouts have shifted in ways that break fuzz_process's byte-offset assumptions.

**Suggested fix:** Either (a) leave as-is — the fuzz scripts have their own checks — or (b) add the exact-size check in fuzz_process's style to __init__.py too. Optional.

---

### LO-03: `ROADMAP.md` Phase 7 and Phase 8 plan sections are copy-pasted from Phase 3

**File:** `.planning/ROADMAP.md:141-160`
**Line(s):** 141-160
**Category:** documentation drift
**Evidence:**

Lines 141-160 list "Plans: 03-01-PLAN.md ... 03-02-PLAN.md ..." under Phase 7 and Phase 8 where the actual plans are Phase 7 and Phase 8 plans (not yet written). The lines are verbatim copies of Phase 3 Plans content. A reader scanning the roadmap gets misleading information about Phase 7 and 8 scope.

**Why it matters:** Roadmap-quality issue only. Does not affect code correctness. But the user is sensitized to "docs claim A, reality is B" patterns right now.

**Suggested fix:** Either blank the plan lists under Phase 7 and 8, or replace them with placeholder text like "Plans TBD." Cosmetic.

---

### LO-04: Several test assertions use `TEST_PASS()` to mean "no crash" without ever actually asserting a behavior

**File:** `tests/unit/process/test_process_basic.c:42-52, 117-127`
**Line(s):** 42-52, 117-127
**Category:** test-quality
**Evidence:**

```c
static void test_process_null_state_noop(void) {
    ...
    spu94_process(NULL, Lin, Rin, Lout, Rout, 4);  /* no crash == pass */
    TEST_PASS();
}
```

```c
static void test_process_inplace_no_crash(void) {
    ...
    spu94_process(state, buf_l, buf_r, buf_l, buf_r, 128);
    TEST_PASS();  /* No crash / no UBSan trip == pass. */
}
```

`TEST_PASS()` is a Unity macro that unconditionally marks the test as passed. These tests rely on side-channel signals (SIGSEGV would abort the process; UBSan would trap). If future code changes cause incorrect-but-non-crashing output for in-place processing, the test would pass.

**Why it matters:** Weak-form regression gates. The in-place contract is bit-identity (proven elsewhere by `test_process_in_place.c`, which has the CR-02 vacuity issue); the in-place test here is really a "didn't crash" smoke test. Worth acknowledging.

**Suggested fix:** Optional — these are fine as smoke tests, but consider strengthening `test_process_inplace_no_crash` to assert the output matches an out-of-place reference. (And first fix CR-02 so the reference isn't silent.)

---

## Informational

### INFO-01: Header comment in `spu94_io_chain.c` still describes Phase 4 as "reads zero mix-bus inputs"

**File:** `src/spu94/spu94_io_chain.c:27-30, 42`
**Line(s):** 27-30 (Phase-5 stitching note), 42 (reverb_body reads zero...)
**Category:** stale comment
**Evidence:**

Line 27-30: `Phase-5 stitching note: spu94_tick runs the Phase-3 reverb body with the current register state. The reverb body reads zero mix-bus inputs in Phase 4 -- Phase 5 will wire vLIN/vRIN into the mix-bus path so the FIR decimator outputs feed the reverb indirectly.`

Line 42: `reverb_body reads zero mix-bus inputs (Phase 3 Plan 01 placeholder).`

Phase 5 is done; mix-bus inputs are now wired via `state->mix_bus_l/r` in `spu94_process`. These comments describe pre-Phase-5 reality.

**Why it matters:** Documentation drift. Same as HI-01 on test_process_flush.c but in the production source. Reader-misleading but functionally correct.

**Suggested fix:** Update comments to describe the current wiring.

---

### INFO-02: `spu94_destroy(state)` + subsequent accessor calls return a weird state

**File:** `src/spu94/spu94_state.c:123-131` + any get_* accessor
**Line(s):** state.c 123-131
**Category:** API-design
**Evidence:**

`spu94_destroy` calls `spu94_zero_bytes(state, sizeof(*state))` which zeroes the WHOLE struct including work_buf, work_buf_size, etc. After destroy, `spu94_get_buffer_address(state)` returns 0 (from zeroed storage). But `state` is still a valid pointer and the accessor doesn't detect the destroyed state. Any get_*/set_* call after destroy "works" but returns/writes nonsense.

The Python wrapper (`SPU94` class) handles this by guarding on `self._state is None` after destroy. The C API has no such protection.

**Why it matters:** The C API contract in `include/spu94/spu94.h` line 146-148 says "After spu94_destroy, the state pointer is invalid until the caller re-runs spu94_init." A caller who violates this sees silent garbage, not an error. This is documented behavior, so it's INFO not a bug, but it's worth knowing.

**Suggested fix:** None required — the contract is documented. A future hardening could add a magic-number sentinel to the struct and have destroy clear it, with accessors checking the sentinel, but that's a minor defensive addition.

---

## Process-Level Observations

The reverb-not-wired bug and every finding above that's severity HI or above share a root cause: **tests verify that machinery runs, not that the user's product claim holds**.

- `test_preset_nonzero_tail.c` header (pre-fix) documented the gap as "out of M1 scope."
- `test_reverb_body.c` computed wet output in Path B and `(void)`-cast it.
- `test_process_block_size.c` and `test_process_in_place.c` ran Hall on fresh state without setting vLOUT/vROUT, comparing silence to silence.
- `self_test()` ran silent-input through the reverb and asserted int16 stayed int16.
- The CLI test suite read output WAV headers and never samples.
- `fuzz_process.py` had a "non-zero output after preset load" invariant that depended on a race with random vLOUT writes to reset its patience counter.

Each of these individually looks like a minor testing shortcut. Collectively, they are a system where "the reverb actually reverbs" has no test. Phase 7 is where golden-file regressions land; until Phase 7 happens, **the only test in the repo that verifies Hall produces audibly-different output from Off is `tests/unit/process/test_process_reverb_audible.c`, added two commits ago as part of the fix.**

A concrete recommendation: establish a convention, documented in a short markdown file under `.planning/` or in the test layer's README, that says "every public API claim has a behavioral test: feed non-silent input, assert output is non-zero / specific / different from silence / different from dry." This is orthogonal to bit-exactness tests (which have their place). The gap that shipped the bug is the gap between "the C function returned" and "the user hears what the README promises."

A second-order recommendation: when CLAUDE or an agent completes a phase and writes a PHASE-SUMMARY.md claiming "Phase N complete," consider adding a review pass that specifically asks: "for each claim in this summary, is there an end-to-end test that would fail if that claim became false?" This is a process gate, not a code gate, and it's cheap.

## Known Issues NOT covered here

- **Witness-diff audit vs lv2-psx-reverb / DuckStation / Mednafen.** Out of v1 scope (Phase 7 work). The current DSP correctness question CR-01 (-5 dBFS output ceiling) is exactly the kind of thing a witness-diff would have caught; prioritize Phase 7 scaffolding once the debug session resolves CR-01.

- **Research and ADR drift review.** I didn't audit `docs/DECISIONS.md` or `.planning/research/*.md` for claims that have drifted out of sync with code. The code review is source-focused; prose correctness in research docs is a separate audit.

- **Build-system review.** CMakeLists.txt files and the `scripts/ci/*` shell scripts were only spot-read. A full audit of CI flag propagation, manylinux constraints, and RPATH handling is out of v1 scope.

- **Full cross-reference of docs/BIBLIOGRAPHY.md source citations against the paraphrased claims in code comments.** Licensing posture relevant; deferred.

- **Performance profile of spu94_process at 44.1 kHz block sizes.** RT-safety tests exist (`tests/rt_safety/`), but I didn't check whether the benchmark harness's numbers are meaningful or whether the "no allocations / no syscalls" guarantees hold under block-size-1 stress.

- **Static analysis output review.** `.clang-tidy` exists in the repo root; its current output and any suppressions were not audited.

- **The Phase 7-and-8 test sections referenced in ROADMAP.md.** Those phases are not implemented yet; reviewing them is premature.
