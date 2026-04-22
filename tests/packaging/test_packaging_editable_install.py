"""PYBIND-06 part 1: pip install -e . works; import works without SPU94_LIB."""
import os
import subprocess
import sys
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]

def test_pyproject_toml_present():
    pp = REPO_ROOT / "pyproject.toml"
    assert pp.exists(), f"pyproject.toml missing at {pp}"
    content = pp.read_text()
    # Sanity checks on the key fields.
    for s in (
        'name = "spu94"',
        'requires-python = ">=3.10"',
        '"spu94.cli:main"',
        'wheel.py-api = "py3"',
        '"manylinux_2_28"',
    ):
        assert s in content, f"pyproject.toml missing expected field: {s!r}"

def test_skbuild_install_rule_present_library():
    f = REPO_ROOT / "src" / "spu94" / "CMakeLists.txt"
    content = f.read_text()
    assert "SKBUILD_PROJECT_NAME" in content
    assert "install(TARGETS spu94_shared" in content

def test_skbuild_install_rule_present_cli():
    f = REPO_ROOT / "src" / "cli" / "CMakeLists.txt"
    content = f.read_text()
    assert "SKBUILD_PROJECT_NAME" in content
    assert "install(TARGETS spu94_cli" in content

@pytest.mark.skipif(
    os.environ.get("SPU94_SKIP_PIP_INSTALL") == "1",
    reason="SPU94_SKIP_PIP_INSTALL=1 (CI skip toggle)",
)
def test_editable_install_in_venv(tmp_path):
    """Create a fresh venv, pip install -e ., verify `import spu94` works
    without SPU94_LIB."""
    venv = tmp_path / "venv"
    subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    py = venv / "bin" / "python"

    # Install build backend + this package in editable mode.
    # --no-build-isolation keeps us using the already-installed scikit-build-core
    # if the dev host has it; --no-isolation otherwise. Use --no-cache-dir to
    # keep the test hermetic.
    env = {
        **os.environ,
        "PIP_NO_BUILD_ISOLATION": "0",  # let pip install build deps in ephemeral env
    }
    # Drop any SPU94_LIB set by the dev shell — the install should make it unneeded.
    env.pop("SPU94_LIB", None)

    # Install scikit-build-core + this package.
    res = subprocess.run(
        [str(py), "-m", "pip", "install", "--no-cache-dir",
         "scikit-build-core>=0.10", "numpy>=1.23", "cmake>=3.20"],
        env=env, capture_output=True, text=True,
    )
    if res.returncode != 0:
        pytest.fail(f"dev deps install failed:\n{res.stdout}\n{res.stderr}")

    res = subprocess.run(
        [str(py), "-m", "pip", "install", "--no-cache-dir", "-e", str(REPO_ROOT)],
        env=env, capture_output=True, text=True,
    )
    if res.returncode != 0:
        pytest.fail(f"editable install failed:\n{res.stdout}\n{res.stderr}")

    # Import without SPU94_LIB set — should find libspu94.so next to __init__.py.
    res = subprocess.run(
        [str(py), "-c",
         "import spu94; print(spu94.SPU94_REG__COUNT, spu94.SPU94_PRESET__COUNT)"],
        env=env, capture_output=True, text=True,
    )
    assert res.returncode == 0, (
        f"import spu94 failed after editable install:\n{res.stdout}\n{res.stderr}"
    )
    assert "35 10" in res.stdout

    # self_test runs.
    res = subprocess.run(
        [str(py), "-c", "import spu94; spu94.self_test()"],
        env=env, capture_output=True, text=True,
    )
    assert res.returncode == 0, f"self_test failed: {res.stderr}"

    # spu94 CLI is on PATH (pip auto-generated the script).
    spu94_bin = venv / "bin" / "spu94"
    assert spu94_bin.exists(), f"pip did not install [project.scripts] entry"
    res = subprocess.run(
        [str(spu94_bin), "--list-presets"],
        env=env, capture_output=True, text=True,
    )
    assert res.returncode == 0, f"spu94 --list-presets failed: {res.stderr}"
    names = [line.strip() for line in res.stdout.splitlines() if line.strip()]
    assert len(names) == 10
