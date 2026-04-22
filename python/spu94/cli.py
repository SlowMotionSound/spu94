"""python/spu94/cli.py

Phase 6 Plan 2 Task 2: entry_point shim for the compiled ``spu94`` binary.

``pip install spu94`` registers ``spu94 = "spu94.cli:main"`` in
``[project.scripts]`` (wired by Plan 4's ``pyproject.toml``). Running
``spu94 ...`` from the shell invokes this ``main()``, which locates the
compiled binary next to ``__init__.py`` and ``os.execv``'s to it. Exit
codes pass through naturally — the Python interpreter is replaced by
the binary, so ``$?`` reflects the binary's actual exit status.

The compiled binary is delivered by Plan 3 (``src/cli/main.c``) and
installed inside the package directory by Plan 4's CMake
``install(TARGETS spu94_cli RUNTIME DESTINATION "${SKBUILD_PROJECT_NAME}")``
rule.

The shim itself is intentionally minimal: locate → exec → fail fast
with a polished stderr message if the binary is missing. No argv
inspection, no flag handling — the binary owns all of that (Plan 3's
dr_wav + jsmn + getopt_long machinery).
"""
import os
import sys
from pathlib import Path


def _resolve_binary() -> Path | None:
    """Return an absolute ``Path`` to the compiled ``spu94`` binary, or
    ``None`` if not found.

    Search order mirrors the library resolution in ``_binding.py``:
      1. ``Path(__file__).parent / 'spu94'`` (wheel-install layout —
         binary dropped next to ``__init__.py``). Also checks
         ``spu94.exe`` for Windows-style installs.
      2. scikit-build-core editable-install layout: each ``sys.path``
         entry gets a ``/spu94/spu94`` probe. ``pip install -e .`` puts
         the source tree on sys.path but drops the compiled binary
         inside site-packages/spu94/.

    Never returns a bare ``spu94`` shell-PATH resolution — the caller
    owns PATH and a stale system ``spu94`` binary would shadow the one
    installed with the Python package.
    """
    here = Path(__file__).resolve().parent
    candidates = [here / "spu94", here / "spu94.exe"]

    for entry in sys.path:
        if not entry:
            continue
        candidates.append(Path(entry) / "spu94" / "spu94")
        candidates.append(Path(entry) / "spu94" / "spu94.exe")

    for cand in candidates:
        if cand.exists():
            return cand.resolve()
    return None


def main() -> None:
    """Locate the compiled ``spu94`` binary and ``os.execv`` to it.

    On failure (binary missing, permissions issue), prints a one-line
    error to stderr and ``sys.exit(1)``s.
    """
    binary = _resolve_binary()
    if binary is None:
        here = Path(__file__).resolve().parent
        print(
            f"spu94: error: compiled binary not found at {here / 'spu94'}. "
            f"The wheel install may be corrupted; try: "
            f"pip install --force-reinstall spu94",
            file=sys.stderr,
        )
        sys.exit(1)
    # os.execv replaces the Python interpreter with the binary. argv[0]
    # is the program name (standard convention); argv[1:] are user args.
    try:
        os.execv(str(binary), [str(binary), *sys.argv[1:]])
    except OSError as e:
        print(
            f"spu94: error: failed to exec {binary}: {e}",
            file=sys.stderr,
        )
        sys.exit(1)
