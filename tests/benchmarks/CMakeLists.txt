# tests/benchmarks/CMakeLists.txt -- Phase 7 Plan 05 (BUILD-06).
#
# Report-only timing harness via pytest-benchmark. D-20: never gates CI
# on timing noise; ctest runs the harness but only to exercise it.
# Actual reporting in CI is via the benchmark-report job (separate
# continue-on-error job that uploads the JSON as an artifact).
#
# Conditional on Python3 (same pattern as tests/python/CMakeLists.txt)
# so hosts without Python skip the benchmark without breaking the build.

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_benchmark.py")
    add_test(
        NAME bench_process
        COMMAND ${Python3_EXECUTABLE} -m pytest
                ${CMAKE_CURRENT_SOURCE_DIR}/test_benchmark.py
                --benchmark-json=${CMAKE_BINARY_DIR}/bench-ci.json
                -q
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    # Pitfall-7 mitigation: $<TARGET_FILE:spu94_shared> resolves at test-
    # invocation time so the harness always loads the just-built .so.
    # Matches the env-var pattern in tests/python/CMakeLists.txt.
    set_tests_properties(bench_process PROPERTIES
        LABELS "benchmark;report_only"
        ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
        # Benchmark is slow by design (warmups + multiple rounds at
        # 4096-sample blocks over 10 presets = ~70s on dev host).
        TIMEOUT 600)
elseif(NOT Python3_Interpreter_FOUND)
    message(STATUS "Phase 7: Python3 3.10+ not found; bench_process skipped")
endif()
