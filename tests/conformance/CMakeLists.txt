# tests/conformance/CMakeLists.txt — Phase 7 Plan 01 Task 3 Step 3
#                                     + Plan 02 Task 2 Step 3.
#
# Wires the COVERAGE.md structural integrity meta-test + the
# scripts/ci/test_check_coverage.py validator meta-tests into ctest,
# plus (Plan 02) the golden-file presence test + regenerate_goldens.py
# meta-tests.
#
# Four add_test() targets:
#   - coverage_map_integrity: parses docs/COVERAGE.md, asserts the three
#     section headers + pinned wayback URL + >=35 per-register rows +
#     every non-Known-Gaps row has a non-empty test: cell + all 35
#     spu94_reg_t names appear.
#   - test_check_coverage: runs the 5 meta-tests for the D-02 validator
#     (1 positive, 4 negative: missing file, fake ctest name, empty
#     cell outside Known Gaps, shell-metacharacter injection).
#   - goldens_present: structural — 50 .wav + 50 .sha256 files exist
#     under tests/golden/<preset>/<input>.{wav,sha256}; sidecars are
#     well-formed; 3 spot-check hashes match.
#   - regenerate_goldens_meta: positive (committed corpus passes
#     --check) + negative (1-byte mutation fails --check with correct
#     preset/input pair on stderr).
#
# All use pytest; Python 3.10+ is already required elsewhere in the
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

add_test(
    NAME goldens_present
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_CURRENT_SOURCE_DIR}/test_goldens_present.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME regenerate_goldens_meta
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_SOURCE_DIR}/scripts/test_regenerate_goldens.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Phase 7 Plan 03 Task 3 — witness-diff D-06 determinism meta-test.
# Runs scripts/ci/witness_diff.py twice in a row and asserts the resulting
# witness_report.json is identical (byte-equal or numerically within 1e-9).
# Preconditions (checked inside the test; it self-skips if absent):
#   - scripts/ci/witness_diff_build.sh has run (.LV2_PATH sidecar present)
#   - build/src/cli/spu94 is built
# Labeled `witness` so `ctest -L witness` runs it selectively. The suite
# takes ~60 s (two harness runs × ~30 s each).
add_test(
    NAME witness_determinism
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_SOURCE_DIR}/tests/python/test_witness_determinism.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(witness_determinism PROPERTIES
    LABELS "witness"
    TIMEOUT 600
)

# Phase 7 Plan 04 Task 2 — LEVERS-CATALOG.md structural conformance +
# idempotent-AUTO-writer meta-tests.
#   - levers_catalog_complete: 35 data rows in spu94_reg_t enum order;
#     no empty / placeholder AUTO cells once the writer has run.
#   - levers_catalog_writer: idempotency + HAND-column preservation +
#     HAND-columns-empty-by-default meta-tests for the writer script.
# Both consume tests/python/modulation_report.json, so ctest order
# should run `modulation_harness` before either of these in a clean
# build. In practice the committed report is checked in-tree, so the
# tests pass stand-alone.
add_test(
    NAME levers_catalog_complete
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_CURRENT_SOURCE_DIR}/test_levers_catalog_complete.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(levers_catalog_complete PROPERTIES
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
    LABELS "levers"
)

add_test(
    NAME levers_catalog_writer
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_SOURCE_DIR}/tests/python/test_levers_catalog_writer.py
        -q --no-header
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(levers_catalog_writer PROPERTIES
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>"
    LABELS "levers"
)
