# src/spu94/CMakeLists.txt
# spu94_obj compiles each source file ONCE; spu94_shared and spu94_static
# link the same objects. Guarantees flag-identical artifacts (BUILD-01, BUILD-02).

add_library(spu94_obj OBJECT
    spu94_state.c
    spu94_registers.c
    spu94_register_io.c
    spu94_write_policy.c
    spu94_pending.c
    spu94_reverb.c
    spu94_tick.c
    spu94_buffer.c
    spu94_fir_coef.c
    spu94_fir.c
    spu94_io_chain.c
    spu94_process.c
    spu94_slew.c
    spu94_presets.c
    spu94_preset_io.c
    spu94_interp.c
    spu94_adpcm.c
    spu94_adpcm_encode.c
    spu94_gauss.c
    vag.c
    spu94_dac_fir.c
    spu94_dac_fir_coef.c
    spu94_dac_noise.c
    spu94_voice.c
    spu94_sample_loader.c
)

# PUBLIC include dirs: downstream consumers of spu94_shared/static see them too.
target_include_directories(spu94_obj PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Determinism + warnings: PRIVATE — they apply when compiling spu94_obj,
# but do NOT leak to downstream targets. Critical per RESEARCH.md Pitfall 7.
if(WIN32)
    target_link_libraries(spu94_obj PRIVATE spu94_warnings)
else()
    target_link_libraries(spu94_obj PRIVATE spu94_warnings m)
endif()

# Shared library consumer.
add_library(spu94_shared SHARED $<TARGET_OBJECTS:spu94_obj>)
target_link_libraries(spu94_shared PUBLIC spu94_obj)
set_target_properties(spu94_shared PROPERTIES OUTPUT_NAME spu94)

# Static library consumer.
add_library(spu94_static STATIC $<TARGET_OBJECTS:spu94_obj>)
target_link_libraries(spu94_static PUBLIC spu94_obj)
# On Windows, the shared lib's import library (spu94.lib) collides with the
# static library (spu94.lib). Use a distinct name to avoid the clash.
if(WIN32)
    set_target_properties(spu94_static PROPERTIES OUTPUT_NAME spu94_static)
else()
    set_target_properties(spu94_static PROPERTIES OUTPUT_NAME spu94)
endif()

# Phase 6 Plan 4: install libspu94.so into the Python package directory
# at wheel-build time. SKBUILD_PROJECT_NAME is set by scikit-build-core;
# absent during plain `cmake -B build` so developer flows are unaffected.
if(DEFINED SKBUILD_PROJECT_NAME)
    install(TARGETS spu94_shared
            LIBRARY DESTINATION "${SKBUILD_PROJECT_NAME}"
    )
endif()
