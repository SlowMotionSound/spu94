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
