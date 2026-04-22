"""PYBIND-06 part 2: `python -m build --wheel` produces a wheel with the
expected tag; scripts/ci/verify-wheel-tag.sh accepts it."""
import os
import subprocess
import sys
import zipfile
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]

def test_verify_wheel_tag_script_exists_and_executable():
    script = REPO_ROOT / "scripts" / "ci" / "verify-wheel-tag.sh"
    assert script.exists()
    assert os.access(script, os.X_OK), "verify-wheel-tag.sh is not executable"

def test_verify_wheel_tag_rejects_bad_tag(tmp_path):
    """Construct a fake wheel with a deliberately-wrong Tag line and assert
    verify-wheel-tag.sh fails with exit 1."""
    fake_wheel = tmp_path / "spu94-0.0.1-cp310-cp310-linux_x86_64.whl"
    # Minimum-viable wheel: a ZIP with a .dist-info/WHEEL file.
    with zipfile.ZipFile(fake_wheel, "w") as z:
        z.writestr(
            "spu94-0.0.1.dist-info/WHEEL",
            "Wheel-Version: 1.0\nGenerator: test\n"
            "Root-Is-Purelib: false\n"
            "Tag: cp310-cp310-linux_x86_64\n"  # D-23 violation — not py3-none
        )
    res = subprocess.run(
        [str(REPO_ROOT / "scripts" / "ci" / "verify-wheel-tag.sh"), str(fake_wheel)],
        capture_output=True, text=True,
    )
    assert res.returncode != 0
    assert "py3-none" in res.stderr

def test_verify_wheel_tag_accepts_good_tag_relaxed(tmp_path):
    """Relaxed mode (default) accepts py3-none-linux_x86_64."""
    wheel = tmp_path / "spu94-0.1.0-py3-none-linux_x86_64.whl"
    with zipfile.ZipFile(wheel, "w") as z:
        z.writestr(
            "spu94-0.1.0.dist-info/WHEEL",
            "Wheel-Version: 1.0\nGenerator: test\n"
            "Root-Is-Purelib: false\n"
            "Tag: py3-none-linux_x86_64\n"
        )
    res = subprocess.run(
        [str(REPO_ROOT / "scripts" / "ci" / "verify-wheel-tag.sh"), str(wheel)],
        capture_output=True, text=True,
    )
    assert res.returncode == 0, f"Unexpected failure: {res.stderr}"
    assert "PASS" in res.stdout

def test_verify_wheel_tag_strict_mode(tmp_path):
    """Strict mode requires py3-none-manylinux_2_28_x86_64 exactly."""
    wheel_bad = tmp_path / "wheel1.whl"
    with zipfile.ZipFile(wheel_bad, "w") as z:
        z.writestr(
            "spu94-0.1.0.dist-info/WHEEL",
            "Tag: py3-none-linux_x86_64\n"
        )
    res = subprocess.run(
        [str(REPO_ROOT / "scripts" / "ci" / "verify-wheel-tag.sh"), str(wheel_bad)],
        capture_output=True, text=True,
        env={**os.environ, "SPU94_WHEEL_STRICT": "1"},
    )
    assert res.returncode != 0
    assert "strict mode" in res.stderr

    wheel_good = tmp_path / "wheel2.whl"
    with zipfile.ZipFile(wheel_good, "w") as z:
        z.writestr(
            "spu94-0.1.0.dist-info/WHEEL",
            "Tag: py3-none-manylinux_2_28_x86_64\n"
        )
    res = subprocess.run(
        [str(REPO_ROOT / "scripts" / "ci" / "verify-wheel-tag.sh"), str(wheel_good)],
        capture_output=True, text=True,
        env={**os.environ, "SPU94_WHEEL_STRICT": "1"},
    )
    assert res.returncode == 0

