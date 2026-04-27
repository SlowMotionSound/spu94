add_executable(test_vag test_vag.c)
target_link_libraries(test_vag PRIVATE unity spu94_static)
add_test(NAME vag_read_write COMMAND test_vag)
