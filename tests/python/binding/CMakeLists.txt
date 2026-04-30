# tests/python/binding/CMakeLists.txt
# Phase 6 Plan 1: ctest wiring for the new pytest-based binding unit tests.
# Each file gets its own add_test() so `ctest -R test_binding_surface` works
# piecewise; label "binding" groups them for the Phase 6 quick loop.
#
# Pitfall-7 mitigation (from Phase 2 Plan 05): the generator expression
# $<TARGET_FILE:spu94_shared> resolves at test-invocation time, so the
# harness always loads the just-built .so — never a stale one.

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

# The binding pytest suite. Plan 1 shipped the first four tests; Plan 2
# adds test_binding_numpy_contract for PYBIND-02 (numpy ndpointer
# argtype overrides + strict int16 contract validation + SPU94 class +
# cli shim missing-binary path).
set(_binding_tests
    test_binding_surface
    test_binding_register_intenum
    test_binding_preset_table
    test_binding_drift_detection
    test_binding_numpy_contract
    test_binding_adpcm
    test_binding_mixer_dac
)

foreach(test_name IN LISTS _binding_tests)
    add_test(
        NAME ${test_name}
        COMMAND ${Python3_EXECUTABLE} -m pytest
                ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.py
                -v --no-header --tb=short
    )
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
        LABELS "binding"
    )
endforeach()
