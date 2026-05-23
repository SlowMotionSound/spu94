add_executable(test_voice_tick test_voice_tick.c)
target_link_libraries(test_voice_tick PRIVATE unity spu94_static)
add_test(NAME voice_tick_unit COMMAND test_voice_tick)

add_executable(test_noise_gen test_noise_gen.c)
target_link_libraries(test_noise_gen PRIVATE unity spu94_static)
add_test(NAME noise_unit COMMAND test_noise_gen)

add_executable(test_adsr test_adsr.c)
target_link_libraries(test_adsr PRIVATE unity spu94_static)
add_test(NAME adsr_unit COMMAND test_adsr)

add_executable(test_sweep test_sweep.c)
target_link_libraries(test_sweep PRIVATE unity spu94_static)
add_test(NAME sweep_unit COMMAND test_sweep)

add_executable(test_sample_loader test_sample_loader.c)
target_link_libraries(test_sample_loader PRIVATE unity spu94_static)
add_test(NAME sample_loader_unit COMMAND test_sample_loader)
