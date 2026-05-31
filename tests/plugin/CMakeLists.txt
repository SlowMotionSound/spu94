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

# Phase 25 / PLUG-32, PLUG-35: bus layout whitelist unit test.
# Instantiates SPU94AudioProcessor and verifies checkBusesLayoutSupported
# returns true for exactly {mono-mono, mono-stereo, stereo-stereo} and
# false for all other configurations.
juce_add_console_app(test_bus_layout
    PRODUCT_NAME "SPU-94 Bus Layout Test"
)
juce_generate_juce_header(test_bus_layout)

target_sources(test_bus_layout PRIVATE
    test_bus_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_bus_layout PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_bus_layout PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_bus_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_bus_layout PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_bus_layout PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

add_test(NAME bus_layout
         COMMAND test_bus_layout)

# Phase 25 / PLUG-34: mono output summing unit test.
# Verifies processBlock with a 1-channel AudioBuffer does not crash,
# produces finite output, and yields non-zero reverb content.
juce_add_console_app(test_mono_sum
    PRODUCT_NAME "SPU-94 Mono Sum Test"
)
juce_generate_juce_header(test_mono_sum)

target_sources(test_mono_sum PRIVATE
    test_mono_sum.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_mono_sum PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_mono_sum PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_mono_sum PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_mono_sum PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_mono_sum PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

add_test(NAME mono_sum
         COMMAND test_mono_sum)

# Phase 60 (VCOUNT-02 / VALLOC-01..03): active-voice-count allocation tests.
# Headless SPU94AudioProcessor; drives allocateVoice via the friend struct
# VoiceAllocTest and observes the voice mixer's pending_kon / pending_koff
# bitmasks. One CTest case per argv selector (argv[1] == case name).
juce_add_console_app(test_voice_alloc
    PRODUCT_NAME "SPU-94 Voice Alloc Test"
)
juce_generate_juce_header(test_voice_alloc)

target_sources(test_voice_alloc PRIVATE
    test_voice_alloc.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_voice_alloc PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_voice_alloc PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_voice_alloc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_voice_alloc PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_voice_alloc PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

# One add_test per case so `ctest -R voice_alloc_<case>` resolves to exactly that case.
add_test(NAME voice_alloc_mono_vs_poly
         COMMAND test_voice_alloc voice_alloc_mono_vs_poly)
add_test(NAME voice_alloc_only_active
         COMMAND test_voice_alloc voice_alloc_only_active)
add_test(NAME voice_alloc_steal_oldest
         COMMAND test_voice_alloc voice_alloc_steal_oldest)
add_test(NAME voice_alloc_mono_takeover
         COMMAND test_voice_alloc voice_alloc_mono_takeover)
add_test(NAME voice_alloc_default24_regression
         COMMAND test_voice_alloc voice_alloc_default24_regression)

# Phase 63 (VCOUNT-04): voice-count text-preset persistence tests. First automated
# coverage of the plugin-layer .spu94 round-trip (savePresetToString /
# loadPresetFromString / getActiveVoiceCount). Headless SPU94AudioProcessor; one
# CTest case per argv selector (argv[1] == case name).
juce_add_console_app(test_voice_persist
    PRODUCT_NAME "SPU-94 Voice Persist Test"
)
juce_generate_juce_header(test_voice_persist)

target_sources(test_voice_persist PRIVATE
    test_voice_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_voice_persist PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_voice_persist PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_voice_persist PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_voice_persist PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_voice_persist PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

# One add_test per case so `ctest -R voice_persist_<case>` resolves to exactly that case.
add_test(NAME voice_persist_roundtrip
         COMMAND test_voice_persist voice_persist_roundtrip)
add_test(NAME voice_persist_backcompat
         COMMAND test_voice_persist voice_persist_backcompat)
add_test(NAME voice_persist_clamp
         COMMAND test_voice_persist voice_persist_clamp)

# Phase 61 (VCTRL-01/02/03 / D-01 / D-06 / Pitfall 4): coherent per-voice control
# fan-out tests. Headless SPU94AudioProcessor; drives applyContinuousVoiceControls
# via the friend struct VoiceControlsTest and observes the voice mixer's
# voices[v].base_vol_l/r, non_flags, pmon_flags. One CTest case per argv selector.
# RED/GREEN: the 6 count-sensitive cases FAIL against Plan 01's no-op stub (this
# build links because the stub resolves the symbol); Plan 02 flips them GREEN.
juce_add_console_app(test_voice_controls
    PRODUCT_NAME "SPU-94 Voice Controls Test"
)
juce_generate_juce_header(test_voice_controls)

target_sources(test_voice_controls PRIVATE
    test_voice_controls.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_voice_controls PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(test_voice_controls PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_include_directories(test_voice_controls PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_voice_controls PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_voice_controls PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

# One add_test per case so `ctest -R voice_controls_<case>` resolves to exactly that case.
add_test(NAME voice_controls_level_all_active
         COMMAND test_voice_controls voice_controls_level_all_active)
add_test(NAME voice_controls_pan_all_active
         COMMAND test_voice_controls voice_controls_pan_all_active)
add_test(NAME voice_controls_non_pmon_all_active
         COMMAND test_voice_controls voice_controls_non_pmon_all_active)
add_test(NAME voice_controls_adsr_shared
         COMMAND test_voice_controls voice_controls_adsr_shared)
add_test(NAME voice_controls_velocity_rides_level
         COMMAND test_voice_controls voice_controls_velocity_rides_level)
add_test(NAME voice_controls_default24_regression
         COMMAND test_voice_controls voice_controls_default24_regression)
add_test(NAME voice_controls_out_of_range_untouched
         COMMAND test_voice_controls voice_controls_out_of_range_untouched)
add_test(NAME voice_controls_sweep_interaction
         COMMAND test_voice_controls voice_controls_sweep_interaction)
