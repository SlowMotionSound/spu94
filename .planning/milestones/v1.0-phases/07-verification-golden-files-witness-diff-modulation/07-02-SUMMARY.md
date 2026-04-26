---
phase: 07-verification-golden-files-witness-diff-modulation
plan: 02
subsystem: verification-reproducibility
tags: [goldens, docker, reproducibility, sha256, ci, threat-mitigation, dockerignore]
requires:
  - phase-6-python-binding-cli
  - phase-7-plan-01-foundation
provides:
  - 50-golden-wav-corpus
  - 50-sha256-sidecars
  - deterministic-golden-generator-with-check-mode
  - pinned-bookworm-slim-repro-container
  - reproducibility-ci-job
  - dockerignore-host-clutter-exclusion
  - t-07-02-a-through-e-mitigations
affects:
  - future-phase-07-plans-consume-golden-corpus
  - future-ci-runs-prove-byte-reproducibility-on-every-push
  - future-dockerfile-work-inherits-clean-build-context
tech-stack:
  added:
    - docker-ce-29.1.3
    - debian-bookworm-slim-sha-pinned
  patterns:
    - dockerignore-over-in-dockerfile-rm
    - closed-allowlist-for-filesystem-path-construction
    - .wav-plus-.sha256-sidecar-pair
    - digest-pinned-base-image-as-sole-pin-point
    - determinism-env-mirrored-host-to-container
    - sha256-fresh-render-vs-committed-sidecar-diff
key-files:
  created:
    - scripts/regenerate_goldens.py
    - scripts/test_regenerate_goldens.py
    - tests/conformance/test_goldens_present.py
    - Dockerfile.repro
    - .dockerignore
    - tests/golden/.gitkeep
    - tests/golden/{10 presets}/{5 inputs}.wav (50 files)
    - tests/golden/{10 presets}/{5 inputs}.wav.sha256 (50 files)
  modified:
    - tests/conformance/CMakeLists.txt
    - .github/workflows/ci.yml
    - .gitignore
decisions:
  - "D-14 digest at execute time: used the LIVE amd64 bookworm-slim digest sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644 (verified 2026-04-23 against Docker Hub Registry v2 API), superseding the 07-RESEARCH.md researcher-verified digest sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252 from 2026-04-22. Per D-14 discipline, 'whichever is live at plan-execute time.'"
  - "Standard input set parameters locked (per D-11, planner's discretion): 44100 Hz int16 PCM stereo, 2.0 sec / 88200 samples per channel, amplitude 16000. impulse: L[0]=R[0]=16000, rest zero. silence: all zeros. white_noise: np.random.default_rng(0x1094_DADA).integers(-16000, 16000, (88200,2), int16). sine_1khz: 16000*sin(2*pi*1000*t/44100), both channels identical. sweep: scipy.signal.chirp(t, 20.0, 2.0, 20000.0, method='logarithmic') * 16000, both channels identical."
  - "Host-vs-container reproducibility gap discovered during local smoke test: Dockerfile's `COPY . .` was pulling the host's `build/` directory (with a host-path-pinned CMakeCache.txt) into the image, causing in-container cmake to refuse the cache. Fix was a new .dockerignore excluding build/, .git/, .planning/, .venv/, __pycache__/, *.pyc, .pytest_cache/, .mypy_cache/, *.egg-info/, dist/ — standard docker practice, chosen over an in-Dockerfile `rm -rf build` workaround because (a) prevents the whole class of host-clutter-leaks-into-image bugs, (b) smaller image, (c) faster builds (less context sent to daemon). .dockerignore's commit message references 07-02 so its origin remains traceable."
  - "Goldens committed in-tree (not .gitignored) despite ~2 MB total size: conformance CI needs them present to diff against. .gitignore explicitly excludes only generated build artefacts, not goldens."
  - "Path-traversal mitigation (T-07-02-D): PRESETS and INPUTS are module-level literals inside scripts/regenerate_goldens.py; no CLI arg, env var, or external config reaches filename construction. Acceptance criterion `grep -q 'PRESETS = \\[' scripts/regenerate_goldens.py` pins this."
  - "Silent-mutation mitigation (T-07-02-E): meta-test `test_check_fails_on_mutated_wav` copies the committed tree to pytest tmp_path, flips one byte in one .wav, asserts --check exits 1 and stderr names the mutated preset/input. Belt-and-suspenders on top of the CI reproducibility job (which also catches any committed-byte divergence)."
  - "Supply-chain posture (T-07-02-B, accepted): no per-package apt pins in Dockerfile.repro. The base-image digest transitively locks toolchain versions. Updating the digest requires a D-15 ADR + golden regeneration."
