add_executable(test_interp test_interp.c)
target_link_libraries(test_interp PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_interp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_interp COMMAND test_interp)
set_tests_properties(test_interp PROPERTIES LABELS "interp")
