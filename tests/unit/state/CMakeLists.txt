add_executable(test_state_lifecycle test_state_lifecycle.c)
target_link_libraries(test_state_lifecycle
    PRIVATE
        unity
        spu94_static
)
add_test(NAME state_lifecycle COMMAND test_state_lifecycle)

# ADR-0023 (M1 close-out Step 4): observable OOB counter.
add_executable(test_error_counters test_error_counters.c)
target_link_libraries(test_error_counters
    PRIVATE
        unity
        spu94_static
)
add_test(NAME error_counters COMMAND test_error_counters)
