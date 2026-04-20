# Build vendored Unity as a static lib we can link into each test TU.
add_library(unity STATIC
    vendor/Unity/unity.c
)
target_include_directories(unity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/Unity
)
# Unity's own source has warnings under our strict warning set; relax for the
# vendored TU ONLY — do NOT link spu94_warnings here. Core library warnings
# remain strict; only third-party vendored Unity is exempt.
# (Intentionally omit target_link_libraries(unity PRIVATE spu94_warnings).)

add_subdirectory(q15)
add_subdirectory(state)
add_subdirectory(registers)
add_subdirectory(buffer)
add_subdirectory(reverb)
add_subdirectory(fir)
