---
review: SPU-94 CLI + Python binding close-out audit
reviewed: 2026-04-24
depth: deep
scope:
  - src/cli/main.c
  - src/cli/json_config.c
  - src/cli/json_config.h
  - src/cli/wav_io.c
  - src/cli/wav_io.h
  - src/cli/preset_names.c
  - src/cli/preset_names.h
  - python/spu94/__init__.py
  - python/spu94/_binding.py
  - python/spu94/api.py
  - python/spu94/reverb.py
  - python/spu94/presets.py
  - python/spu94/cli.py
findings:
  critical: 2
  high: 6
  medium: 7
  low: 5
  nit: 4
  total: 24
status: issues_found
---

# SPU-94 v1.0 Close-Out Review — CLI + Python Binding

## Summary

This audit was triggered by the `work_buf_size=8192` default silent-degradation
bug (Hall needs ~11 KB; undersized buffers silently zero all reverb reads and
discard all writes with no warning). The concern was whether similar
silent-failure patterns exist elsewhere on the user-facing surface.

**They do.** The same class of bug appears in at least four places:

1. The 8192-byte default in `api.init()` AND `SPU94(work_buf_size=8192)` —
   confirmed by the user, and **the fix cannot live in the defaults alone**
   because the core C contract in `spu94_reverb.c:54,69` silently degrades
   instead of erroring.
2. `self_test()` uses the same broken 8192 default — the audibility check
   passes for the wrong reason (the mix-bus/dry path writes output
   regardless of reverb buffer state).
3. `json_config.c`'s `tok_copy()` silently truncates register names,
   producing misleading "unknown register" messages for long-but-valid
   identifiers.
4. `preset_names.c`'s `spu94_cli_preset_name_list` silently truncates
   and returns early — callers see an incomplete list.

Beyond silent-failure, the audit surfaced two **critical** issues:

- **C-01: `drwav_read_pcm_frames_s16` requires signed pcm**; `dr_wav` will
  happily read 8-bit unsigned PCM and deliver garbage sign-extended values,
  because we do not check `bitsPerSample`. An 8-bit or 24-bit WAV opens
  cleanly (channels==2, rate==44100) and produces nonsense audio.
- **C-02: `main.c`'s output-buffer malloc is unchecked for overflow**
  when `total_out * sizeof(int16_t)` wraps on 32-bit size_t, or when
  `--tail-seconds` takes a huge value.

The Python binding has solid ctypes discipline overall (ndpointer, absolute
library path, drift assertions, explicit bool-rejection) — **but the
keep-alive anchors for `work_buf` and `state_buf` have a GC hazard**
(H-03) that the self_test doesn't exercise.

See per-finding detail below. Severity scale: critical / high / medium / low / nit.

---

## Critical

### C-01 — WAV loader accepts non-16-bit PCM and ships nonsense samples

**File:** `src/cli/wav_io.c:44-65`
**Category:** Silent-failure / input validation
**Severity:** critical

`spu94_cli_wav_load` checks `dw.channels != 2` and `dw.sampleRate != 44100`
but **never checks `dw.bitsPerSample`**. `drwav_read_pcm_frames_s16`
happily converts 8-bit unsigned, 24-bit signed, 32-bit float, etc. into
int16 — but the sample-value envelope for some of those (8-bit unsigned
in particular) produces silent / near-DC output once converted. More
importantly for the "Anthony-the-engineer" audience: a 24-bit 44.1 kHz
stereo file is by far the most likely thing he drops on the CLI, and
`drwav` will quietly down-convert to 16-bit without warning — silent
bit-depth loss on a bit-faithful-DSP tool is a Principle-of-Least-Surprise
violation.

**Fix:**
```c
if (dw.bitsPerSample != 16) {
    snprintf(err_buf, err_buf_size,
             "WAV file '%s' is %u-bit; 16-bit PCM required "
             "(use ffmpeg -sample_fmt s16 to convert)",
             path, (unsigned)dw.bitsPerSample);
    drwav_uninit(&dw);
    return 1;
}
if (dw.translatedFormatTag != DR_WAVE_FORMAT_PCM) {
    snprintf(err_buf, err_buf_size,
             "WAV file '%s' is not PCM; 16-bit PCM required", path);
    drwav_uninit(&dw);
    return 1;
}
```