metrics:
  duration_minutes: ~90 (across two sessions: executor initial landing + human smoke-test close-out)
  tasks_completed: 3
  files_created: 55 (50 .wav + 50 .sha256 == 100; + 5 source/support files; PLUS 1 post-hoc .dockerignore fix; counting by logical artefact type the committed delta is 50 goldens + 50 sidecars + script + meta-test + conformance test + .gitkeep + Dockerfile.repro + .dockerignore)
  files_modified: 3 (tests/conformance/CMakeLists.txt, .github/workflows/ci.yml, .gitignore)
  ctest_targets_added: 2 (regenerate_goldens_meta, goldens_present — the latter expands to 100+ parametrized cases via pytest parametrize)
  ci_jobs_added: 1 (reproducibility)
  smoke_test_verified_by: human (Anthony) on 2026-04-23 (docker 29.1.3, ubuntu-studio host)
completed: 2026-04-23
---

# Phase 07 Plan 02: Golden Corpus + Docker Reproducibility Summary

**Shipped 50-golden .wav corpus (10 presets × 5 inputs) with paired SHA-256 sidecars, a deterministic `regenerate_goldens.py` with `--check` diff mode, a bookworm-slim sha-pinned `Dockerfile.repro` that reproduces the corpus byte-for-byte inside CI, and the `.dockerignore` that makes that reproducibility claim actually hold.**

## Performance

- **Duration:** ~90 min (executor pass ~60 min, human smoke-test close-out ~30 min across the docker-group checkpoint and the subsequent .dockerignore gap)
- **Started:** 2026-04-23 (executor wave 2 kickoff)
- **Completed:** 2026-04-23
- **Tasks:** 3
- **Files created:** 105 logical (50 goldens + 50 sidecars + 5 source/support files)
- **Files modified:** 3 (ci.yml, tests/conformance/CMakeLists.txt, .gitignore)

## Accomplishments

- 50 committed `.wav` + 50 committed `.sha256` sidecars under `tests/golden/<preset>/<input>.{wav,sha256}` — 10 presets × 5 inputs (impulse, white_noise, sine_1khz, silence, sweep).
- `scripts/regenerate_goldens.py` — deterministic generator with default-regen + `--check` modes, hardcoded `PRESETS`/`INPUTS` allowlists (path-traversal mitigation), determinism env (`LC_ALL=C`, `TZ=UTC`, `SOURCE_DATE_EPOCH=1704067200`).
- Meta-tests: `scripts/test_regenerate_goldens.py` (positive `--check` + mutated-byte negative) + `tests/conformance/test_goldens_present.py` (50 wav + 50 sidecar + 3 spot-check sha-match cases, all parametrized).
- `Dockerfile.repro` pinned to `debian:bookworm-slim@sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644` (live amd64 digest on 2026-04-23, verified against Docker Hub Registry v2 API), determinism env mirrored host→container, toolchain identical to `scripts/ci/install-phase7-deps.sh`.
- New `reproducibility` GitHub CI job added to `.github/workflows/ci.yml` — `docker build -f Dockerfile.repro -t spu94-repro .` then `docker run --rm spu94-repro`. Uses the SHA-pinned `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683` reused from Plan 07-01.
- `.dockerignore` excludes host-side `build/`, `.git/`, `.planning/`, and venv/python-cache clutter from the docker build context — closes the host-vs-container reproducibility gap caught during local smoke test.
- **Smoke test PASSED on Anthony's host (2026-04-23):** `sudo docker build -f Dockerfile.repro -t spu94-repro . && sudo docker run --rm spu94-repro` → exit 0, final line `PASS: 50/50 goldens match`. This closes the final acceptance gate for Task 3.

## Task Commits

Each task committed atomically; one post-hoc fix for the reproducibility gap discovered at smoke test:

