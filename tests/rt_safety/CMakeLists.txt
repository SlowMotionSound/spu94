# tests/rt_safety/CMakeLists.txt -- Phase 5 Plan 04.
#
# Four permanent RT-safety CI gates; one test target per axis (D-09e):
#   rt_no_heap       -- D-09a: no malloc/calloc/realloc/free in link closure
#   rt_no_locks      -- D-09b: no pthread/sem/futex in link closure
#   rt_no_syscalls   -- D-09c: zero syscalls in 10^5-iter spu94_process loop
#   rt_bench_latency -- D-09d: (p99-median)/median <= THRESHOLD bounded jitter
#
# Per-axis failure diagnosis (D-09e) preserved: a failure points directly at
# the broken property. The four targets share label "rt_safety" for
# `ctest -L rt_safety` discovery.
#
# Conditional skips:
#   Hosts without strace (non-Linux) skip rt_no_syscalls gracefully.
#   Hosts without Python3 skip rt_bench_latency gracefully.
# The two linker-symbol axes (rt_no_heap, rt_no_locks) run everywhere nm is
# available (all M1 development targets).

# -------------------------------------------------------------------------
# test_phase5_linksym: Phase 5 public-symbol link-closure target.
# -------------------------------------------------------------------------
# Small C harness that references all three Phase 5 public symbols
# (spu94_process, spu94_flush, spu94_load_preset). Static-linking
# against spu94_static pulls every Phase 5 code path + its transitive
# helpers into this binary's link closure; nm -u audits against that
# closure in test_no_heap.sh and verify-no-locks.sh.
#
# Rationale: libspu94.so may hide an internal helper that IS reachable
# from Phase 5 symbols but is NOT on the shared-lib's dynamic undefined-
# symbol list. Auditing the static-linked binary catches those.
add_executable(test_phase5_linksym test_phase5_linksym.c)
target_link_libraries(test_phase5_linksym PRIVATE spu94_static spu94_warnings)
target_include_directories(test_phase5_linksym PRIVATE
    ${CMAKE_SOURCE_DIR}/include)

# -------------------------------------------------------------------------
# D-09a: rt_no_heap. Script reads SPU94_LIB + PHASE5_BIN env vars.
# -------------------------------------------------------------------------
add_test(NAME rt_no_heap
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_no_heap.sh)
set_tests_properties(rt_no_heap PROPERTIES
    LABELS "rt_safety"
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>;PHASE5_BIN=$<TARGET_FILE:test_phase5_linksym>")

# -------------------------------------------------------------------------
# D-09b: rt_no_locks. Same env vars as rt_no_heap.
# -------------------------------------------------------------------------
add_test(NAME rt_no_locks
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/verify-no-locks.sh)
set_tests_properties(rt_no_locks PROPERTIES
    LABELS "rt_safety"
    ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>;PHASE5_BIN=$<TARGET_FILE:test_phase5_linksym>")

# -------------------------------------------------------------------------
# D-09c: rt_no_syscalls. Conditional on strace availability AND on Task 2
# having landed the binary+script source files. Task 1 (this file's first
# commit) creates only the linker-symbol halves; Task 2 creates
# test_no_syscalls.c + test_no_syscalls.sh and re-runs cmake to activate
# this block.
# -------------------------------------------------------------------------
find_program(STRACE_EXE strace)
if(STRACE_EXE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_no_syscalls.c"
              AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_no_syscalls.sh")
    add_executable(test_no_syscalls test_no_syscalls.c)
    target_link_libraries(test_no_syscalls PRIVATE spu94_static spu94_warnings)
    target_include_directories(test_no_syscalls PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME rt_no_syscalls
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_no_syscalls.sh)
    set_tests_properties(rt_no_syscalls PROPERTIES
        LABELS "rt_safety"
        ENVIRONMENT "SYSCALLS_BIN=$<TARGET_FILE:test_no_syscalls>;STRACE_EXE=${STRACE_EXE}")
elseif(NOT STRACE_EXE)
    message(STATUS "Phase 5: strace not found; rt_no_syscalls test skipped")
else()
    message(STATUS "Phase 5: Task 2 source files not yet present; rt_no_syscalls test deferred")
endif()

# -------------------------------------------------------------------------
# D-09d: rt_bench_latency. Conditional on Python3 3.10+ AND on Task 2
# having landed bench_latency.py. Accepts optional THRESHOLD via the
# RT_LATENCY_THRESHOLD cache variable. Default pinned to 2.0 by
# ADR-Phase-5-E per the measure-then-pin protocol:
# dev-host measurement = 0.741; pinned = max(2.0, 2 * observed) = 2.0.
# -------------------------------------------------------------------------
find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench_latency.py")
    set(RT_LATENCY_THRESHOLD "2.0" CACHE STRING
        "D-09d (p99-median)/median bound; pinned by ADR-Phase-5-E (measure-then-pin)")
    add_test(NAME rt_bench_latency
             COMMAND Python3::Interpreter
                     ${CMAKE_CURRENT_SOURCE_DIR}/bench_latency.py
                     ${RT_LATENCY_THRESHOLD})
    set_tests_properties(rt_bench_latency PROPERTIES
        LABELS "rt_safety"
        ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>")
elseif(NOT Python3_Interpreter_FOUND)
    message(STATUS "Phase 5: Python3 3.10+ not found; rt_bench_latency test skipped")
else()
    message(STATUS "Phase 5: Task 2 bench_latency.py not yet present; rt_bench_latency test deferred")
endif()
