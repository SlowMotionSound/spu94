add_executable(test_register_identity test_register_identity.c)
target_link_libraries(test_register_identity
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_identity_unit COMMAND test_register_identity)
