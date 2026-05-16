add_executable(test_voice_tick test_voice_tick.c)
target_link_libraries(test_voice_tick PRIVATE unity spu94_static)
add_test(NAME voice_tick_unit COMMAND test_voice_tick)

add_executable(test_adsr test_adsr.c)
target_link_libraries(test_adsr PRIVATE unity spu94_static)
add_test(NAME adsr_unit COMMAND test_adsr)

add_executable(test_sample_loader test_sample_loader.c)
target_link_libraries(test_sample_loader PRIVATE unity spu94_static)
add_test(NAME sample_loader_unit COMMAND test_sample_loader)
