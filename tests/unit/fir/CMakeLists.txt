# tests/unit/fir/CMakeLists.txt -- Phase 4 Plan 01 Task 3
#
# Phase 4 FIR test TUs. Plan 01 lands test_fir_coef_table (table
# integrity). Plans 02/03 append test_fir_decimate, test_fir_interpolate,
# test_fir_bit_identity, test_fir_overflow_proof, test_fir_impulse,
# test_fir_dc, test_fir_freq_sweep, test_fir_round_trip, test_fir_latency,
# test_fir_err_overflow_taps (per 04-RESEARCH Architecture Patterns --
# one TU per test dimension, not per stage).

add_executable(test_fir_coef_table test_fir_coef_table.c)
target_link_libraries(test_fir_coef_table PRIVATE unity spu94_static)
add_test(NAME fir_coef_table COMMAND test_fir_coef_table)
set_tests_properties(fir_coef_table PROPERTIES LABELS "fir")

add_executable(test_fir_bit_identity test_fir_bit_identity.c)
target_link_libraries(test_fir_bit_identity PRIVATE unity spu94_static)
add_test(NAME fir_bit_identity COMMAND test_fir_bit_identity)
set_tests_properties(fir_bit_identity PROPERTIES LABELS "fir")

add_executable(test_fir_overflow_proof test_fir_overflow_proof.c)
target_link_libraries(test_fir_overflow_proof PRIVATE unity spu94_static)
add_test(NAME fir_overflow_proof COMMAND test_fir_overflow_proof)
set_tests_properties(fir_overflow_proof PROPERTIES LABELS "fir")

add_executable(test_fir_decimate test_fir_decimate.c)
target_link_libraries(test_fir_decimate PRIVATE unity spu94_static)
add_test(NAME fir_decimate COMMAND test_fir_decimate)
set_tests_properties(fir_decimate PROPERTIES LABELS "fir")

add_executable(test_fir_interpolate test_fir_interpolate.c)
target_link_libraries(test_fir_interpolate PRIVATE unity spu94_static)
add_test(NAME fir_interpolate COMMAND test_fir_interpolate)
set_tests_properties(fir_interpolate PROPERTIES LABELS "fir")

add_executable(test_fir_chain_latency test_fir_chain_latency.c)
target_link_libraries(test_fir_chain_latency PRIVATE unity spu94_static)
add_test(NAME fir_chain_latency COMMAND test_fir_chain_latency)
set_tests_properties(fir_chain_latency PROPERTIES LABELS "fir")

add_executable(test_fir_impulse test_fir_impulse.c)
target_link_libraries(test_fir_impulse PRIVATE unity spu94_static)
add_test(NAME fir_impulse COMMAND test_fir_impulse)
set_tests_properties(fir_impulse PROPERTIES LABELS "fir")

add_executable(test_fir_dc test_fir_dc.c)
target_link_libraries(test_fir_dc PRIVATE unity spu94_static)
add_test(NAME fir_dc COMMAND test_fir_dc)
set_tests_properties(fir_dc PROPERTIES LABELS "fir")