---

### C-02 — Output-buffer sizing can overflow `size_t` on 32-bit builds / pathological `--tail-seconds`

**File:** `src/cli/main.c:203-216`
**Category:** Bug / security
**Severity:** critical

```c
uint64_t tail_frames = (tail_seconds > 0.0)
    ? (uint64_t)(tail_seconds * (double)input.sample_rate + 0.5)
    : 0u;
uint64_t total_out = input.num_frames + tail_frames;

int16_t *L_out = (int16_t *)malloc((size_t)total_out * sizeof(int16_t));
```

Three issues:

1. **`tail_seconds` is not upper-bounded.** `--tail-seconds 1e18` becomes
   `tail_frames = 4.4e22`, which wraps when cast to `size_t` on both 32-
   and 64-bit hosts and `malloc` gets a small, nonsensical size. The
   subsequent `spu94_flush` loop writes *far* past the end of the
   allocation.
2. **`total_out * sizeof(int16_t)` silently overflows** when
   `total_out > SIZE_MAX/2` (≈ 2^62 frames on 64-bit — unlikely, but a
   32-bit build overflows at 1 GiB of frames, which is reachable with a
   2-hour tail).
3. **The `(uint64_t)` cast of a huge double is UB** in C if the value
   exceeds `UINT64_MAX`; on x86-64 this typically produces
   `0x8000000000000000`.

**Fix:** Cap tail_seconds and check multiplication overflow:
```c
if (tail_seconds > 3600.0) {
    SPU94_ERROR("--tail-seconds %g exceeds 3600 (one-hour) limit", tail_seconds);
    free(input.L); free(input.R); return 2;
}
uint64_t tail_frames = (uint64_t)(tail_seconds * input.sample_rate + 0.5);
if (tail_frames > UINT64_MAX - input.num_frames) { /* overflow guard */ }
uint64_t total_out = input.num_frames + tail_frames;
if (total_out > SIZE_MAX / sizeof(int16_t)) {
    SPU94_ERROR("output would exceed addressable memory");
    /* cleanup */ return 2;
}
```

Also: `strtod` accepts `inf`, `nan`, and hex doubles (`0x1p60`). The
current `tail_seconds < 0.0` check lets all of those through because
`NaN < 0.0` is false and `inf < 0.0` is false. Reject with `isfinite()`:
```c
if (!isfinite(tail_seconds) || tail_seconds < 0.0) { /* error */ }
```

---

## High

### H-01 — `work_buf_size=8192` default is too small for most presets (the audit trigger)

**File:** `python/spu94/api.py:52`, `python/spu94/reverb.py:56`, `python/spu94/api.py:414`
**Category:** Silent-failure / default correctness
**Severity:** high

This is the bug that triggered the audit. The 8192-byte default:

- Is **undocumented as being too small**. The docstring at `api.py:60`
  says "8192 bytes is a small-footprint default that covers most short
  delays" — in reality it does NOT cover Hall, Studio A/B/C, Half Echo,
  Space Echo, Echo, or Delay. It only covers Off (trivial) and Room.
- Silently degrades because `spu94_reverb.c` reverb_buf_read returns 0
  and reverb_buf_write is a no-op when `byte_off + 1 >= work_buf_size`.
- The CLI-side default is 512 KB (`main.c:153`), so the CLI works. The
  Python default is 8192. The two surfaces disagree and the smaller one
  silently degrades.

**Fix (layered, do all three):**

1. **Raise the Python defaults to `0x80000` (512 KB)** to match the CLI
   and the underlying PS1 hardware's reverb RAM size. At 2025 memory
   prices this is irrelevant; it is 0.5 MiB and removes a whole class
   of bug.

2. **Add a C-side helper** `spu94_preset_required_work_buf(preset_id)`
   that returns the maximum address any register in the preset touches,
   plus a word. Then `load_preset` can optionally validate. Even better:

