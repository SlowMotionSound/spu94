add_executable(test_buffer_basic test_buffer_basic.c)
target_link_libraries(test_buffer_basic
    PRIVATE
        unity
        spu94_static
)
add_test(NAME buffer_basic_unit COMMAND test_buffer_basic)
