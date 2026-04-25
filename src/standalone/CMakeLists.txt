# src/standalone/CMakeLists.txt -- Phase 8: JUCE standalone GUI
# Produces a directly-launchable Linux executable wrapping libspu94.
# Future plugin-formats phase changes FORMATS to "Standalone VST3 LV2 CLAP".

juce_add_plugin(spu94_standalone
    PRODUCT_NAME                "SPU-94"
    COMPANY_NAME                "SPU-94 Project"
    BUNDLE_ID                   "com.spu94project.spu94standalone"
    PLUGIN_NAME                 "SPU-94"
    PLUGIN_MANUFACTURER_CODE    Spu9
    PLUGIN_CODE                 Spv1
    FORMATS                     Standalone
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_SYNTH                    FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

juce_generate_juce_header(spu94_standalone)

target_sources(spu94_standalone
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
        WavLoader.cpp
        ParameterBridge.cpp
)

target_compile_definitions(spu94_standalone
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(spu94_standalone
    PRIVATE
        spu94_shared
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(spu94_standalone PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
