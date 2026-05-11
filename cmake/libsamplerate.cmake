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
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        2ccde9568cca73c7b32c97fefca2e418c16ae5e3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(libsamplerate)
