add_subdirectory(unit)
add_subdirectory(api)
add_subdirectory(python)
add_subdirectory(rt_safety)

# Phase 6 Plan 3: CLI tests + shared fixtures
add_subdirectory(fixtures)
add_subdirectory(cli)

# Phase 6 Plan 4: packaging smoke tests
add_subdirectory(packaging)

# Phase 6 Plan 5: DOCS-04 README.md structure + content gate
add_subdirectory(docs)

# Phase 7 Plan 01: docs/COVERAGE.md integrity + check_coverage.py meta-tests
add_subdirectory(conformance)

# Phase 7 Plan 05: pytest-benchmark report-only timing harness (BUILD-06 D-20)
add_subdirectory(benchmarks)
