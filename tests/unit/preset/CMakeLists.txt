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
