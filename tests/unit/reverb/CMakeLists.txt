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

# Plan 02 Task 3: SAME IIR + DIFF IIR bit-exactness + CORE-08 anomaly
# + TEST-06 non-anomaly control. Each TU includes the src-only internal
# header via relative path to reach the non-public stage entry points.
add_executable(test_reverb_same_iir test_reverb_same_iir.c)
target_link_libraries(test_reverb_same_iir PRIVATE unity spu94_static)
add_test(NAME reverb_same_iir COMMAND test_reverb_same_iir)

add_executable(test_reverb_diff_iir test_reverb_diff_iir.c)
target_link_libraries(test_reverb_diff_iir PRIVATE unity spu94_static)
add_test(NAME reverb_diff_iir COMMAND test_reverb_diff_iir)

# Plan 03 Task 3: 4-tap comb (D-07 cascading sat_s16) + APF1 + APF2
# per-stage bit-exactness, Pitfall-7 edge coverage, D-07 distinguishing
# test case pinning the cascading-sat behavior against int32-accumulate.
add_executable(test_reverb_comb test_reverb_comb.c)
target_link_libraries(test_reverb_comb PRIVATE unity spu94_static)
add_test(NAME reverb_comb COMMAND test_reverb_comb)

add_executable(test_reverb_apf1 test_reverb_apf1.c)
target_link_libraries(test_reverb_apf1 PRIVATE unity spu94_static)
add_test(NAME reverb_apf1 COMMAND test_reverb_apf1)

add_executable(test_reverb_apf2 test_reverb_apf2.c)
target_link_libraries(test_reverb_apf2 PRIVATE unity spu94_static)
add_test(NAME reverb_apf2 COMMAND test_reverb_apf2)

# Plan 04 Task 1: TEST-07 fixed-point edge battery. Q15 saturation,
# truncation direction (ADR-0001 re-assertion at stage level), and
# Pitfall-1/7/8 signed-overflow guards at every stage.
add_executable(test_reverb_edges test_reverb_edges.c)
target_link_libraries(test_reverb_edges PRIVATE unity spu94_static)
add_test(NAME reverb_edges COMMAND test_reverb_edges)
