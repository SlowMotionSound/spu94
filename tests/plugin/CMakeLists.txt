# tests/plugin/CMakeLists.txt
# Phase 22 / PLUG-15 headless null-test harness. Avoids needing a real
# DAW (Ardour/Reaper) to verify the SRC sandwich + latency reporting math.
#
# The test instantiates SrcChain directly (the same class PluginProcessor
# uses), drives the spu94 C core in pure dry-passthrough configuration,
# shifts the dry input by the latency the plugin reports, sums against
# the wet output, and measures the residual RMS in dBFS. PLUG-15's pass
# criterion is residual <= -60 dBFS at 48 kHz host SR.

juce_add_console_app(test_null_passthrough
    PRODUCT_NAME "SPU-94 Null Test"
)
juce_generate_juce_header(test_null_passthrough)

target_sources(test_null_passthrough PRIVATE
    test_null_passthrough.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

target_include_directories(test_null_passthrough PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

# Match the plugin target's JUCE feature flags so juce_core doesn't pull in
# curl / web-browser symbols (the test never touches network/UI).
target_compile_definitions(test_null_passthrough PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

# libsamplerate is already pulled by src/plugin/CMakeLists.txt; FetchContent
# is idempotent on the same target name, so re-include is a no-op.
include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_null_passthrough PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
)

add_test(NAME plug15_null_passthrough_48k
         COMMAND test_null_passthrough)

# Phase 24 Plan 01: StateSerializer binary container unit tests (PLUG-22..27)
juce_add_console_app(test_state_serializer
    PRODUCT_NAME "SPU-94 State Serializer Test"
)
juce_generate_juce_header(test_state_serializer)

target_sources(test_state_serializer PRIVATE
    test_state_serializer.cpp
)

target_include_directories(test_state_serializer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_state_serializer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(test_state_serializer PRIVATE
    spu94_static
    juce::juce_core
    juce::juce_recommended_config_flags
)

add_test(NAME state_serializer
         COMMAND test_state_serializer)

# Phase 24 PLUG-26/27: Full processor state round-trip test.
# Instantiates SPU94AudioProcessor directly to verify save→load
# preserves all 9 param values, and two instances retain independent state.
juce_add_console_app(test_state_roundtrip
    PRODUCT_NAME "SPU-94 State Roundtrip Test"
)
juce_generate_juce_header(test_state_roundtrip)

target_sources(test_state_roundtrip PRIVATE
    test_state_roundtrip.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

# Standalone-only WavLoader bolt-on (needed by PluginProcessor.cpp includes)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_state_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_state_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_state_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_state_roundtrip PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_state_roundtrip PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

add_test(NAME state_roundtrip
         COMMAND test_state_roundtrip)
