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
