#!/usr/bin/env python3
"""bench_latency.py -- Phase 5 Plan 04, D-09d.

Times 10^5 consecutive spu94_process(state, L, R, Lout, Rout, 1024) calls
using time.perf_counter_ns (backed by clock_gettime(CLOCK_MONOTONIC)).
Discards the first 1000 calls as warmup. Asserts the ratio

  (p99 - median) / median  <=  THRESHOLD

across the measurement window.

Usage: bench_latency.py [THRESHOLD]
  THRESHOLD defaults to 3.0. Planner calibrates the pinned threshold
  via the measure-then-pin protocol: run once, observe the ratio,
  pin max(2.0, 2 * observed) in CMake. See 05-RESEARCH.md § D-09d.

Exit codes:
  0 -- ratio within threshold
  1 -- ratio exceeds threshold OR measurement failed
"""
import ctypes
import os
import statistics
import sys
import time
from ctypes import POINTER, c_int16, c_size_t, c_uint32, c_void_p

THRESHOLD = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
LIB_PATH = os.environ.get("SPU94_LIB")
if not LIB_PATH:
    print("FAIL: SPU94_LIB env var required", file=sys.stderr)
    sys.exit(1)

# Load library + bind C signatures.
lib = ctypes.CDLL(LIB_PATH)

# size_t spu94_state_size(void);
lib.spu94_state_size.restype = c_size_t
lib.spu94_state_size.argtypes = []

# spu94_state *spu94_init(void *, size_t, void *, size_t);
lib.spu94_init.restype = c_void_p
lib.spu94_init.argtypes = [c_void_p, c_size_t, c_void_p, c_size_t]

# spu94_result_t spu94_load_preset(spu94_state *, spu94_preset_id_t);
lib.spu94_load_preset.restype = ctypes.c_int
lib.spu94_load_preset.argtypes = [c_void_p, ctypes.c_int]

# void spu94_process(spu94_state *, L_in, R_in, L_out, R_out, uint32_t);
lib.spu94_process.restype = None
lib.spu94_process.argtypes = [c_void_p,
                              POINTER(c_int16), POINTER(c_int16),
                              POINTER(c_int16), POINTER(c_int16),
                              c_uint32]

# void spu94_destroy(spu94_state *);
lib.spu94_destroy.restype = None
lib.spu94_destroy.argtypes = [c_void_p]

# Hand-synced with include/spu94/spu94.h (Phase 6 will auto-sync).
SPU94_STATE_SIZE_MAX = 16384
SPU94_PRESET_HALL = 5
BLOCK = 1024
WARMUP = 1000
MEASURE = 100_000

# Aligned state buffer + work buffer. ctypes arrays are heap-backed but
# that's fine -- the benchmark measures ONLY the spu94_process calls;
# outer setup/teardown are excluded from the timing window.
state_buf = (ctypes.c_ubyte * SPU94_STATE_SIZE_MAX)()
work_buf = (ctypes.c_ubyte * (64 * 1024))()

state = lib.spu94_init(ctypes.addressof(state_buf), SPU94_STATE_SIZE_MAX,
                       ctypes.addressof(work_buf), 64 * 1024)
if not state:
    print("FAIL: spu94_init returned NULL", file=sys.stderr)
    sys.exit(1)

if lib.spu94_load_preset(state, SPU94_PRESET_HALL) != 0:
    print("FAIL: spu94_load_preset returned non-OK", file=sys.stderr)
    sys.exit(1)

Lin = (c_int16 * BLOCK)()
Rin = (c_int16 * BLOCK)()
Lout = (c_int16 * BLOCK)()
Rout = (c_int16 * BLOCK)()
for i in range(BLOCK):
    Lin[i] = (i * 17) % 32767 - 16383
    Rin[i] = (i * 31) % 32767 - 16383


def one_call():
    t0 = time.perf_counter_ns()
    lib.spu94_process(state, Lin, Rin, Lout, Rout, BLOCK)
    return time.perf_counter_ns() - t0


# Warmup: 1000 calls to populate icache / dcache / TLBs.
for _ in range(WARMUP):
    one_call()

# Measurement window: 100_000 calls.
samples = [one_call() for _ in range(MEASURE)]

samples_sorted = sorted(samples)
median_ns = samples_sorted[len(samples_sorted) // 2]
p99_ns = samples_sorted[int(len(samples_sorted) * 0.99)]
max_ns = samples_sorted[-1]
mean_ns = statistics.fmean(samples)

ratio = (p99_ns - median_ns) / median_ns if median_ns > 0 else float("inf")

print(f"bench_latency: N={MEASURE} block={BLOCK} samples")
print(f"  median={median_ns} ns  mean={mean_ns:.0f} ns  "
      f"p99={p99_ns} ns  max={max_ns} ns")
print(f"  ratio=(p99-median)/median = {ratio:.3f}  threshold = {THRESHOLD}")

lib.spu94_destroy(state)

if ratio > THRESHOLD:
    print(f"FAIL: ratio {ratio:.3f} exceeds threshold {THRESHOLD}",
          file=sys.stderr)
    sys.exit(1)
print("PASS: latency variance within bound")
sys.exit(0)
