cmake_minimum_required(VERSION 3.20)
project(spu94
    VERSION 0.1.0
    DESCRIPTION "Bit-faithful PS1 SPU reverb DSP"
    LANGUAGES C
)

# Force C11 project-wide; no compiler-extension fallback.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Emit compile_commands.json for static analysis + flag verification (Plan 02).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Position-independent code so the OBJECT library can feed SHARED later.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Include the warnings/determinism INTERFACE target definition.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/spu94_warnings.cmake)

# CTest must be enabled at the top level, BEFORE add_subdirectory(tests).
include(CTest)
enable_testing()

add_subdirectory(src/spu94)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
