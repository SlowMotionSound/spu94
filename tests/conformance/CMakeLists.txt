# tests/conformance/CMakeLists.txt — Phase 7 Plan 01 Task 3 Step 3.
#
# Wires the COVERAGE.md structural integrity meta-test + the
# scripts/ci/test_check_coverage.py validator meta-tests into ctest.
#
# Two add_test() targets:
#   - coverage_map_integrity: parses docs/COVERAGE.md, asserts the three
#     section headers + pinned wayback URL + >=35 per-register rows +
#     every non-Known-Gaps row has a non-empty test: cell + all 35
#     spu94_reg_t names appear.
#   - test_check_coverage: runs the 5 meta-tests for the D-02 validator
#     (1 positive, 4 negative: missing file, fake ctest name, empty
#     cell outside Known Gaps, shell-metacharacter injection).
#
# Both use pytest; Python 3.10+ is already required elsewhere in the
# project (find_package above in tests/CMakeLists.txt's parent scope).

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME coverage_map_integrity
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_CURRENT_SOURCE_DIR}/test_coverage_map_integrity.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME test_check_coverage
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_SOURCE_DIR}/scripts/ci/test_check_coverage.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
