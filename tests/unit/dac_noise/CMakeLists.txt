# tests/unit/dac_noise/CMakeLists.txt -- Phase 6 Plan 02 Task 2
#
# DAC noise model tests: LFSR behavior, spectral slope, amplitude.

add_executable(test_dac_noise_lfsr test_dac_noise_lfsr.c)
target_link_libraries(test_dac_noise_lfsr PRIVATE unity spu94_static)
add_test(NAME dac_noise_lfsr COMMAND test_dac_noise_lfsr)
set_tests_properties(dac_noise_lfsr PROPERTIES LABELS "dac_noise")

add_executable(test_dac_noise_spectral test_dac_noise_spectral.c)
target_link_libraries(test_dac_noise_spectral PRIVATE unity spu94_static m)
add_test(NAME dac_noise_spectral COMMAND test_dac_noise_spectral)
set_tests_properties(dac_noise_spectral PROPERTIES LABELS "dac_noise")

add_executable(test_dac_noise_amplitude test_dac_noise_amplitude.c)
target_link_libraries(test_dac_noise_amplitude PRIVATE unity spu94_static m)
add_test(NAME dac_noise_amplitude COMMAND test_dac_noise_amplitude)
set_tests_properties(dac_noise_amplitude PROPERTIES LABELS "dac_noise")
