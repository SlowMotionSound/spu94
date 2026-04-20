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
