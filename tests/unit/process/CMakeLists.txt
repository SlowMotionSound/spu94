# tests/unit/process/CMakeLists.txt
# Phase 5 Plan 02 Task 2: public block-based audio entry point tests.
# Three concerns split into three TUs:
#   test_process_basic    -- block-loop correctness, NULL-safety, zero-length,
#                            impulse peak near SPU94_LATENCY_SAMPLES, in-place
#   test_process_flush    -- drain semantics: NULL-safe, zero-length,
#                            Off-preset-equivalent produces all-zero tail
#   test_process_mix_bus  -- D-05 mailbox: init-zero, reset-clears,
#                            tick observes mailbox writes (overflow_magnitude
#                            proof)
#
# All three TUs reach spu94_state_internal.h via the src/spu94 include dir
# below -- the mailbox-proof test needs struct field access. Follows the
# same pattern as tests/unit/reverb/test_reverb_body.c which also includes
# the internal header via a relative path.

add_executable(test_process_basic test_process_basic.c)
target_link_libraries(test_process_basic PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_basic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_basic COMMAND test_process_basic)
set_tests_properties(test_process_basic PROPERTIES LABELS "process")

add_executable(test_process_flush test_process_flush.c)
target_link_libraries(test_process_flush PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_flush PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_flush COMMAND test_process_flush)
set_tests_properties(test_process_flush PROPERTIES LABELS "process")

add_executable(test_process_mix_bus test_process_mix_bus.c)
target_link_libraries(test_process_mix_bus PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_mix_bus PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_mix_bus COMMAND test_process_mix_bus)
set_tests_properties(test_process_mix_bus PROPERTIES LABELS "process")

# Phase 5 Plan 05 Task 2: API-03 D-03/D-04 contract closure.
#   test_process_block_size -- block-size invariance sweep across
#     {1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096}. Reference is block-1.
#   test_process_in_place   -- in-place (L_out == L_in, R_out == R_in)
#     bit-identical to out-of-place baseline under matched initial state.
# Both load the Hall preset on fresh state + tick once to commit
# TICK_LATCHED registers before driving deterministic pseudo-input.

add_executable(test_process_block_size test_process_block_size.c)
target_link_libraries(test_process_block_size PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_block_size PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_block_size COMMAND test_process_block_size)
set_tests_properties(test_process_block_size PROPERTIES LABELS "process")

add_executable(test_process_in_place test_process_in_place.c)
target_link_libraries(test_process_in_place PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_in_place PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_in_place COMMAND test_process_in_place)
set_tests_properties(test_process_in_place PROPERTIES LABELS "process")

# Phase 6 Plan 06 (ADR-Phase-6-G): end-to-end reverb-audibility regression
# gate. Would have caught the Phase 4/5 wiring bug documented in
# .planning/debug/resolved/reverb-not-in-audio-path.md: the test asserts
# (1) Hall preset output is materially different from the Off-preset
# reference, (2) Off + noise -> silence at 44.1 kHz (wet-only wiring),
# (3) Hall's decay tail extends deep into spu94_flush.
add_executable(test_process_reverb_audible test_process_reverb_audible.c)
target_link_libraries(test_process_reverb_audible PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_reverb_audible PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_reverb_audible COMMAND test_process_reverb_audible)
set_tests_properties(test_process_reverb_audible PROPERTIES LABELS "process")

# CR-01 regression: Hall output level must scale linearly with input
# level. The input_scale bug (raw widening multiply instead of Q15)
# pinned output at ~-5 dBFS regardless of input amplitude. This test
# feeds Hall at full / half / quarter amplitudes and checks the peak
# ratio falls in a linear band (2.5..5.0 for full/quarter).
add_executable(test_process_reverb_linearity test_process_reverb_linearity.c)
target_link_libraries(test_process_reverb_linearity PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_reverb_linearity PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_reverb_linearity COMMAND test_process_reverb_linearity)
set_tests_properties(test_process_reverb_linearity PROPERTIES LABELS "process")

# Step 13 / M1 close-out: external-anchor test.
# Off preset + impulse input must produce all-zero output (algebra: every
# gain register in Off is 0x0000, so the SPU reverb's output equation
# evaluates to identically zero for any input). Catches the class of bug
# where SPU-94 is reproducible-but-wrong — the witness-diff harness,
# golden files, and modulation harness all confirm reproducibility but
# none of them confirms correctness against an externally-derived value.
add_executable(test_process_external_anchor_off test_process_external_anchor_off.c)
target_link_libraries(test_process_external_anchor_off PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_external_anchor_off PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
add_test(NAME test_process_external_anchor_off COMMAND test_process_external_anchor_off)
set_tests_properties(test_process_external_anchor_off PROPERTIES LABELS "process")

# Phase 2 (ADPCM-INT): integration tests for ADPCM pipeline wiring.
# Covers: toggle (INT-01), double-buffer latency (INT-02), latency report
# (INT-03), state management (INT-04), default-off regression (INT-05),
# state size cap (INT-05).
add_executable(test_process_adpcm test_process_adpcm.c)
target_link_libraries(test_process_adpcm PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_adpcm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_adpcm COMMAND test_process_adpcm)
set_tests_properties(test_process_adpcm PROPERTIES LABELS "process;adpcm_integration")

# Phase 7: mixer bus routing and fader math integration tests.
add_executable(test_process_mixer test_process_mixer.c)
target_link_libraries(test_process_mixer PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_mixer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_mixer COMMAND test_process_mixer)
set_tests_properties(test_process_mixer PROPERTIES LABELS "process;mixer")

# Phase 7: DAC toggle hierarchy and processing integration tests.
add_executable(test_process_dac_integration test_process_dac_integration.c)
target_link_libraries(test_process_dac_integration PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_dac_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_dac_integration COMMAND test_process_dac_integration)
set_tests_properties(test_process_dac_integration PROPERTIES LABELS "process;dac_integration")

# Phase 7: latency compensation delay buffer integration tests.
add_executable(test_process_latency_comp test_process_latency_comp.c)
target_link_libraries(test_process_latency_comp PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_latency_comp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_latency_comp COMMAND test_process_latency_comp)
set_tests_properties(test_process_latency_comp PROPERTIES LABELS "process;mixer")

# Phase 9: DAC toggle transitions, reverb interaction, ADPCM composition.
add_executable(test_process_dac_toggle_transitions test_process_dac_toggle_transitions.c)
target_link_libraries(test_process_dac_toggle_transitions PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_dac_toggle_transitions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_dac_toggle_transitions COMMAND test_process_dac_toggle_transitions)
set_tests_properties(test_process_dac_toggle_transitions PROPERTIES LABELS "process;dac_integration")

# Phase 11: A/B mode toggle (v1.2/v1.3) + latency verification.
add_executable(test_process_dac_mode_toggle test_process_dac_mode_toggle.c)
target_link_libraries(test_process_dac_mode_toggle PRIVATE unity spu94_static spu94_warnings)
target_include_directories(test_process_dac_mode_toggle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/spu94)
add_test(NAME test_process_dac_mode_toggle COMMAND test_process_dac_mode_toggle)
set_tests_properties(test_process_dac_mode_toggle PROPERTIES LABELS "process;dac_integration")
