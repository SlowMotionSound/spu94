# tests/cli/CMakeLists.txt — Phase 6 Plan 3 Task 1.
# ctest wiring for the pytest-based CLI integration tests plus the CLI-03
# regression gate (nm audit of libspu94.so).

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

set(_cli_tests
    test_cli_preset_hall_roundtrip
    test_cli_config_and_list
    test_cli_error_paths
)

foreach(test_name IN LISTS _cli_tests)
    add_test(
        NAME ${test_name}
        COMMAND ${Python3_EXECUTABLE} -m pytest
                ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.py
                -v --no-header --tb=short
    )
    set_tests_properties(${test_name} PROPERTIES
        LABELS "cli"
    )
endforeach()

# CLI-03 regression gate: libspu94.so must NOT contain any drwav_ or jsmn_
# symbols. The vendored single-header libraries are CLI-linked only.
add_test(
    NAME verify_no_drwav_in_libspu94
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/verify-no-drwav-in-libspu94.sh
                 $<TARGET_FILE:spu94_shared>
)
set_tests_properties(verify_no_drwav_in_libspu94 PROPERTIES
    LABELS "cli"
)
