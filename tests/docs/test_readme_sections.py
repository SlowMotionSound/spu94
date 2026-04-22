"""DOCS-04: README.md structure + content gate.

Wraps the CI shell script (``scripts/ci/verify-readme-sections.sh``) in a
pytest suite so CI failures surface with a consistent runner and adds a
few Python-level content checks that would be awkward to express in bash.

The shell script is the authoritative checker — this module runs it and
asserts exit 0. A few additional assertions catch regressions in content
the shell script doesn't explicitly inspect (hero-paragraph presence,
minimum length, `--tail-seconds` flag documented, licensing section names
the vendored libs + MIT/Apache deferral).
"""
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_verify_readme_sections_script_passes():
    """Run the CI gate script against the actual README."""
    result = subprocess.run(
        [str(REPO_ROOT / "scripts" / "ci" / "verify-readme-sections.sh"),
         str(REPO_ROOT / "README.md")],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, (
        f"verify-readme-sections.sh failed:\n"
        f"stdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )
    assert "PASS" in result.stdout


def test_readme_exists():
    """README.md must exist at the repo root."""
    assert (REPO_ROOT / "README.md").exists()


def test_readme_hero_paragraph_present():
    """The hero / pitch paragraph appears before the first `## ` heading."""
    text = (REPO_ROOT / "README.md").read_text()
    first_section = text.find("## ")
    assert first_section > 0, "README has no level-2 section headings"
    hero = text[:first_section]
    assert "SPU-94" in hero
    # Pitch paragraph must mention the project's core value.
    assert "PlayStation 1" in hero or "PS1" in hero or "Sony" in hero
    assert "reverb" in hero.lower()


def test_readme_minimum_length():
    """README must be substantial — at least 200 lines to cover all 10 sections."""
    text = (REPO_ROOT / "README.md").read_text()
    assert text.count("\n") >= 200, (
        f"README is too short: {text.count(chr(10))} lines (need >= 200)"
    )


def test_readme_python_example_uses_class():
    """Python walkthrough demonstrates both surfaces — at minimum the class."""
    text = (REPO_ROOT / "README.md").read_text()
    assert "spu94.SPU94" in text or "SPU94()" in text
    # Context-manager usage (recommended pattern).
    assert "with " in text


def test_readme_cli_example_has_tail_seconds():
    """CLI walkthrough shows the tail-seconds flag."""
    text = (REPO_ROOT / "README.md").read_text()
    assert "--tail-seconds" in text


def test_readme_licensing_section_names_vendored_libs():
    """Licensing posture names dr_wav and jsmn (vendored libs)."""
    text = (REPO_ROOT / "README.md").read_text()
    assert "dr_wav" in text
    assert "jsmn" in text
    # Final MIT vs Apache-2.0 deferral mentioned.
    assert ("MIT" in text and "Apache" in text) or "LICENSE" in text


def test_readme_cli_error_examples_match_plan3_contract():
    """CLI walkthrough cites the exact Plan 3 error-shape contract."""
    text = (REPO_ROOT / "README.md").read_text()
    # All CLI errors use `spu94: error:` prefix per Plan 3 SUMMARY table.
    assert "spu94: error:" in text
