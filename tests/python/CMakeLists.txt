# tests/python/CMakeLists.txt
# Phase 2 Plan 05 Task 3: ROADMAP Phase 2 SC 3 -- 10^6-step fuzz harness of
# the BufferAddress wrap invariant. Pre-Phase-6 setup -- no full Python
# wheel; ctypes against the built .so.

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME fuzz_buffer
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_buffer.py
)

# Pitfall 7 mitigation: the $<TARGET_FILE:spu94_shared> generator expression
# resolves to the just-built .so path; CMake re-evaluates on every test
# invocation. The harness is therefore guaranteed to load the current build,
# never a stale one from a prior cmake run.
set_tests_properties(fuzz_buffer PROPERTIES
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
)

# Phase 3 Plan 04 Task 2: ROADMAP Phase 3 SC-4 -- 10^6-step fuzz of the
# full reverb network. Same SPU94_LIB plumbing pattern (generator
# expression ensures the test picks up the fresh .so every run,
# Pitfall-7 mitigation from Phase 2 Plan 05).
add_test(
    NAME fuzz_reverb
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_reverb.py
)

set_tests_properties(fuzz_reverb PROPERTIES
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
)

# Phase 4 Plan 04 Task 2 (D-16): 10^6-step fuzz of the full 44.1 kHz FIR
# chain via spu94_fir_chain_step_reverb_bypass. Same SPU94_LIB plumbing
# pattern (Pitfall-7 mitigation); dual labels so `ctest -L fir` and
# `ctest -L fuzz` both pick it up.
add_test(
    NAME fuzz_fir
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_fir.py
)

set_tests_properties(fuzz_fir PROPERTIES
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
    LABELS "fuzz;fir"
)

# Phase 5 Plan 01 Task 3 (D-07): cell-equality verifier over the two preset
# audit CSVs (BIB-011 nocash vs BIB-012 hitmen). Reads CSVs only -- no
# SPU94_LIB dependency. Permanent regression gate on the audit artifacts.
add_test(
    NAME verify_preset_sources
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_preset_sources.py
)

set_tests_properties(verify_preset_sources PROPERTIES
    LABELS "preset"
)

# Phase 5 Plan 05 Task 1 (D-10a): 10^6-step random-walk fuzz of the public
# spu94_process + spu94_flush + spu94_load_preset entries with interleaved
# mid-stream register writes. Extends the Phase 2/3/4 fuzz pattern to the
# public-API layer. Same SPU94_LIB plumbing (Pitfall-7 mitigation); dual
# labels so `ctest -L process` and `ctest -L fuzz` both pick it up.
add_test(
    NAME fuzz_process
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_process.py
)

set_tests_properties(fuzz_process PROPERTIES
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
    LABELS "fuzz;process"
    TIMEOUT 1200
)
# TIMEOUT 1200 (20 min): measured 595 s wall clock on dev workstation
# (~1680 ops/s); ctypes overhead on the 5 public-API entries dominates
# over the Phase 2/3/4 fuzz harnesses which call only one internal
# per-sample step function. 1200 s is 2x observed runtime, giving
# slower CI runners headroom without masking a future regression
# that doubles the per-step cost.

# Phase 6 Plan 1: binding unit tests (pytest-based; ctest label "binding").
add_subdirectory(binding)
