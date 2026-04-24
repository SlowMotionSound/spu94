"""tests/python/test_hotpath_alloc_gate_meta.py -- Phase 7 Plan 05 (BUILD-06 D-20).

Negative meta-test wrapper for tests/rt_safety/hotpath_alloc_gate.sh.

The ctest layer already exercises both cases (hotpath_alloc_gate +
hotpath_alloc_gate_negative with WILL_FAIL TRUE), but this pytest file
makes the "gate detects real malloc hits" claim directly assertable:

  1. test_clean_target_passes: running the gate against the allocator-
     clean binary exits 0 and prints the canonical PASS line.
  2. test_with_malloc_target_fails: running the same gate wrapper
     against the intentionally-poisoned binary (hotpath_alloc_gate_
     target_with_malloc.c) exits non-zero AND names at least one of
     the filtered heap syscalls in stderr.

If test 2 ever passes silently, the gate has rotted -- a parser bug
or a filter gap is letting real heap hits through. That would be a
merge blocker, not a noisy false positive.
"""
import os
import subprocess
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
_GATE = ["bash", str(_REPO_ROOT / "tests" / "rt_safety" / "hotpath_alloc_gate.sh")]


def _find(name: str) -> str:
    """Locate a ctest-built binary under the primary build/ tree.

    Prefers the canonical ``build/`` directory so stale sibling build
    trees (``build-ubsan/``, ``build-clang-*/``, ``build-release/``) can
    never shadow the primary tree via lexicographic sort order. Falls
    back to a repo-wide ``build*/`` scan only when ``build/`` is absent,
    and asserts on ambiguity so a caller sees the full hit list rather
    than silently picking the first sorted match.
    """
    primary = _REPO_ROOT / "build"
    if primary.is_dir():
        hits = sorted(primary.rglob(name))
    else:
        hits = sorted(_REPO_ROOT.rglob(f"build*/{'/'.join(['**', name])}"))
    assert hits, (
        f"{name} not built under build/. Run `cmake --build build` and retry."
    )
    assert len(hits) == 1, (
        f"ambiguous: {len(hits)} candidates for {name}: "
        f"{[str(p) for p in hits]}. Remove stale build dirs and retry."
    )
    return str(hits[0])


def _run_gate(bin_path: str) -> subprocess.CompletedProcess:
    env = {**os.environ, "SYSCALLS_BIN": bin_path, "STRACE_EXE": "strace"}
    return subprocess.run(_GATE, env=env, capture_output=True, check=False)


def test_clean_target_passes():
    """Gate exits 0 on the allocator-clean target + prints PASS line."""
    r = _run_gate(_find("hotpath_alloc_gate_target"))
    assert r.returncode == 0, (
        f"clean target unexpectedly failed (exit {r.returncode}):\n"
        f"--- stdout ---\n{r.stdout.decode(errors='replace')}\n"
        f"--- stderr ---\n{r.stderr.decode(errors='replace')}"
    )
    assert b"PASS: zero heap syscalls" in r.stdout, (
        f"PASS line missing from stdout: {r.stdout.decode(errors='replace')}"
    )


def test_with_malloc_target_fails():
    """Gate exits non-zero on the malloc-poisoned target + names a heap
    syscall in stderr. Proves the gate actually detects real hits rather
    than passing on every binary (D-20 negative meta-test contract)."""
    r = _run_gate(_find("hotpath_alloc_gate_target_with_malloc"))
    assert r.returncode != 0, (
        "with_malloc target unexpectedly passed -- gate is broken. "
        f"stdout: {r.stdout.decode(errors='replace')}"
    )
    assert b"FAIL" in r.stderr, (
        f"FAIL message missing from stderr: {r.stderr.decode(errors='replace')}"
    )
    # Gate should name at least one of the filtered heap syscalls.
    assert any(s in r.stderr for s in (b"brk", b"mmap", b"munmap", b"mremap")), (
        f"no heap-syscall name in stderr: {r.stderr.decode(errors='replace')}"
    )


if __name__ == "__main__":  # pragma: no cover
    pytest.main([__file__, "-v"])
