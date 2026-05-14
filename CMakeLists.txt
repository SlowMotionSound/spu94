cmake_minimum_required(VERSION 3.22)
project(spu94
    VERSION 0.1.0
    DESCRIPTION "Bit-faithful PS1 SPU reverb DSP"
    LANGUAGES C CXX
)

# Force C11 project-wide; no compiler-extension fallback.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++17 for JUCE standalone target (Phase 8).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit compile_commands.json for static analysis + flag verification (Plan 02).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Position-independent code so the OBJECT library can feed SHARED later.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SPU94_BUILD_GUI "Build JUCE standalone and plugin targets" ON)

if(SPU94_BUILD_GUI)
    # --- JUCE 8.0.12 via FetchContent (Phase 8: standalone GUI) ---
    # Pin to the 8.0.12 release tag SHA for byte-reproducibility.
    # Resolved via: git ls-remote https://github.com/juce-framework/JUCE.git refs/tags/8.0.12
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        29396c22c93392d6738e021b83196283d6e4d850
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# Include the warnings/determinism INTERFACE target definition.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/spu94_warnings.cmake)

# CTest must be enabled at the top level, BEFORE add_subdirectory(tests).
include(CTest)
enable_testing()

add_subdirectory(src/spu94)
add_subdirectory(src/cli)
if(SPU94_BUILD_GUI)
    add_subdirectory(src/plugin)
    add_subdirectory(src/standalone)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
