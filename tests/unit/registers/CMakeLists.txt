add_executable(test_register_identity test_register_identity.c)
target_link_libraries(test_register_identity
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_identity_unit COMMAND test_register_identity)

add_executable(test_register_io test_register_io.c)
target_link_libraries(test_register_io
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_io_unit COMMAND test_register_io)

add_executable(test_register_facade test_register_facade.c)
target_link_libraries(test_register_facade
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_facade_unit COMMAND test_register_facade)

# Phase 2 Plan 05 Task 1: TEST-02 register battery -- four TUs covering all
# 35 registers across round-trip, signedness/type-mismatch enforcement,
# write-timing policy (IMMEDIATE vs TICK_LATCHED), and edge values.

add_executable(test_register_roundtrip test_register_roundtrip.c)
target_link_libraries(test_register_roundtrip
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_roundtrip COMMAND test_register_roundtrip)

add_executable(test_register_types test_register_types.c)
target_link_libraries(test_register_types
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_types COMMAND test_register_types)

add_executable(test_register_policy test_register_policy.c)
target_link_libraries(test_register_policy
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_policy COMMAND test_register_policy)

add_executable(test_register_edges test_register_edges.c)
target_link_libraries(test_register_edges
    PRIVATE
        unity
        spu94_static
)
add_test(NAME register_edges COMMAND test_register_edges)