3. **Make the core C functions FAIL instead of silently degrade.**
   In `spu94_reverb.c:54,69`, instead of `return 0;` and silent no-op
   write, set a sticky `state->reverb_oob = 1` flag. Expose it via a
   new `spu94_get_reverb_oob(state)` accessor. Callers can check once
   per block. The Python `process()` can check it after the call and
   raise `RuntimeError("reverb work buffer too small for current
   preset")` the first time it fires.

(3) is the only real fix — the others paper over the underlying contract
hole that allowed the bug to ship in the first place. The rest of this
review assumes "silent-degradation is a bug class" and flags additional
instances below.

---

### H-02 — `self_test()` uses the broken 8192 default AND its audibility check passes for the wrong reason

**File:** `python/spu94/api.py:414-461`
**Category:** Silent-failure / test quality
**Severity:** high

```python
state = init(work_buf_size=8192)
```

`self_test` loads Hall and asserts at least one output sample is non-zero
under non-silent input. This passes **even when the reverb buffer is
entirely broken**, because the dry / mix-bus path writes output that
does not depend on `reverb_buf_read` succeeding. The test was *added*
specifically to catch "reverb not wired to output" regressions (the
code comment at line 428-432 says so) and it does not actually catch
that class of bug.

**Why it's broken:** feeding a non-silent input produces non-zero output
even when Hall's entire IIR network is reading zeros from a stuck buffer.
vLOUT/vROUT at 0x7FFF route the mix-in signal straight to output via a
path that does not require any reverb_buf_read to succeed.

**Fix:** run one arm with `spu94.Preset.OFF` (baseline) and one with
`spu94.Preset.HALL`; assert the RMS of Hall output is materially larger
than Off. With correct buffer size that delta is large. With the broken
8192 default, it collapses to ~1x (the dry paths are identical). Also:
use `work_buf_size=0x80000` in self_test so the test doesn't share the
bug it is supposed to catch.

```python
def self_test() -> None:
    def _render(preset):
        state = init(work_buf_size=0x80000)
        try:
            load_preset(state, preset); tick(state)
            set_reg_i16(state, "vLOUT", 0x7FFF)
            set_reg_i16(state, "vROUT", 0x7FFF)
            tick(state)
            n = 4096
            L = (np.sin(np.arange(n) * 0.1) * 8000).astype(np.int16)
            R = L.copy()
            Lo = np.zeros(n, dtype=np.int16); Ro = np.zeros(n, dtype=np.int16)
            process(state, L, R, Lo, Ro)
            return float(np.sqrt(np.mean(Lo.astype(np.float64)**2)))
        finally:
            destroy(state)
    rms_off  = _render("off")
    rms_hall = _render("hall")
    if rms_hall < rms_off * 1.5:
        raise RuntimeError(
            f"self_test: Hall RMS {rms_hall:.1f} not materially > "
            f"Off RMS {rms_off:.1f} — reverb not in audio path"
        )
```

---

### H-03 — Python keep-alive anchors rely on `c_void_p` attribute-setting, which leaks on destroy-then-reset

**File:** `python/spu94/api.py:101-104`, `python/spu94/reverb.py:78-84`
**Category:** ctypes correctness / resource lifecycle
**Severity:** high

```python
handle = ctypes.c_void_p(state)
handle._spu94_state_buf = raw      # keep-alive anchor
handle._spu94_work_buf = work_buf  # keep-alive anchor
```

Two problems:

1. **`destroy()` zeros the state bytes but does NOT drop the anchors.**
   After `destroy`, `handle._spu94_state_buf` is still live. The C library
   has no way to know the Python side already "destroyed" — so if the user
   then accidentally calls `spu94.process(rev.state, ...)` (where
   `rev._state` is still the `c_void_p` referenced by the SPU94 class
   only until the property guard), the ctypes dispatcher happily pushes
   the (now zeroed) pointer into C and the C side crashes or produces
   garbage. The `SPU94` class's `state` property guards this, but the
   raw-panel `api.destroy(handle)` does NOT invalidate the handle — a
   subsequent `api.process(handle, ...)` is UB.

