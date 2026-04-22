# tests/docs/CMakeLists.txt
# Phase 6 Plan 5 Task 1: DOCS-04 regression gate — README.md structure +
# content validator. Runs `scripts/ci/verify-readme-sections.sh` via a
# pytest wrapper so the failure surface is consistent with the rest of
# the python-side test suites.
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME test_readme_sections
    COMMAND ${Python3_EXECUTABLE} -m pytest
            ${CMAKE_CURRENT_SOURCE_DIR}/test_readme_sections.py
            -v --no-header --tb=short
)
set_tests_properties(test_readme_sections PROPERTIES
    LABELS "docs"
)
