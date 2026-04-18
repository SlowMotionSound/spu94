add_executable(test_q15 test_q15.c)
# Test links the static library (so we can test link-level correctness later in Plan 02 UBSan job)
target_link_libraries(test_q15
    PRIVATE
        unity
        spu94_static
)
add_test(NAME q15_unit COMMAND test_q15)