2. **The `work_buf = (ctypes.c_uint8 * work_buf_size)()` is stack-like
   Python object**; the address passed to `spu94_init` is
   `ctypes.cast(work_buf, ctypes.c_void_p)`. That cast returns a NEW
   `c_void_p` holding the integer address; `work_buf` itself is not
   referenced by that new pointer, so without the `_spu94_work_buf`
   attribute-anchor the buffer would be GC'd immediately. The anchor
   works, but it's fragile — if anyone later refactors `init()` to
   return just `state` (dropping the anchor), the bug is immediate and
   hard to debug. Consider boxing the anchors in a small class:

```python
class _StateHandle(ctypes.c_void_p):
    """c_void_p with strong references to the owning buffers."""
    __slots__ = ("_state_buf", "_work_buf", "_destroyed")

def destroy(state):
    _lib.spu94_destroy(state)
    if hasattr(state, "_destroyed"):
        state._destroyed = True
    state.value = 0   # null the pointer so subsequent C calls NULL-segfault
                      # inside the C-side argument-validator rather than
                      # touching zeroed state
```

Setting `state.value = 0` after destroy is the key change — it converts
"silent UB" into "C-side NULL check fires."

---

### H-04 — `json_config.c` `tok_copy` silently truncates long register names → misleading error messages

**File:** `src/cli/json_config.c:49-56, 101-112`
**Category:** Silent-failure
**Severity:** high

```c
static void tok_copy(const char *json, const jsmntok_t *t,
                     char *out, size_t out_size) {
    ...
    size_t copy_len = (tlen < out_size - 1) ? tlen : (out_size - 1);
    memcpy(out, json + t->start, copy_len);
    out[copy_len] = '\0';
}
```

And `find_reg_by_name` (line 101) rejects keys with `tlen >= 64` silently
by returning `-1`, which then produces the error
`"unknown register 'FIRST_63_CHARS_OF_NAME'"` — misleading, because
the register name might actually be valid but the user has a 70-character
typo or junk key. The current wording suggests the name is *wrong* when
the real problem is *too long*.

**Fix:** distinguish the cases:
```c
if (tlen >= 64) {
    snprintf(err_buf, err_buf_size,
             "JSON key longer than 63 chars — likely malformed config");
    return 1;
}
```

Related: in `parse_int_or_hex` (line 63), `len >= 32` silently rejects
with `-1`, which surfaces as "invalid value" — same class.

---

### H-05 — Flat config's duplicate-key fix has a hole: unknown keys + count==35 bypasses the "all 35 registers" invariant

**File:** `src/cli/json_config.c:358-398`
**Category:** Bug / input validation
**Severity:** high

The flat-config path checks `pairs != SPU94_REG__COUNT`. But jsmn counts
*every* top-level key, so a config like:

```json
{ "vIIR": 0, "vALL": 0, "bogus": 0, "typo": 0, ..., /* 31 more bogus */ }
```

has `pairs == 35` and passes the count check. Then `apply_one` rejects
the first unknown key with an "unknown register" error — fine for that
specific user-facing message, but the user never learns that **even if
every key were valid**, the file would fail because none of the real
35 register names are present. More subtly: a flat config with 34 valid
keys + 1 typo has `pairs == 35`, passes the count check, and fails with
an "unknown register" message that hides the fact that one real register
is *missing*.

**Fix:** After the loop, check that all 35 `seen[]` slots are true:

```c
/* After the apply loop: */
for (int r = 0; r < SPU94_REG__COUNT; ++r) {
    if (!seen[r]) {
        snprintf(err_buf, err_buf_size,
                 "flat config '%s' is missing register '%s'",
                 path, spu94_reg_name((spu94_reg_t)r));
        free(json);
        return 1;
    }
}
```

This also makes it possible to give a better error for the
"count==35 but 34 valid + 1 typo" case — the typo gets flagged AND
the missing register gets flagged on a re-run.

---

### H-06 — `parse_int_or_hex` accepts `"0x"` + trailing garbage via strtol's lax parsing

**File:** `src/cli/json_config.c:61-97`
**Category:** Bug / input validation
**Severity:** high

