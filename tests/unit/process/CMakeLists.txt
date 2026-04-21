# tests/unit/process/CMakeLists.txt
# Phase 5 Plan 02 Task 2: public block-based audio entry point tests.
# Three concerns split into three TUs:
#   test_process_basic    -- block-loop correctness, NULL-safety, zero-length,
#                            impulse peak near SPU94_LATENCY_SAMPLES, in-place
#   test_process_flush    -- drain semantics: NULL-safe, zero-length,
#                            Off-preset-equivalent produces all-zero tail
#   test_process_mix_bus  -- D-05 mailbox: init-zero, reset-clears,
#                            tick observes mailbox writes (overflow_magnitude
#                            proof)
#
# All three TUs reach spu94_state_internal.h via the src/spu94 include dir
# below -- the mailbox-proof test needs struct field access. Follows the
# same pattern as tests/unit/reverb/test_reverb_body.c which also includes
# the internal header via a relative path.

add_executable(test_process_basic test_process_basic.c)
target_link_libraries(test_process_basic PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_basic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_basic COMMAND test_process_basic)
set_tests_properties(test_process_basic PROPERTIES LABELS "process")

add_executable(test_process_flush test_process_flush.c)
target_link_libraries(test_process_flush PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_flush PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_flush COMMAND test_process_flush)
set_tests_properties(test_process_flush PROPERTIES LABELS "process")

add_executable(test_process_mix_bus test_process_mix_bus.c)
target_link_libraries(test_process_mix_bus PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_mix_bus PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_mix_bus COMMAND test_process_mix_bus)
set_tests_properties(test_process_mix_bus PROPERTIES LABELS "process")

# Phase 5 Plan 05 Task 2: API-03 D-03/D-04 contract closure.
#   test_process_block_size -- block-size invariance sweep across
#     {1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096}. Reference is block-1.
#   test_process_in_place   -- in-place (L_out == L_in, R_out == R_in)
#     bit-identical to out-of-place baseline under matched initial state.
# Both load the Hall preset on fresh state + tick once to commit
# TICK_LATCHED registers before driving deterministic pseudo-input.

add_executable(test_process_block_size test_process_block_size.c)
target_link_libraries(test_process_block_size PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_block_size PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_block_size COMMAND test_process_block_size)
set_tests_properties(test_process_block_size PROPERTIES LABELS "process")

add_executable(test_process_in_place test_process_in_place.c)
target_link_libraries(test_process_in_place PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_in_place PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_in_place COMMAND test_process_in_place)
set_tests_properties(test_process_in_place PROPERTIES LABELS "process")
