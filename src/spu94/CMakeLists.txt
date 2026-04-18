# src/spu94/CMakeLists.txt
# spu94_obj compiles each source file ONCE; spu94_shared and spu94_static
# link the same objects. Guarantees flag-identical artifacts (BUILD-01, BUILD-02).

add_library(spu94_obj OBJECT
    spu94_placeholder.c
)

# PUBLIC include dirs: downstream consumers of spu94_shared/static see them too.
target_include_directories(spu94_obj PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Determinism + warnings: PRIVATE — they apply when compiling spu94_obj,
# but do NOT leak to downstream targets. Critical per RESEARCH.md Pitfall 7.
target_link_libraries(spu94_obj PRIVATE spu94_warnings)

# Shared library consumer.
add_library(spu94_shared SHARED $<TARGET_OBJECTS:spu94_obj>)
target_link_libraries(spu94_shared PUBLIC spu94_obj)
set_target_properties(spu94_shared PROPERTIES OUTPUT_NAME spu94)

# Static library consumer.
add_library(spu94_static STATIC $<TARGET_OBJECTS:spu94_obj>)
target_link_libraries(spu94_static PUBLIC spu94_obj)
set_target_properties(spu94_static PROPERTIES OUTPUT_NAME spu94)