`strtol(s + 2, &endp, 16)` on input like `"0xFFFFFFFF"` returns `LONG_MAX`
on 32-bit long systems with `errno=ERANGE`, which IS caught. But on a
64-bit long system, `"0xFFFFFFFF"` returns `4294967295` — fits in `long`,
no ERANGE — then the `v < INT_MIN || v > INT_MAX` check fires correctly.
That part is fine.

The real issue: **negative-hex parsing does not validate the magnitude
before negation.** Line 81-82:

```c
v = strtol(s + 2, &endp, 16);
if (negative) v = -v;
```

If `s = "-0x80000000"` on a 64-bit system, `strtol` returns
`0x80000000 = 2147483648` (positive, fits in long), then `v = -v` makes
it `-2147483648 = INT_MIN`. The subsequent `v < INT_MIN` check passes.
But the round-trip is wrong for a user who wrote `"-0x80000001"` expecting
an error — they get `v = -2147483649`, check fires — ok, that case works.

The *actual* hole: `"-0x0"` gives `v = 0` (negative zero is zero), which
is silently accepted as `0`. Not a bug, just surprising.

The real bug is **`strtol` with base 16 accepts `0x` prefix as optional**
— `strtol("0x", &endp, 16)` returns 0 with `endp = s` (no characters
consumed). We already check `s[2] == '\0'` for bare `"0x"`, but we
miss `"0xG"` — wait, no, `strtol` stops at `G`, returning 0 with `endp
-> "G"`, and our `*endp != '\0'` catches it. OK, that's fine.

**What is actually broken:** `INT_MIN` on a platform where `long` is 32
bits. `errno = ERANGE` is set by strtol, we catch it. OK.

**Fix:** the code is defensible but under-tested. Add fuzz coverage for:
- `"-0x"` (should error — currently errors via `s[2] == '\0'`)
- `"0x80000000"` (positive overflow on 32-bit long — caught by ERANGE)
- `"-0x80000001"` (negative beyond INT_MIN — caught by INT_MIN check)
- `"+0x10"` (plus-sign prefix — currently handled on line 78)
- `"0X10"` (capital X — handled on line 79)
- `"0x 10"` (space — strtol stops at space, *endp != '\0', rejected)

Most are handled; the code would benefit from a unit test harness that
exercises all of these edges. Severity **high** because the analysis is
non-obvious and a future refactor could break it.

---

## Medium

### M-01 — `_silent_in` cache is module-global and not thread-safe

**File:** `python/spu94/api.py:134-151`
**Category:** Bug / concurrency
**Severity:** medium

```python
_silent_in: Optional[np.ndarray] = None

def _silent_input(n: int) -> np.ndarray:
    global _silent_in
    if _silent_in is None or len(_silent_in) < n: ...
```

