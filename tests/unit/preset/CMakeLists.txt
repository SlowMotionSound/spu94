# Phase 5 Plan 01 Task 5 Step D: pin the preset table contract.
# Count + names + Off-audit-match + regs[] length.

add_executable(test_preset_table_integrity test_preset_table_integrity.c)
target_link_libraries(test_preset_table_integrity
    PRIVATE
        unity
        spu94_static
)
add_test(NAME preset_table_integrity COMMAND test_preset_table_integrity)
set_tests_properties(preset_table_integrity PROPERTIES LABELS "preset")

# Phase 5 Plan 03 Task 2: D-08 split-policy verification -- every v-prefix
# active immediately after load; every d-prefix and m-prefix staged to pending
# with active-slot unchanged; one spu94_tick commits pending-to-active.
add_executable(test_preset_load_all test_preset_load_all.c)
target_link_libraries(test_preset_load_all
    PRIVATE
        unity
        spu94_static
        spu94_warnings
)
target_include_directories(test_preset_load_all PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_preset_load_all COMMAND test_preset_load_all)
set_tests_properties(test_preset_load_all PROPERTIES LABELS "preset")

# Phase 5 Plan 03 Task 3: SC-2 behavioral proof -- every non-Off preset
# produces non-zero output when driven by noise through spu94_process +
# spu94_flush; Off + silent input produces silent output.
add_executable(test_preset_nonzero_tail test_preset_nonzero_tail.c)
target_link_libraries(test_preset_nonzero_tail
    PRIVATE
        unity
        spu94_static
        spu94_warnings
)
target_include_directories(test_preset_nonzero_tail PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_preset_nonzero_tail COMMAND test_preset_nonzero_tail)
set_tests_properties(test_preset_nonzero_tail PROPERTIES LABELS "preset")

# Phase 13 Plan 01: Save format validation and round-trip scaffold
add_executable(test_preset_roundtrip test_preset_roundtrip.c)
target_link_libraries(test_preset_roundtrip
    PRIVATE
        unity
        spu94_static
        spu94_warnings
)
target_include_directories(test_preset_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_preset_roundtrip COMMAND test_preset_roundtrip)
set_tests_properties(test_preset_roundtrip PROPERTIES LABELS "preset")

# Phase 13 Plan 02: Parser edge-case tests (D-08 missing keys, D-09 unknown keys)
add_executable(test_preset_parse test_preset_parse.c)
target_link_libraries(test_preset_parse
    PRIVATE
        unity
        spu94_static
        spu94_warnings
)
target_include_directories(test_preset_parse PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_preset_parse COMMAND test_preset_parse)
set_tests_properties(test_preset_parse PROPERTIES LABELS "preset")
