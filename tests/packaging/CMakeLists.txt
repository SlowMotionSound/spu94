# Phase 6 Plan 4: packaging smoke tests — editable install + wheel build.
# Both tests are SLOW (build a wheel from source takes ~60 s); they run
# under the `packaging` label and are excluded from the quick-loop
# `ctest -L binding|cli` run.

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

set(_packaging_tests
    test_packaging_editable_install
    test_packaging_wheel_tag
)

foreach(test_name IN LISTS _packaging_tests)
    add_test(
        NAME ${test_name}
        COMMAND ${Python3_EXECUTABLE} -m pytest
                ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.py
                -v --no-header --tb=short
        # Run from the repo root so pip install -e . sees the pyproject.toml.
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(${test_name} PROPERTIES
        LABELS "packaging"
        # Packaging tests can take a while; give them headroom.
        TIMEOUT 300
    )
endforeach()
