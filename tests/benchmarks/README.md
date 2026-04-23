# Phase 7 Benchmark Harness

Report-only timing track for `spu94_process`. See `.planning/phases/07-*/07-05-PLAN.md`.

## Policy (D-20 / D-21)

- **D-20:** timing is **report-only**. The only real-time-safety GATE is
  `tests/rt_safety/hotpath_alloc_gate.sh` (hard CI fail on any heap syscall
  in the `spu94_process` hot path). `test_benchmark.py` publishes numbers
  for historical comparison; CI never fails the build on timing jitter.
- **D-21:** `benchmark_baselines.json` is committed and human-reviewed.
  CI never regenerates it. Any diff is visible in `git diff` on the PR.

## Refresh baseline (manual, human-reviewed)

Run on the dev workstation after reviewing the numbers:

```bash
SPU94_LIB=$(pwd)/build/src/spu94/libspu94.so \
    pytest tests/benchmarks/test_benchmark.py \
        --benchmark-json=tests/benchmarks/benchmark_baselines.json \
        --benchmark-columns=min,mean,median,stddev,rounds,iterations \
        -q

# Strip per-round raw samples to keep the committed file small
# (~26 KB vs ~3.6 MB with the full `data[]` array kept).
python3 - <<'EOF'
import json
with open('tests/benchmarks/benchmark_baselines.json') as f:
    r = json.load(f)
for b in r['benchmarks']:
    b['stats']['data'] = []
with open('tests/benchmarks/benchmark_baselines.json', 'w') as f:
    json.dump(r, f, indent=2, sort_keys=True)
EOF

git add tests/benchmarks/benchmark_baselines.json
git commit -m "bench: refresh Phase 7 baselines"
```

## What the CI reports

The `benchmark-report` CI job runs the same harness, writes
`bench-ci.json`, and uploads it as a build artifact. It does NOT diff
against the committed baseline and does NOT fail on regressions --
ubuntu-latest runner jitter would produce too many false positives.
Comparison is a human-in-the-loop activity; download both JSONs and
diff manually when curious.

## Shape of the harness

- 10 presets (Off, Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay)
- 2 block sizes (1024, 4096 samples at 44.1 kHz)
- `min_rounds=5`, `warmup=True`, `disable_gc=True`

= 20 benchmark groups total.
