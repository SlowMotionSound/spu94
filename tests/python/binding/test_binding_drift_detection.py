"""PYBIND-05: import-time drift assertions fire with RuntimeError.

Wave 0 scaffold — real assertions arrive in Plan 1 Task 2.
"""
import pytest

pytestmark = pytest.mark.skip(reason="Wave 0 scaffold — Plan 1 Task 2 implements")


def test_placeholder():
    pass
