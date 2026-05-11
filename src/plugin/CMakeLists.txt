# src/plugin/CMakeLists.txt -- v1.7 DAW plugin target
# Phase 21: target renamed from spu94_standalone -> spu94_plugin.
# WavLoader is now a testbed-only source pulled in from src/standalone/ when
# the Standalone format is in FORMATS. All other source files moved here from
# the old src/standalone/ via git mv (PLUG-50).
#
# FORMATS list is per-OS-gated (PLUG-01..05):
#   Linux:   Standalone + VST3 + LV2 (+ CLAP via shim)
#   macOS:   Standalone + VST3 + AU  (+ CLAP via shim)
#   Windows: Standalone + VST3       (+ CLAP via shim)
# LV2 is intentionally Linux-only -- macOS/Windows hosts do not scan LV2.
#
# AU plugin codes (locked for Phase 25 auval setup -- DO NOT CHANGE without
# regenerating the macOS AU cache):
#   PLUGIN_MANUFACTURER_CODE = Spu9   (carried forward from v1.6 standalone)
#   PLUGIN_CODE              = Sp94   (replaces v1.6 placeholder Spv1)

set(SPU94_FORMATS Standalone VST3)
if(UNIX AND NOT APPLE)
    list(APPEND SPU94_FORMATS LV2)
endif()
if(APPLE)
    list(APPEND SPU94_FORMATS AU)
endif()

juce_add_plugin(spu94_plugin
    PRODUCT_NAME                "SPU-94"
    COMPANY_NAME                "SPU-94 Project"
    BUNDLE_ID                   "com.spu94project.spu94"
    PLUGIN_NAME                 "SPU-94"
    PLUGIN_MANUFACTURER_CODE    Spu9
    PLUGIN_CODE                 Sp94
    FORMATS                     ${SPU94_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Reverb
    LV2URI                      "https://spu94project.org/spu94"
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
# Pulled in only when Standalone is one of the requested FORMATS. Include path
# lets #include "WavLoader.h" continue to resolve without source edits.
if("Standalone" IN_LIST SPU94_FORMATS)
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
        JUCE_VST3_CAN_REPLACE_VST2=0
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

# CLAP wrapper via clap-juce-extensions (FetchContent shim).
# Isolated in cmake/clap_juce_extensions.cmake so the JUCE-9-native-CLAP
# swap when it arrives is a single-file change.
include(${CMAKE_SOURCE_DIR}/cmake/clap_juce_extensions.cmake)
clap_juce_extensions_plugin(TARGET   spu94_plugin
                            CLAP_ID  "com.spu94project.spu94"
                            CLAP_FEATURES audio-effect stereo reverb)
