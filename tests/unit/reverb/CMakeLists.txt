# tests/unit/reverb/CMakeLists.txt — Phase 3 Plan 01 Task 3
#
# Per-stage unit tests for the Plan 01 reverb stages. Each TU includes
# the src-only internal header via relative path (same pattern as Phase 2
# buffer tests reaching spu94_state_internal.h).

add_executable(test_reverb_hard_clip test_reverb_hard_clip.c)
target_link_libraries(test_reverb_hard_clip PRIVATE unity spu94_static)
add_test(NAME reverb_hard_clip COMMAND test_reverb_hard_clip)

add_executable(test_reverb_input_scale test_reverb_input_scale.c)
target_link_libraries(test_reverb_input_scale PRIVATE unity spu94_static)
add_test(NAME reverb_input_scale COMMAND test_reverb_input_scale)

add_executable(test_reverb_output_scale test_reverb_output_scale.c)
target_link_libraries(test_reverb_output_scale PRIVATE unity spu94_static)
add_test(NAME reverb_output_scale COMMAND test_reverb_output_scale)