1. **Task 1: ship 50-golden generator + commit golden corpus** — `a1b585a` (feat)
2. **Task 2: meta-tests for regenerate_goldens.py + goldens presence conformance test** — `a6d0e87` (test)
3. **Task 3a: pinned Dockerfile.repro + reproducibility CI job** — `81d4bb6` (feat)
4. **Pause checkpoint:** `7876180` (wip) — paused at smoke-test step awaiting docker group access for user `ubuntu-studio`; structured HANDOFF.json + `.continue-here.md` emitted.
5. **Task 3b: .dockerignore (post-smoke-test fix)** — `dfc5b01` (fix) — gap discovered during local docker build: host `build/` dir copied into image broke in-container cmake.

**Plan metadata:** this SUMMARY + roadmap plan-progress update (next commit).

## Standard Input Set — Locked (D-11)

All 5 inputs: 44100 Hz, int16 PCM, stereo (2 ch), 2.0 sec (88200 samples per channel), amplitude 16000.

| Input | Construction |
|-------|--------------|
| `impulse` | `x = zeros((88200, 2), int16); x[0, 0] = x[0, 1] = 16000` |
| `silence` | `zeros((88200, 2), int16)` |
| `white_noise` | `np.random.default_rng(0x1094_DADA).integers(-16000, 16000, (88200, 2), int16)` |
| `sine_1khz` | `16000 * sin(2π · 1000 · t/44100)`, both channels identical |
| `sweep` | `16000 * scipy.signal.chirp(t, 20.0, 2.0, 20000.0, method='logarithmic')`, both channels identical |

The seed `0x1094_DADA` pins white_noise across regenerations. The determinism env (`LC_ALL=C`, `TZ=UTC`, `SOURCE_DATE_EPOCH=1704067200`) is set both in the generator and in the Dockerfile so host-regen and in-container-regen operate in the same byte-level environment.

## Docker Digest — Execute-Time vs Research-Time

| When | Digest | Note |
|------|--------|------|
| Research (2026-04-22) | `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252` | `07-RESEARCH.md` — superseded |
| Execute (2026-04-23) | `sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644` | **In use.** Amd64 digest live on Docker Hub, verified via the command in `Dockerfile.repro` header comment. |

Per D-14: bumping this digest in the future requires a D-15 ADR + golden regeneration. The Dockerfile header comment records both digests so the revision trail is legible.

## Threat Mitigations (concrete code terms)

| Threat | Mitigation | Evidence |
|--------|-----------|----------|
| T-07-02-A (base image tampered / drift) | `FROM debian:bookworm-slim@sha256:<digest>` — digest pin, not tag pin. `docker build` fails loudly on unknown digest. | `grep -qE "^FROM debian:bookworm-slim@sha256:[0-9a-f]{64}$" Dockerfile.repro` (plan acceptance criterion) |
| T-07-02-B (apt supply-chain) | **Accepted, not mitigated further.** Base-image digest transitively locks toolchain. D-15 governs digest bumps. | Dockerfile header comment pins the policy; no `apt install pkg=version` lines. |
| T-07-02-C (CLI subprocess info disclosure) | CLI invoked with closed-enum preset name, no shell, no external input reaches argv construction. | `scripts/regenerate_goldens.py` `subprocess.run([spu94_bin, "--preset", preset, ...], ...)` — list form, no shell=True. |
| T-07-02-D (path traversal in `<preset>/<input>` filesystem paths) | `PRESETS` + `INPUTS` are module-level literals; no user-controlled name reaches `Path(...)`. | `grep -q "PRESETS = \\[" scripts/regenerate_goldens.py` (acceptance) + `PRESETS` defined at line ~15 as a Python list literal. |
| T-07-02-E (PR silently mutates committed .wav) | `--check` mode re-renders and diffs sha256 vs committed sidecar; CI `reproducibility` job fails on any mismatch. | Meta-test `test_check_fails_on_mutated_wav` proves the catch path works (exits 1, names offending preset/input). |

## Host-vs-Container Reproducibility Investigation

The final acceptance gate for Task 3 was a local smoke test on Anthony's host. First run failed:

```
CMake Error: The current CMakeCache.txt directory /work/build/CMakeCache.txt is
different than the directory /home/ubuntu-studio/Desktop/PSX Reverb/build where
CMakeCache.txt was created.
```

