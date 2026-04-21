# tests/fixtures/CMakeLists.txt — Phase 6 Plan 3 Task 1.
#
# Generate sample_1s_stereo_44k.wav at configure/build time and copy the
# static JSON fixtures into the build dir so the CLI pytest suite finds
# them at a stable path regardless of source-tree structure.

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

set(_fixture_wav ${CMAKE_CURRENT_BINARY_DIR}/sample_1s_stereo_44k.wav)

add_custom_command(
    OUTPUT ${_fixture_wav}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/generate_fixtures.py
            --out ${_fixture_wav}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate_fixtures.py
    COMMENT "Generating 1-second stereo 44.1 kHz fixture WAV"
    VERBATIM
)

add_custom_target(spu94_cli_fixtures ALL DEPENDS ${_fixture_wav})

# Copy static JSON fixtures into the build tree so the test conftest can
# find them at a predictable location.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/sample_override_hall.json
    ${CMAKE_CURRENT_BINARY_DIR}/sample_override_hall.json
    COPYONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/sample_flat_registermap.json
    ${CMAKE_CURRENT_BINARY_DIR}/sample_flat_registermap.json
    COPYONLY
)