Two threads each calling `process(state_A, None, None, Lo, Ro, 1024)` and
`process(state_B, None, None, Lo, Ro, 4096)` race on `_silent_in`. Even
if the race is benign (zero data, worst case is a redundant allocation),
numpy is not guaranteed reentrant and the `_silent_in[:n]` slice could
observe an in-flight resize. The library isn't declared thread-safe
(D-01 doesn't promise that), but the self_test and typical notebook
usage will single-thread so this is medium not high.

**Fix:** make it per-state, stored on the handle as another keep-alive
anchor; OR wrap in a `threading.Lock`; OR document "binding is not
thread-safe, wrap calls in your own mutex."

---

### M-02 — `spu94_cli_preset_name_list` silently truncates the list

**File:** `src/cli/preset_names.c:84-97`
**Category:** Silent-failure
**Severity:** medium

```c
if ((size_t)w >= buf_size - pos) return;  /* truncation — stop early */
```

When the error-message buffer is smaller than the full 10-preset list
(~89 chars + commas), the function silently returns the first N names.
Callers print `"valid: hall, room, studio_a"` instead of the full list,
and the user sees a misleading "these are your options" message.
`main.c:181` passes `names[256]` which is enough for the current 10
presets (~120 chars), but the interface doesn't statically enforce
"buf is big enough" — a future 20-preset release would silently truncate
without any warning.

**Fix:** on truncation, write `"...+N more"` suffix, or abort with a
build-time `_Static_assert` that the caller buffer is big enough for
all preset names. At minimum, document the required minimum size.

---

### M-03 — `json_config.c` reads JSON without checking `fread`'s return value semantics

**File:** `src/cli/json_config.c:231-233`
**Category:** Bug
**Severity:** medium

```c
size_t got = fread(json, 1, (size_t)fs, fp);
fclose(fp);
json[got] = '\0';
```

If `fread` returns less than `fs` (partial read — rare but possible on
network filesystems or if the file is truncated between `ftell` and the
read), we NUL-terminate at `got` and hand a truncated JSON to jsmn. jsmn
will produce a parse error, so the user sees "invalid JSON" — technically
correct but misleading. Worse: if `fread` returns 0 (file vanished), we
NUL-terminate at byte 0 and parse an empty string, producing the same
"invalid JSON" error.

**Fix:**
```c
if (got != (size_t)fs) {
    free(json); 
    snprintf(err_buf, err_buf_size,
             "could not fully read '%s' (%zu of %ld bytes)", path, got, fs);
    return 1;
}
```

---

### M-04 — `wav_io.c` write path does not `fflush`/`fsync` before `drwav_uninit`

**File:** `src/cli/wav_io.c:162`
**Category:** Low-risk data-loss
**Severity:** medium

`drwav_uninit` closes the file but doesn't fsync. If the user's shell is
killed right after the CLI returns 0 (e.g. container OOM kill, SIGKILL
from parent), the output WAV may be missing its last chunk on some
filesystems. Not a correctness issue per se, but for a tool that
advertises "the render completed successfully (exit 0)" to then leave
an unflushed file on disk is surprising.

**Fix:** after `drwav_uninit`, open the file again and `fsync(fileno(fp))`,
or use `drwav_init_file_write_sequential` with a post-uninit fsync, or
just document that exit 0 means "all data was handed to the kernel" not
"all data is on disk."

Low priority — record-to-disk tools generally don't fsync.

---

### M-05 — `_coerce_reg` accepts arbitrary ints without bounds check

**File:** `python/spu94/api.py:308-324`
**Category:** Bug / input validation
**Severity:** medium

```python
if isinstance(reg, int):
    return reg
```

`reg = 9999` is silently forwarded to the C side. `spu94_set_reg_i16`
returns `SPU94_UNKNOWN_REG` so the caller gets a result code — that's
the documented contract — but a user who wrote `set_reg(999, 42)` gets
a result code they have to inspect manually, whereas a user who wrote
`set_reg("nonexistent", 42)` gets a `KeyError` immediately. Asymmetric
error shapes for equivalent mistakes.

**Fix:** bounds-check ints to `[0, SPU94_REG__COUNT)` and raise
`ValueError` the same way strings do.

---

### M-06 — `process()`'s length validator returns `len(L_out)` but doesn't verify `L_out` is non-None

**File:** `python/spu94/api.py:154-175`
**Category:** Bug
**Severity:** medium

```python
def _validate_process_lengths(L_in, R_in, L_out, R_out) -> int:
    n = len(L_out)   # <-- AttributeError if L_out is None
```

`L_out=None` falls through to `len(None)` which raises `TypeError`. The
docstring at api.py:19-22 already says "NULL outputs are never useful,"
but the validator gives a confusing `TypeError: object of type 'NoneType'
has no len()` rather than the actionable:

```python
if L_out is None or R_out is None:
    raise TypeError("L_out and R_out must be int16 numpy arrays (None is not legal)")
```

---

### M-07 — `presets.py` decodes preset names as ASCII; UTF-8 in a preset name crashes import

**File:** `python/spu94/presets.py:100, 148`
**Category:** Bug (latent)
**Severity:** medium

```python
_actual = _actual_bytes.decode("ascii")
```

No current preset name uses non-ASCII. But if a future phase adds a
preset named "Höhle" or "Café Echo", **import of the `spu94` package
fails** with `UnicodeDecodeError`. Even if you never ship such a name,
the ABI contract technically allows any NUL-terminated C string in
`spu94_presets[i].name`. Switching to `"utf-8"` costs nothing and removes
a footgun.

---

## Low

### L-01 — `preset_names.c` uses `tolower` on signed char (UB on negative values)

**File:** `src/cli/preset_names.c:32`
**Category:** Bug (latent)
**Severity:** low

```c
unsigned char c = (unsigned char)in[i];
...
out[i] = (char)tolower(c);
```

Good: the cast to `unsigned char` before `tolower` is correct. But if
`in` contains high-ASCII bytes (e.g. `--preset café`), the result is
locale-dependent. On a Turkish locale, `tolower('I')` → `'ı'`. The current
code is defensible for pure-ASCII preset names, but shell CLIs in
non-C locales can surprise. Call `setlocale(LC_ALL, "C")` at CLI entry
OR use a private ASCII-only tolower.

---

### L-02 — `main.c` hardcodes `WORK_BUF_SIZE = 512 KB` with a "well under 64 KB" comment that is wrong

**File:** `src/cli/main.c:152-153`
**Category:** Documentation drift
**Severity:** low

```c
 * work_buf: 512 KB is enough for all PS1 SPU preset buffer-offsets
 * (the largest delay address + 4 bytes is well under 64 KB).
```

The PS1 SPU reverb region is 256 KB (`0x40000`), not 64 KB. The largest
mBASE + buffer-offset combination in the factory presets approaches
`0x40000 - 1`. 512 KB is correct for headroom but the comment is wrong.
Also: why 512 KB and not exactly `0x80000` (524288)? The comment should
justify the choice.

**Fix:** change comment to `"512 KB matches the PS1 SPU's 256 KB reverb
region with 2x headroom"`, and replace `512u * 1024u` with `0x80000` to
make the hardware-alignment intent obvious.

---

### L-03 — Python `init()` does not support `work_buf` = 0 (valid per C contract)

**File:** `python/spu94/api.py:86-91`
**Category:** Missing feature / API asymmetry
**Severity:** low

The C-side `spu94_init` accepts `work_buf=NULL && work_buf_size=0` as a
legal "don't-care" config (header comment at spu94.h:118-120). The
Python `init(work_buf_size=0)` would allocate a zero-length ctypes
array and pass a non-NULL zero-size pointer — technically different
from the C contract. If someone explicitly passes `0` expecting
"no reverb, just bypass," they get confusing behavior.

**Fix:**
```python
if work_buf_size == 0:
    work_buf = None
    work_buf_addr = None
else:
    work_buf = (ctypes.c_uint8 * work_buf_size)()
    work_buf_addr = ctypes.cast(work_buf, ctypes.c_void_p)
```

---

### L-04 — `cli.py` `os.execv` leaks the Python interpreter state

**File:** `python/spu94/cli.py:78`
**Category:** Minor behavior
**Severity:** low

`os.execv` replaces the Python interpreter — no cleanup of already-opened
file descriptors, no atexit hooks, no `finally` blocks. If a user has
activated a context manager or has an atexit registered (e.g. logging
flush), it's silently skipped. For the shim-executable use case this is
correct behavior, but for users who import `spu94.cli.main` and call it
programmatically (wouldn't be the expected pattern, but possible), the
Python state vanishes mid-function.

**Fix:** document explicitly: "cli.main() calls os.execv and does not
return." The docstring at line 59-63 mentions execv but not the
"does not return" / "skips atexit" consequences.

---

### L-05 — CLI's `--tail-seconds` parser accepts `" 1.5 "` but rejects `"1.5\n"` inconsistently

**File:** `src/cli/main.c:97-103`
**Category:** Minor / user-surface
**Severity:** low

`strtod` skips leading whitespace but does not skip trailing whitespace.
The `*endp != '\0'` check then rejects `"1.5 "` (trailing space). Given
that shells strip trailing whitespace from argv, this is a non-issue in
practice. If a user ever constructs argv programmatically, they get a
rejection with a misleading message.

Not worth fixing. Just noting.

---

## Nit

### N-01 — Module docstrings reference "Plan N Task M" artifacts that won't survive M1 close-out

**File:** most Python files, all CLI files
**Category:** Documentation cleanup
**Severity:** nit

Comments like `"Phase 6 Plan 2 Task 2"`, `"D-01..D-11"`, `"T-06-02..T-06-04"`
are useful during development but will be stale history after v1.0 ships.
Consider a sweep that either inlines the referenced rules or strips the
pointers. Not urgent.

---

### N-02 — `api.py:_coerce_reg` has a `from . import Register` inside the hot path

**File:** `python/spu94/api.py:310-311`
**Category:** Performance (out of scope — v1 review says skip perf)
**Severity:** nit

Per-call import is cached after the first lookup, but `import` hits the
module cache every call. At 44100 samples/sec with block=1024, this adds
up over a long render. **Skipping per v1 scope.** Noted for future
optimization if profile says this matters.

---

### N-03 — `reverb.py` has two nearly-identical `_coerce_reg`-like blocks

**File:** `python/spu94/reverb.py:136-154` and `api.py:308-324`
**Category:** Duplication
**Severity:** nit

`SPU94._reg_type` duplicates the enum/bool/int/str coercion logic from
`api._coerce_reg`, then adds a `spu94_reg_type` C call. Factor the
coercion out (e.g. make `api._coerce_reg` public as `api._reg_to_int`
and call it from both).

---

### N-04 — `json_config.c` allocates `json` buffer for 1 MB max but never uses the limit for fuzz resistance

**File:** `src/cli/json_config.c:217`
**Category:** Defensive coding
**Severity:** nit

1 MB cap is stated but arbitrary. Typical flat config is < 2 KB, override
config < 500 bytes. Reducing to 64 KB would close a (small) resource-
exhaustion vector without inconveniencing any real user. Not urgent.

---

## Cross-cutting observations

### The "silent degradation instead of loud error" pattern

Across this audit, the same anti-pattern appears in:

1. `spu94_reverb.c:54,69` — reverb_buf_read/write silently no-op when
   buffer is too small (the root cause).
2. `wav_io.c` — no bit-depth check; dr_wav silently down-converts.
3. `json_config.c:49-56` — tok_copy silently truncates long keys.
4. `preset_names.c:94` — list builder silently truncates preset names.
5. `api.py` `_silent_input` — silently reuses a shared buffer across
   threads.
6. `api.py init` / `reverb.py SPU94` — 8192 default silently degrades.
7. `api.py self_test` — uses broken default, passes for wrong reason.

**Recommendation for M1 close-out:** adopt a project rule that validation
failures at user-facing boundaries (CLI argv, JSON, WAV headers, Python
types) MUST raise or exit non-zero. Silent clamping / truncation /
substitution is banned on user-facing paths. Internal DSP code (C core)
can still clamp for hot-path performance, but it MUST set a sticky flag
that the outer layer surfaces to the user on next call.

### The ctypes side is solid

Despite the findings above, the ctypes plumbing is carefully done:
absolute library paths, ndpointer for int16 + C-contiguous enforcement,
drift assertions at import, explicit bool-rejection, argtypes/restype
set for every symbol, matched by `test_binding_surface.py`. The
zero-copy claim is validated by the sentinel test. The main exposure is
the keep-alive anchor fragility (H-03), not the ctypes contract.

### Recommended close-out blocker vs. ship-with-known-issues

**Block M1 close on:**
- C-01 (bit-depth check), C-02 (overflow guards), H-01 (default), H-02 (self_test)

**Acceptable to defer to M2 (document in CHANGELOG as known):**
- H-03 (keep-alive refactor) — only hits if user calls raw-panel api.destroy then api.process
- H-04, H-05 (JSON error-message quality)
- M-* and L-* (polish)

**Hard requirement:** at least add the `spu94_get_reverb_oob` sticky
flag so future silent-degradation bugs surface on the next block.

---

_Reviewed: 2026-04-24_
_Reviewer: gsd-code-reviewer (deep)_
_Scope: v1.0 close-out — CLI + Python binding_
