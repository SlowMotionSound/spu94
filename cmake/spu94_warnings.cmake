# spu94_warnings.cmake
# Single source of truth for SPU-94's determinism + warning flag set.
# Linked PRIVATE into spu94_obj (never PUBLIC/INTERFACE on obj — would leak
# into downstream consumers per Pitfall 7 in RESEARCH.md).
# Consumed by future phases: tests, MCU cross-compile (Phase 8), Python wheel (Phase 6).

add_library(spu94_warnings INTERFACE)

target_compile_options(spu94_warnings INTERFACE
    # Error on any warning — BUILD-02 requirement.
    -Werror
    # Warning set agreed for Phase 1 (see RESEARCH.md §Architecture Patterns).
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wstrict-prototypes
    -Wmissing-prototypes
    # Determinism flags (BUILD-02). Defense-in-depth against accidental float
    # introduction even though BUILD-07 grep guard also forbids float/double.
    -ffp-contract=off
    -fno-fast-math
)
