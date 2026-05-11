# src/plugin/CMakeLists.txt -- v1.7 DAW plugin target
# Phase 21 / Task 1: target renamed from spu94_standalone -> spu94_plugin.
# WavLoader is now a testbed-only source pulled in from src/standalone/ when
# the Standalone format is in FORMATS. All other source files moved here from
# the old src/standalone/ via git mv (PLUG-50).
# FORMATS remains Standalone for Task 1; Task 2 expands it to the full
# per-OS-gated list (VST3 + LV2 [Linux] + CLAP [via shim] + AU [macOS] + Standalone).

juce_add_plugin(spu94_plugin
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

juce_generate_juce_header(spu94_plugin)

target_sources(spu94_plugin
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
        ParameterBridge.cpp
        RegisterPanel.cpp
        MorphPanel.cpp
)

# Standalone-only WavLoader bolt-on -- testbed-only source lives in src/standalone/.
# Added to the plugin target as PRIVATE sources only when Standalone is in FORMATS.
# Include path lets #include "WavLoader.h" continue to resolve without source edits.
get_target_property(_SPU94_FORMATS spu94_plugin JUCE_FORMATS)
if(_SPU94_FORMATS MATCHES "Standalone")
    target_sources(spu94_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp
    )
    target_include_directories(spu94_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone
    )
endif()

target_compile_definitions(spu94_plugin
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(spu94_plugin
    PRIVATE
        spu94_static
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(spu94_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
