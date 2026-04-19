add_executable(test_buffer_basic test_buffer_basic.c)
target_link_libraries(test_buffer_basic
    PRIVATE
        unity
        spu94_static
)
add_test(NAME buffer_basic_unit COMMAND test_buffer_basic)

# Phase 2 Plan 05 Task 2: buffer wrap-formula corners + mBASE snap-on-write
# behavior + work-buffer-unchanged invariant. Closes ADR-0006's test
# obligation (snap + no implicit clear) and exercises the formula corners
# reachable via the public u16 mBASE API. The wrap-from-top corner that
# requires brute-forcing 262K ticks is the Python fuzz harness's job.

add_executable(test_buffer_wrap test_buffer_wrap.c)
target_link_libraries(test_buffer_wrap
    PRIVATE
        unity
        spu94_static
)
add_test(NAME buffer_wrap COMMAND test_buffer_wrap)

add_executable(test_buffer_mbase test_buffer_mbase.c)
target_link_libraries(test_buffer_mbase
    PRIVATE
        unity
        spu94_static
)
add_test(NAME buffer_mbase COMMAND test_buffer_mbase)
