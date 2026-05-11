# cmake/libsamplerate.cmake
# Phase 22 / Task 1: FetchContent shim for libsndfile/libsamplerate.
#
# libsamplerate is the BSD-2-Clause SRC library used for v1.7's
# bidirectional host_SR <-> 44100 conversion at the JUCE wrapper boundary.
# Quality preset SRC_SINC_MEDIUM_QUALITY is selected at SRC_STATE construction
# time inside src/plugin/SrcChain.cpp (NOT here).
#
# Pinned by SHA per project FetchContent discipline (matches
# cmake/clap_juce_extensions.cmake and root CMakeLists.txt's JUCE pin).
# SHA resolved 2026-05-11 via:
#   git ls-remote https://github.com/libsndfile/libsamplerate.git HEAD
#
# Future swap path: if a future Linux runner ships libsamplerate via apt and
# we want to prefer system packages, replace this FetchContent block with
#   find_package(SampleRate REQUIRED)
# and update src/plugin/CMakeLists.txt's link line to use SampleRate::samplerate.

include(FetchContent)

# Force libsamplerate's own build to skip tools/tests/examples so the
# fetch is fast and produces only the libsamplerate static target.
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL  OFF CACHE BOOL "" FORCE)

# Save and restore project-level BUILD_TESTING around the FetchContent so we
# only disable libsamplerate's tests (it reads BUILD_TESTING during its own
# include(CTest)), without poisoning the cache and disabling THIS project's
# tests. Caught during Phase 22 UAT: the previous CACHE FORCE form killed
# every gsd/ctest test in this repo because libsamplerate.cmake is included
# from src/plugin/CMakeLists.txt BEFORE add_subdirectory(tests) runs.
set(_spu94_save_BUILD_TESTING "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        2ccde9568cca73c7b32c97fefca2e418c16ae5e3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(libsamplerate)

# Restore parent project's BUILD_TESTING (FORCE is needed because we forced
# above; restore the original value, defaulting to ON which is CTest's
# default when unset).
if(NOT DEFINED _spu94_save_BUILD_TESTING OR "${_spu94_save_BUILD_TESTING}" STREQUAL "")
    set(_spu94_save_BUILD_TESTING ON)
endif()
set(BUILD_TESTING "${_spu94_save_BUILD_TESTING}" CACHE BOOL "" FORCE)
unset(_spu94_save_BUILD_TESTING)