@pytest.mark.skipif(
    os.environ.get("SPU94_SKIP_WHEEL_BUILD") == "1",
    reason="SPU94_SKIP_WHEEL_BUILD=1 (CI skip toggle)",
)
def test_python_m_build_produces_wheel(tmp_path):
    """Run `python -m build --wheel` and assert the output wheel has the
    correct tag and contents. This is the full end-to-end PYBIND-06
    smoke test (without cibuildwheel — that needs Docker)."""
    # Use a fresh venv to avoid polluting the dev environment.
    venv = tmp_path / "buildvenv"
    subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    py = venv / "bin" / "python"

    env = {**os.environ}
    env.pop("SPU94_LIB", None)  # the build shouldn't need this
    # Install build tooling + scikit-build-core. ninja is a scikit-build-core
    # default build backend on Linux; pin it explicitly so --no-isolation
    # doesn't surprise us by asking for a backend pip hasn't seen yet.
    res = subprocess.run(
        [str(py), "-m", "pip", "install", "--no-cache-dir",
         "build", "scikit-build-core>=0.10", "numpy>=1.23",
         "cmake>=3.20", "ninja>=1.5"],
        env=env, capture_output=True, text=True,
    )
    if res.returncode != 0:
        pytest.fail(f"tooling install failed:\n{res.stdout}\n{res.stderr}")

    out_dir = tmp_path / "dist"
    res = subprocess.run(
        [str(py), "-m", "build", "--wheel", "--outdir", str(out_dir),
         "--no-isolation", str(REPO_ROOT)],
        env=env, capture_output=True, text=True,
    )
    if res.returncode != 0:
        pytest.fail(f"wheel build failed:\n{res.stdout}\n{res.stderr}")

    wheels = list(out_dir.glob("spu94-*.whl"))
    assert len(wheels) == 1, f"expected exactly 1 wheel, got {wheels}"
    wheel = wheels[0]

    # Filename shape: spu94-0.1.0-py3-none-linux_x86_64.whl (local) or
    # spu94-0.1.0-py3-none-manylinux_2_28_x86_64.whl (cibuildwheel).
    assert "py3-none" in wheel.name, f"wheel tag drift: {wheel.name}"

    # Contents sanity: libspu94.so + spu94 CLI binary + all 6 Python files.
    with zipfile.ZipFile(wheel) as z:
        names = set(z.namelist())
    # Look for the package-internal files (exact dist-info prefix varies).
    expected_suffixes = [
        "spu94/__init__.py",
        "spu94/_binding.py",
        "spu94/api.py",
        "spu94/reverb.py",
        "spu94/presets.py",
        "spu94/cli.py",
        "spu94/libspu94.so",
        "spu94/spu94",           # the CLI binary
    ]
    for expected in expected_suffixes:
        assert any(n == expected or n.endswith("/" + expected) for n in names), (
            f"wheel missing {expected}; contents: {sorted(names)}"
        )

    # Run verify-wheel-tag.sh against it.
    res = subprocess.run(
        [str(REPO_ROOT / "scripts" / "ci" / "verify-wheel-tag.sh"), str(wheel)],
        capture_output=True, text=True,
    )
    assert res.returncode == 0, f"verify-wheel-tag.sh failed: {res.stderr}"

    # Audit the binary: $ORIGIN RPATH + libspu94.so RPATH.
    with zipfile.ZipFile(wheel) as z:
        cli_bin = [n for n in z.namelist() if n.endswith("spu94/spu94")]
        assert cli_bin
        z.extract(cli_bin[0], tmp_path)
    extracted_cli = tmp_path / cli_bin[0]
    # readelf -d prints dynamic-section entries including RUNPATH/RPATH.
    res = subprocess.run(
        ["readelf", "-d", str(extracted_cli)],
        capture_output=True, text=True,
    )
    if res.returncode == 0:
        # $ORIGIN or /$ORIGIN should appear in the RPATH/RUNPATH line.
        # (cibuildwheel's auditwheel may rewrite to a concrete path.)
        assert ("$ORIGIN" in res.stdout) or ("ORIGIN" in res.stdout), (
            f"CLI binary has no $ORIGIN RPATH — libspu94.so will not be found:\n{res.stdout}"
        )
