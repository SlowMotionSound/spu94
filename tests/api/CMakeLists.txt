# tests/api/CMakeLists.txt
# API-07: prove spu94.h is C99-pedantic-clean and C++-extern-C-clean.
# Both consumers are compile-only tests -- they link against spu94_static to
# confirm symbols resolve, but the main() bodies do nothing meaningful.
# CTest passes if the executables build AND run (exit 0).

# ---- C99 consumer: compile the public header under strict C99 pedantic. ----
add_executable(test_c99_consumer c99_consumer.c)
# C99 + pedantic + Werror -- this overrides the project-wide C11 setting for
# this target only. API-07 requires the header to be C99-valid (independent of
# the project's own C11 build standard). C_EXTENSIONS OFF disables -std=gnu99
# fallback; we want pure -std=c99.
set_target_properties(test_c99_consumer PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_compile_options(test_c99_consumer PRIVATE -pedantic -Werror -Wall -Wextra)
target_link_libraries(test_c99_consumer PRIVATE spu94_static)
add_test(NAME api_c99_consumer COMMAND test_c99_consumer)

# ---- C++ consumer: prove extern "C" wrapping works. ----
enable_language(CXX)
add_executable(test_cxx_consumer cxx_consumer.cpp)
set_target_properties(test_cxx_consumer PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_compile_options(test_cxx_consumer PRIVATE -pedantic -Werror -Wall -Wextra)
target_link_libraries(test_cxx_consumer PRIVATE spu94_static)
add_test(NAME api_cxx_consumer COMMAND test_cxx_consumer)
