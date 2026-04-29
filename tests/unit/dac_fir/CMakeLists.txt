# tests/unit/dac_fir/CMakeLists.txt -- Phase 6 Plan 01 Task 3
#
# DAC interpolation filter test TUs. Four test dimensions:
#   1. Coefficient table integrity (symmetry, zeros, center taps, sums)
#   2. Accumulator overflow proof (adversarial input per stage)
#   3. DC gain through cascade
#   4. Impulse response sanity

add_executable(test_dac_fir_coef_table test_dac_fir_coef_table.c)
target_link_libraries(test_dac_fir_coef_table PRIVATE unity spu94_static)
add_test(NAME dac_fir_coef_table COMMAND test_dac_fir_coef_table)
set_tests_properties(dac_fir_coef_table PROPERTIES LABELS "dac_fir")

add_executable(test_dac_fir_overflow_proof test_dac_fir_overflow_proof.c)
target_link_libraries(test_dac_fir_overflow_proof PRIVATE unity spu94_static)
add_test(NAME dac_fir_overflow_proof COMMAND test_dac_fir_overflow_proof)
set_tests_properties(dac_fir_overflow_proof PROPERTIES LABELS "dac_fir")

add_executable(test_dac_fir_dc_gain test_dac_fir_dc_gain.c)
target_link_libraries(test_dac_fir_dc_gain PRIVATE unity spu94_static)
add_test(NAME dac_fir_dc_gain COMMAND test_dac_fir_dc_gain)
set_tests_properties(dac_fir_dc_gain PROPERTIES LABELS "dac_fir")

add_executable(test_dac_fir_impulse test_dac_fir_impulse.c)
target_link_libraries(test_dac_fir_impulse PRIVATE unity spu94_static)
add_test(NAME dac_fir_impulse COMMAND test_dac_fir_impulse)
set_tests_properties(dac_fir_impulse PROPERTIES LABELS "dac_fir")
