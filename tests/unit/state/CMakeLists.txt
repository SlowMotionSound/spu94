add_executable(test_state_lifecycle test_state_lifecycle.c)
target_link_libraries(test_state_lifecycle
    PRIVATE
        unity
        spu94_static
)
add_test(NAME state_lifecycle COMMAND test_state_lifecycle)