**Root cause:** `Dockerfile.repro`'s `COPY . .` (line 68) pulled the host's `build/` dir — which CMake generates with a source path baked into `CMakeCache.txt` — into the container. Inside the container, sources live at `/work`, so CMake detected the mismatch and refused to reuse the cache.

**Options considered:**

1. Add `rm -rf build` to the Dockerfile `RUN` line (the competing suggestion that appeared in the pending-edit UI).
2. Add a `.dockerignore` that excludes `build/` (and other host clutter) from the docker build context entirely.

**Chose option 2.** Rationale:
- Prevents the whole class of "host clutter leaks into image" bugs, not just this one symptom.
- Keeps `.git/`, `.planning/`, `.venv/`, Python caches out of the image too — smaller image, faster builds, smaller attack surface.
- Conventional docker practice; the in-Dockerfile `rm -rf` approach is an anti-pattern.

**Post-fix smoke test:** `sudo docker build -f Dockerfile.repro -t spu94-repro .` then `sudo docker run --rm spu94-repro` → `PASS: 50/50 goldens match`, exit 0. Reproducibility claim now holds on the first environment outside CI.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule — Blocking] `.dockerignore` missing from Dockerfile context hygiene**
- **Found during:** Task 3 local smoke test (the PLAN.md-required `docker build && docker run` verification step)
- **Issue:** PLAN.md's Task 3 contract did not list `.dockerignore` in `files_modified`, yet Task 3's acceptance criterion (`docker run --rm spu94-repro` exits 0 with PASS) couldn't be met without it on any host that had ever run a local `cmake -B build`.
- **Fix:** Created `.dockerignore` excluding `build/`, `.git/`, `.planning/`, `.venv/`, `__pycache__/`, `*.pyc`, `.pytest_cache/`, `.mypy_cache/`, `*.egg-info/`, `dist/`.
- **Files modified:** `.dockerignore` (created)
- **Verification:** Re-ran `sudo docker build -f Dockerfile.repro -t spu94-repro . && sudo docker run --rm spu94-repro` → exit 0, `PASS: 50/50 goldens match`.
- **Committed in:** `dfc5b01` (standalone fix commit, not folded into Task 3's `feat` commit per atomic-commit discipline)

---

**Total deviations:** 1 auto-fixed (blocking).
**Impact on plan:** Necessary for Task 3's acceptance criterion to hold on any real-world host. No scope creep — `.dockerignore` is minimum-necessary context-hygiene for the Dockerfile this plan shipped.

## Issues Encountered

- **Two sudo/auth checkpoints this phase.** Plan 07-01 paused on passwordless sudo (apt-get); Plan 07-02 paused on docker group membership. The executor's `.continue-here.md` flagged this as an advisory anti-pattern for future plans: any plan that shells out to `docker`, `sudo`, `apt-get`, or other auth-gated binaries must either document the privilege requirement in plan pre-reqs or emit a structured human-action checkpoint. Phase 7 plans going forward that use the pinned toolchain (already installed) should not hit new checkpoints; Plan 07-03 (witness-diff) is the one to watch — it runs `lv2apply` inside the host toolchain, not docker.
- **.dockerignore gap** (documented in Deviations above) — unsurprising on reflection, but a real miss in the plan's up-front file list.

## Next Plan Readiness

- Wave 2 plan 1 (07-02) complete. Wave 2 plan 2 is **07-05** (hot-path allocation gate via strace + pytest-benchmark timing harness). 07-05 shares `.github/workflows/ci.yml` and `.gitignore` with 07-02, so must run strictly sequentially — no parallelism inside Wave 2. Pre-reqs (`strace`, `pytest-benchmark`) already installed by Plan 07-01.
- Wave 3 (07-03 witness-diff + 07-04 modulation harness) and Wave 4 (07-06 bibliography + ADRs) remain scheduled per the phase's wave plan.
- The 50-golden corpus is now a Phase-7-wide asset: 07-03 witness-diff consumes the same input set; 07-04 modulation harness uses it as the per-register baseline; 07-06 BIBLIOGRAPHY.md will cite the corpus in BIB entries.

---
*Phase: 07-verification-golden-files-witness-diff-modulation*
*Plan: 02 (wave 2, plan 1 of 2)*
*Completed: 2026-04-23*
