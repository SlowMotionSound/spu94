# src/cli/CMakeLists.txt — Phase 6 Plan 3
# Builds the spu94 CLI binary. Vendored dr_wav + jsmn are included PRIVATEly
# so they never leak to consumers that link spu94_shared (Pitfall 8).
#
# The binary is installed into the Python package directory ONLY when
# scikit-build-core is driving (SKBUILD_PROJECT_NAME defined). Plain
# `cmake -B build && cmake --build build` leaves the binary in
# build/src/cli/spu94 for dev / ctest use.

add_executable(spu94_cli
    main.c
    cmd_reverb.c
    cmd_adpcm.c
    wav_io.c
    json_config.c
    preset_names.c
)

# Link the shared library (NOT the static) so CLI distribution matches
# wheel layout. Plan 4's install rules drop libspu94.so next to the
# binary in the wheel; $ORIGIN RPATH finds it.
target_link_libraries(spu94_cli PRIVATE spu94_shared)

# PRIVATE include dirs — dr_wav.h and jsmn.h are visible only inside
# this target's TUs; no transitive leak.
target_include_directories(spu94_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/dr_wav
    ${CMAKE_SOURCE_DIR}/vendor/jsmn
)

# The CLI needs the SPU-94 public headers (same as any consumer).
target_include_directories(spu94_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Rename to plain `spu94` — the wheel-installed binary (and the dev
# `build/src/cli/spu94` path) both need this exact name because the
# Python entry_point shim looks for a file named `spu94` (or `spu94.exe`).
set_target_properties(spu94_cli PROPERTIES OUTPUT_NAME spu94)

# Warnings policy:
# The core library builds under the project-wide -Werror set in
# spu94_warnings (including -Wconversion, -Wsign-conversion, -Wpedantic).
# Vendored third-party headers (dr_wav v0.14.6 is ~9000 lines of
# idiomatic but not -Wconversion-clean C) would trip that policy. We
# apply a RELAXED warning set target-locally: still -Wall -Wextra, still
# -Werror on errors we own, but drop the conversion and pedantic flags
# so the vendored headers compile clean. Our own CLI TUs are small
# enough (<300 lines each) that spot-review covers the conversion
# diagnostics the flag would have surfaced.
target_compile_options(spu94_cli PRIVATE
    -Wall
    -Wextra
    -Werror
    # Tempered determinism: we still forbid float contraction / fast-math
    # in the CLI because it processes the same int16 PCM values the core
    # library did, and any sneaky float path would be a bug.
    -ffp-contract=off
    -fno-fast-math
)

# $ORIGIN RPATH for wheel install (Plan 4 layout — libspu94.so next to
# the binary). macOS branch added for future portability.
if(APPLE)
    set(_spu94_rpath_origin "@loader_path")
else()
    set(_spu94_rpath_origin "$ORIGIN")
endif()
set_property(TARGET spu94_cli PROPERTY INSTALL_RPATH "${_spu94_rpath_origin}")

# Install rule — only when scikit-build-core is driving.
if(DEFINED SKBUILD_PROJECT_NAME)
    install(TARGETS spu94_cli
            RUNTIME DESTINATION "${SKBUILD_PROJECT_NAME}"
    )
endif()
